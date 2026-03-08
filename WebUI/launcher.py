import os
import subprocess
import threading
import sys
import time
import json
import logging

# Set up logging for the launcher
log_file_path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'launcher.log')
logging.basicConfig(filename=log_file_path, level=logging.DEBUG, 
                    format='%(asctime)s - %(levelname)s - %(message)s')
logging.info("Starting launcher...")

from PyQt6.QtCore import QUrl
from PyQt6.QtWidgets import QApplication, QMainWindow
from PyQt6.QtWebEngineWidgets import QWebEngineView

from flask import Flask, jsonify, request, send_from_directory
from werkzeug.utils import secure_filename

app = Flask(__name__)

# Config
BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VIDEOS_DIR = os.path.join(BASE_DIR, 'videos')

# Try loading from config.json first
CONFIG_PATH = os.path.join(BASE_DIR, 'WebUI', 'config.json')
if os.path.exists(CONFIG_PATH):
    try:
        with open(CONFIG_PATH, 'r', encoding='utf-8') as f:
            config_data = json.load(f)
            EXE_PATH = config_data.get('exe_path', os.path.join(BASE_DIR, 'build_msvc', 'Release', 'DynamicWallpaper.exe'))
    except Exception as e:
        logging.error(f"Error reading config.json: {e}")
        EXE_PATH = os.path.join(BASE_DIR, 'build_msvc', 'Release', 'DynamicWallpaper.exe')
else:
    EXE_PATH = os.path.join(BASE_DIR, 'build_msvc', 'Release', 'DynamicWallpaper.exe')
    # Create default config.json
    try:
        with open(CONFIG_PATH, 'w', encoding='utf-8') as f:
            json.dump({'exe_path': EXE_PATH}, f, indent=4)
    except Exception as e:
        logging.error(f"Error creating config.json: {e}")

ALLOWED_EXTENSIONS = {'mp4', 'avi', 'mkv', 'mov'}

# Ensure directories exist
THUMBNAILS_DIR = os.path.join(BASE_DIR, 'WebUI', 'static', 'thumbnails')
os.makedirs(VIDEOS_DIR, exist_ok=True)

# Keep track of the running wallpaper process
current_process = None

def allowed_file(filename):
    return '.' in filename and filename.rsplit('.', 1)[1].lower() in ALLOWED_EXTENSIONS

def generate_thumbnail(video_path, thumbnail_path):
    import cv2
    if not os.path.exists(thumbnail_path):
        try:
            cam = cv2.VideoCapture(video_path)
            ret, frame = cam.read()
            if ret:
                cv2.imwrite(thumbnail_path, frame)
            cam.release()
        except Exception as e:
            print(f"Error generating thumbnail for {video_path}: {e}")

@app.route('/')
def index():
    return app.send_static_file('index.html')

@app.route('/api/videos', methods=['GET'])
def list_videos():
    try:
        videos = []
        for filename in os.listdir(VIDEOS_DIR):
            if allowed_file(filename):
                filepath = os.path.join(VIDEOS_DIR, filename)
                size = os.path.getsize(filepath)
                
                # Generate thumbnail if it doesn't exist
                thumb_filename = f"{filename}.jpg"
                thumb_path = os.path.join(THUMBNAILS_DIR, thumb_filename)
                generate_thumbnail(filepath, thumb_path)
                
                thumbnail_url = f"/static/thumbnails/{thumb_filename}" if os.path.exists(thumb_path) else None
                
                videos.append({
                    'name': filename,
                    'size': size,
                    'thumbnail': thumbnail_url
                })
        return jsonify({'status': 'success', 'videos': videos})
    except Exception as e:
        return jsonify({'status': 'error', 'message': str(e)}), 500

@app.route('/api/upload', methods=['POST'])
def upload_video():
    if 'video' not in request.files:
        return jsonify({'status': 'error', 'message': 'No video part in the request'}), 400
    
    file = request.files['video']
    if file.filename == '':
        return jsonify({'status': 'error', 'message': 'No selected file'}), 400
        
    if file and allowed_file(file.filename):
        filename = secure_filename(file.filename)
        filepath = os.path.join(VIDEOS_DIR, filename)
        file.save(filepath)
        
        # Generate thumbnail right after upload
        thumb_filename = f"{filename}.jpg"
        thumb_path = os.path.join(THUMBNAILS_DIR, thumb_filename)
        generate_thumbnail(filepath, thumb_path)
        
        return jsonify({'status': 'success', 'message': f'File {filename} uploaded successfully'})
    
    return jsonify({'status': 'error', 'message': 'Invalid file type'}), 400

