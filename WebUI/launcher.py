import os
import subprocess
import threading
import sys
import time
import json
import logging
import argparse
import ctypes
import ctypes.wintypes  # [修改 1] 新增 wintypes
import winreg

# Set up logging for the launcher
log_file_path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'launcher.log')
logging.basicConfig(filename=log_file_path, level=logging.DEBUG, 
                    format='%(asctime)s - %(levelname)s - %(message)s')
logging.info("Starting launcher...")

from PyQt6.QtCore import QUrl, QTimer  # [修改 1] 新增 QTimer
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

def load_config():
    default_config = {
        'exe_path': os.path.join(BASE_DIR, 'build_msvc', 'Release', 'DynamicWallpaper.exe'),
        'start_on_boot': False,
        'startup_video': ''
    }
    if os.path.exists(CONFIG_PATH):
        try:
            with open(CONFIG_PATH, 'r', encoding='utf-8') as f:
                data = json.load(f)
                default_config.update(data)
        except Exception as e:
            logging.error(f"Error reading config.json: {e}")
    else:
        save_config(default_config)
    return default_config

def save_config(config_data):
    try:
        with open(CONFIG_PATH, 'w', encoding='utf-8') as f:
            json.dump(config_data, f, indent=4)
    except Exception as e:
        logging.error(f"Error writing config.json: {e}")

config = load_config()
EXE_PATH = config.get('exe_path', os.path.join(BASE_DIR, 'build_msvc', 'Release', 'DynamicWallpaper.exe'))

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
                
            # Clear startup video if it was deleted
            cfg = load_config()
            if cfg.get('startup_video') == filename:
                cfg['startup_video'] = ''
                save_config(cfg)
                
            return jsonify({'status': 'success', 'message': f'File {filename} deleted'})
        except Exception as e:
            return jsonify({'status': 'error', 'message': str(e)}), 500
    return jsonify({'status': 'error', 'message': 'File not found'}), 404

def set_static_wallpaper(image_path):
    try:
        if os.path.exists(image_path):
            abs_path = os.path.abspath(image_path)
            ctypes.windll.user32.SystemParametersInfoW(20, 0, abs_path, 3)
            logging.info(f"Set static desktop wallpaper to {abs_path}")
    except Exception as e:
        logging.error(f"Failed to set static wallpaper: {e}")

def apply_wallpaper(filename):
    global current_process
    filepath = os.path.join(VIDEOS_DIR, filename)
    if not os.path.exists(filepath):
        raise FileNotFoundError(f"File {filename} does not exist")
        
    # Set the static wallpaper to the thumbnail for a seamless transition
    thumb_path = os.path.join(THUMBNAILS_DIR, f"{filename}.jpg")
    set_static_wallpaper(thumb_path)
        
    # Kill the existing process if running
    if current_process is not None:
        logging.info("Terminating existing process...")
        try:
            current_process.terminate()
            current_process.wait(timeout=1) # wait for it to actually die
        except Exception as e:
            logging.error(f"Error terminating process: {e}")
        
    # We can also forcefully kill any remaining DynamicWallpaper.exe instances just in case
    logging.info("Running taskkill on previous instances...")
    os.system('taskkill /F /IM DynamicWallpaper.exe')
    
    # Start new process
    absolute_path = os.path.abspath(filepath)
    logging.info(f"Opening log file for engine at: {os.path.join(BASE_DIR, 'wallpaper_engine_log.txt')}")
    log_file = open(os.path.join(BASE_DIR, 'wallpaper_engine_log.txt'), 'w', encoding='utf-8')
    
    logging.info(f"Calling Popen with EXE_PATH: {EXE_PATH} and video: {absolute_path}")
    # DETACHED_PROCESS = 0x00000008, CREATE_NEW_PROCESS_GROUP = 0x00000200
    CREATE_NO_WINDOW = 0x08000000
    DETACHED_PROCESS = 0x00000008
    
    current_process = subprocess.Popen(
        [EXE_PATH, absolute_path], 
        cwd=BASE_DIR,
        stdout=log_file,
        stderr=log_file,
        creationflags=CREATE_NO_WINDOW | DETACHED_PROCESS
    )
    logging.info(f"Successfully spawned process with PID: {current_process.pid}")