@app.route('/api/videos/<filename>', methods=['DELETE'])
def delete_video(filename):
    if not allowed_file(filename):
        return jsonify({'status': 'error', 'message': 'Invalid filename'}), 400
        
    filepath = os.path.join(VIDEOS_DIR, filename)
    thumb_path = os.path.join(THUMBNAILS_DIR, f"{filename}.jpg")
    
    if os.path.exists(filepath):
        try:
            # First, if the video is currently playing, we should probably stop it or at least note it
            # But straightforward delete for now
            os.remove(filepath)
            if os.path.exists(thumb_path):
                os.remove(thumb_path)
            return jsonify({'status': 'success', 'message': f'File {filename} deleted'})
        except Exception as e:
            return jsonify({'status': 'error', 'message': str(e)}), 500
    return jsonify({'status': 'error', 'message': 'File not found'}), 404

@app.route('/api/set_wallpaper', methods=['POST'])
def set_wallpaper():
    global current_process
    
    data = request.json
    if not data or 'filename' not in data:
        return jsonify({'status': 'error', 'message': 'No filename provided'}), 400
        
    filename = data['filename']
    filepath = os.path.join(VIDEOS_DIR, filename)
    
    if not os.path.exists(filepath):
         return jsonify({'status': 'error', 'message': 'File does not exist'}), 404
         
    try:
        # Kill the existing process if running
        if current_process is not None:
            logging.info("Terminating existing process...")
            current_process.terminate()
            current_process.wait() # wait for it to actually die
            
        # We can also forcefully kill any remaining DynamicWallpaper.exe instances just in case
        logging.info("Running taskkill on previous instances...")
        subprocess.run(['taskkill', '/F', '/IM', 'DynamicWallpaper.exe'], 
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        
        # Start new process
        absolute_path = os.path.abspath(filepath)
        logging.info(f"Opening log file for engine at: {os.path.join(BASE_DIR, 'wallpaper_engine_log.txt')}")
        log_file = open(os.path.join(BASE_DIR, 'wallpaper_engine_log.txt'), 'w', encoding='utf-8')
        
        logging.info(f"Calling Popen with EXE_PATH: {EXE_PATH} and video: {absolute_path}")
        current_process = subprocess.Popen(
            [EXE_PATH, absolute_path], 
            cwd=BASE_DIR,
            stdout=log_file,
            stderr=log_file,
            creationflags=subprocess.CREATE_NO_WINDOW
        )
        logging.info(f"Successfully spawned process with PID: {current_process.pid}")
        
        return jsonify({'status': 'success', 'message': f'Started playing {filename}'})
    except Exception as e:
        logging.error(f"Exception in set_wallpaper endpoint: {e}")
        return jsonify({'status': 'error', 'message': str(e)}), 500

def start_server():
    # Use 127.0.0.1 for local desktop binding and turn off debug to avoid restarting threads weirdly in pywebview
    app.run(host='127.0.0.1', port=5000, debug=False)

if __name__ == '__main__':
    print(f"Starting DynamicWallpaper Server from: {BASE_DIR}")
    print(f"Videos Directory: {VIDEOS_DIR}")
    print(f"Executable Path: {EXE_PATH}")
    
    # Start Flask API inside a background daemon thread
    flask_thread = threading.Thread(target=start_server)
    flask_thread.daemon = True
    flask_thread.start()
    
    # Give Flask a second to spin up the server
    time.sleep(1)
    
    # Start the PyQt Desktop GUI
    app_gui = QApplication(sys.argv)
    app_gui.setApplicationName("Dynamic Wallpaper")
    
    window = QMainWindow()
    window.setWindowTitle('Dynamic Wallpaper Settings')
    window.resize(1050, 750)
    
    web_view = QWebEngineView()
    web_view.setUrl(QUrl("http://127.0.0.1:5000"))
    window.setCentralWidget(web_view)
    
    window.show()
    
    # Run the GUI event loop
    sys.exit(app_gui.exec())