def set_autostart_task(enable):
    import os
    
    startup_dir = os.path.join(os.environ['APPDATA'], 'Microsoft', 'Windows', 'Start Menu', 'Programs', 'Startup')
    vbs_path = os.path.join(startup_dir, 'DynamicWallpaperUI.vbs')
    
    # 加入這行：確保 Startup 資料夾一定存在
    os.makedirs(startup_dir, exist_ok=True)
    
    try:
        if enable:
            script_dir = os.path.abspath(BASE_DIR)
            vbs_content = f"""' 自動產生的背景啟動腳本
Dim WshShell
Set WshShell = CreateObject("WScript.Shell")
WshShell.CurrentDirectory = "{script_dir}"
WshShell.Run "pythonw WebUI\launcher.py --bg", 0, False
Set WshShell = Nothing
"""
            with open(vbs_path, 'w', encoding='utf-8') as f:
                f.write(vbs_content)
            logging.info("Created VBS script in Startup folder")
        else:
            if os.path.exists(vbs_path):
                os.remove(vbs_path)
            logging.info("Removed VBS script from Startup folder")
            
        return True, "Success"
    except Exception as e:
        logging.error(f"Failed to set startup via VBS: {e}")
        # 回傳 False 並且把真正的 exception 轉成字串傳出去
        return False, str(e)

@app.route('/api/settings', methods=['GET', 'POST'])
def manage_settings():
    if request.method == 'GET':
        return jsonify({'status': 'success', 'settings': load_config()})
    else:
        data = request.json
        cfg = load_config()
        if 'start_on_boot' in data:
            enable = bool(data['start_on_boot'])
            
            # 接收兩個回傳值
            success, err_msg = set_autostart_task(enable)
            if not success:
                # 把真正的錯誤訊息傳給 UI，不再顯示那個管理員的假警報
                return jsonify({'status': 'error', 'message': f'寫入開機啟動失敗: {err_msg}'}), 500
            cfg['start_on_boot'] = enable
            
        if 'startup_video' in data:
            cfg['startup_video'] = data['startup_video']
            
        save_config(cfg)
        return jsonify({'status': 'success', 'message': 'Settings saved successfully', 'settings': cfg})

@app.route('/api/set_wallpaper', methods=['POST'])
def set_wallpaper():
    data = request.json
    if not data or 'filename' not in data:
        return jsonify({'status': 'error', 'message': 'No filename provided'}), 400
        
    filename = data['filename']
    filepath = os.path.join(VIDEOS_DIR, filename)
    
    if not os.path.exists(filepath):
         return jsonify({'status': 'error', 'message': 'File does not exist'}), 404
         
    try:
        apply_wallpaper(filename)
        
        # Save as startup video
        cfg = load_config()
        cfg['startup_video'] = filename
        save_config(cfg)
        
        return jsonify({'status': 'success', 'message': f'Started playing {filename}'})
    except Exception as e:
        logging.error(f"Exception in set_wallpaper endpoint: {e}")
        return jsonify({'status': 'error', 'message': str(e)}), 500

def start_server():
    # Use 127.0.0.1 for local desktop binding and turn off debug to avoid restarting threads weirdly in pywebview
    app.run(host='127.0.0.1', port=5000, debug=False)

# [修改 2] 新增繼承自 QMainWindow 的自訂視窗類別，用來監聽喚醒事件
class WallpaperWindow(QMainWindow):
    def __init__(self, bg_mode=False):
        super().__init__()
        self.bg_mode = bg_mode
        
        # 如果不是背景模式，才初始化 UI
        if not self.bg_mode:
            self.setWindowTitle('Dynamic Wallpaper Settings')
            self.resize(1050, 750)
            self.web_view = QWebEngineView()
            self.web_view.setUrl(QUrl("http://127.0.0.1:5000"))
            self.setCentralWidget(self.web_view)

    def nativeEvent(self, eventType, message):
        try:
            # 轉換指標以讀取 Windows 訊息
            msg = ctypes.wintypes.MSG.from_address(message.__int__())
            
            WM_POWERBROADCAST = 0x021B
            PBT_APMRESUMEAUTOMATIC = 0x0012
            PBT_APMRESUMESUSPEND = 0x0007

            # 判斷是否為電源廣播，且狀態為「喚醒」
            if msg.message == WM_POWERBROADCAST:
                if msg.wParam == PBT_APMRESUMEAUTOMATIC or msg.wParam == PBT_APMRESUMESUSPEND:
                    logging.info("偵測到系統從睡眠中喚醒，準備重啟桌布...")
                    
                    # 延遲 3 秒執行，確保 Windows 桌面 (DWM) 已經完全載入準備好
                    QTimer.singleShot(3000, self.restart_wallpaper)
        except Exception as e:
            logging.error(f"處理 nativeEvent 時發生錯誤: {e}")
            
        return super().nativeEvent(eventType, message)

    def restart_wallpaper(self):
        cfg = load_config()
        startup_video = cfg.get('startup_video')
        if startup_video:
            logging.info(f"喚醒後重新載入影片: {startup_video}")
            try:
                apply_wallpaper(startup_video)
            except Exception as e:
                logging.error(f"喚醒重啟桌布失敗: {e}")


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--bg', action='store_true', help='Run in background (no UI)')
    args = parser.parse_args()
    
    def is_wallpaper_running():
        try:
            output = subprocess.check_output(['tasklist', '/FI', 'IMAGENAME eq DynamicWallpaper.exe'], stderr=subprocess.STDOUT)
            return b'DynamicWallpaper.exe' in output
        except:
            return False

    def cleanup_old_registry():
        try:
            key_path = r"Software\Microsoft\Windows\CurrentVersion\Run"
            key = winreg.OpenKey(winreg.HKEY_CURRENT_USER, key_path, 0, winreg.KEY_ALL_ACCESS)
            try:
                winreg.DeleteValue(key, "DynamicWallpaperUI")
                logging.info("Cleaned up old registry autostart key.")
            except FileNotFoundError:
                pass
            winreg.CloseKey(key)
        except Exception as e:
            pass

    cleanup_old_registry()

    # Check for startup video on boot, but ONLY if we are starting up (e.g. from background or not already running)
    cfg = load_config()
    if cfg.get('startup_video'):
        if not is_wallpaper_running():
            try:
                apply_wallpaper(cfg['startup_video'])
            except Exception as e:
                logging.error(f"Failed to auto-start wallpaper: {e}")
        else:
            logging.info("Wallpaper engine is already running, skipping auto-start.")
            
    print(f"Starting DynamicWallpaper Server from: {BASE_DIR}")
    print(f"Videos Directory: {VIDEOS_DIR}")
    print(f"Executable Path: {EXE_PATH}")
    
    # Start Flask API inside a background daemon thread
    flask_thread = threading.Thread(target=start_server)
    flask_thread.daemon = True
    flask_thread.start()
    
    # [修改 3] 移除原本的 while True，統一使用 PyQt 的事件迴圈
    time.sleep(1) # 給 Flask 一秒鐘啟動伺服器
    
    app_gui = QApplication(sys.argv)
    app_gui.setApplicationName("Dynamic Wallpaper")
    
    # 如果是背景模式，確保關閉隱藏視窗時不會意外結束程式
    if args.bg:
        app_gui.setQuitOnLastWindowClosed(False)
    
    # 使用我們剛剛建立的自訂視窗類別
    window = WallpaperWindow(bg_mode=args.bg)
    
    if args.bg:
        logging.info("以背景模式運行 (隱藏 GUI，持續監聽系統事件).")
    else:
        window.show()
    
    # 執行 GUI 事件迴圈
    sys.exit(app_gui.exec())