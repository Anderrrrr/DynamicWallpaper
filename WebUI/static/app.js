document.addEventListener('DOMContentLoaded', () => {
    // --- Navigation Logic ---
    const navGallery = document.getElementById('nav-gallery');
    const navUpload = document.getElementById('nav-upload');
    const navSettings = document.getElementById('nav-settings');
    const viewGallery = document.getElementById('view-gallery');
    const viewUpload = document.getElementById('view-upload');
    const viewSettings = document.getElementById('view-settings');

    function switchView(view) {
        // Update nav active states
        [navGallery, navUpload, navSettings].forEach(nav => nav.classList.remove('active'));
        // Hide all views
        [viewGallery, viewUpload, viewSettings].forEach(v => { if (v) v.classList.remove('active') });

        if (view === 'gallery') {
            navGallery.classList.add('active');
            viewGallery.classList.add('active');
            fetchVideos(); // Refresh list when switching back
        } else if (view === 'upload') {
            navUpload.classList.add('active');
            viewUpload.classList.add('active');
        } else if (view === 'settings') {
            navSettings.classList.add('active');
            viewSettings.classList.add('active');
            fetchSettings();
        }
    }

    navGallery.addEventListener('click', (e) => { e.preventDefault(); switchView('gallery'); });
    navUpload.addEventListener('click', (e) => { e.preventDefault(); switchView('upload'); });
    navSettings.addEventListener('click', (e) => { e.preventDefault(); switchView('settings'); });

    // --- Toast Notification ---
    const toast = document.getElementById('toast');
    const toastMsg = document.querySelector('.toast-message');
    const toastIcon = document.querySelector('.toast-icon');
    let toastTimeout;

    function showToast(message, isSuccess = true) {
        toastMsg.textContent = message;
        if (isSuccess) {
            toastIcon.textContent = '✓';
            toastIcon.style.background = 'var(--accent)';
        } else {
            toastIcon.textContent = '!';
            toastIcon.style.background = 'var(--danger)';
        }

        toast.classList.add('show');
        clearTimeout(toastTimeout);
        toastTimeout = setTimeout(() => {
            toast.classList.remove('show');
        }, 3000);
    }

    // --- Gallery Logic ---
    const videoGrid = document.getElementById('video-grid');

    async function fetchVideos() {
        try {
            const response = await fetch('/api/videos');
            const data = await response.json();

            if (data.status === 'success') {
                renderVideos(data.videos);
            } else {
                showToast(data.message, false);
            }
        } catch (error) {
            console.error('Error fetching videos:', error);
            showToast('Failed to load videos from server', false);
        }
    }

    function formatBytes(bytes, decimals = 2) {
        if (!+bytes) return '0 Bytes';
        const k = 1024;
        const dm = decimals < 0 ? 0 : decimals;
        const sizes = ['Bytes', 'KB', 'MB', 'GB', 'TB'];
        const i = Math.floor(Math.log(bytes) / Math.log(k));
        return `${parseFloat((bytes / Math.pow(k, i)).toFixed(dm))} ${sizes[i]}`;
    }

    function renderVideos(videos) {
        videoGrid.innerHTML = '';

        if (videos.length === 0) {
            videoGrid.innerHTML = `
                <div class="loading-state" style="grid-column: 1 / -1;">
                    <p>No videos found. Upload one to get started!</p>
                </div>
            `;
            return;
        }

        videos.forEach(video => {
            const card = document.createElement('div');
            card.className = 'video-card';
            card.onclick = () => window.setWallpaper(video.name);

            const thumbStyle = video.thumbnail ? `style="background-image: url('${video.thumbnail}'); background-size: cover; background-position: center;"` : '';

            card.innerHTML = `
                <div class="video-thumbnail" ${thumbStyle}>
                    <div class="play-overlay">
                        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polygon points="5 3 19 12 5 21 5 3"></polygon></svg>
                    </div>
                </div>
                <div class="video-info">
                    <div class="video-title" title="${video.name}">${video.name}</div>
                    <div class="video-meta">${formatBytes(video.size)}</div>
                    <button class="delete-btn" onclick="window.deleteVideo('${video.name}', event)" title="Delete Wallpaper">
                        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="3 6 5 6 21 6"></polyline><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"></path><line x1="10" y1="11" x2="10" y2="17"></line><line x1="14" y1="11" x2="14" y2="17"></line></svg>
                    </button>
                </div>
            `;
            videoGrid.appendChild(card);
        });
    }

    window.setWallpaper = async function (filename) {
        try {
            const response = await fetch('/api/set_wallpaper', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ filename })
            });
            const data = await response.json();

            if (data.status === 'success') {
                showToast(`Playing ${filename}`, true);
                // Automatically update the config since backend updates startup_video
                if (settingStartupVideo) {
                    settingStartupVideo.textContent = filename;
                }
            } else {
                showToast(data.message, false);
            }
        } catch (error) {
            console.error('Error setting wallpaper:', error);
            showToast('Failed to connect to engine', false);
        }
    };

    window.deleteVideo = async function (filename, event) {
        if (event) event.stopPropagation();

        if (!confirm(`Are you sure you want to delete ${filename}?`)) {
            return;
        }

        try {
            const response = await fetch(`/api/videos/${filename}`, {
                method: 'DELETE'
            });
            const data = await response.json();

            if (data.status === 'success') {
                showToast(data.message, true);
                fetchVideos(); // refresh the list
            } else {
                showToast(data.message, false);
            }
        } catch (error) {
            console.error('Error deleting video:', error);
            showToast('Failed to delete video', false);
        }
    };

    // --- Upload Logic ---
    const dropzone = document.getElementById('dropzone');
    const fileInput = document.getElementById('file-input');
    const uploadProgressContainer = document.getElementById('upload-progress-container');
    const progressBar = document.getElementById('progress-bar');
    const uploadFilename = document.getElementById('upload-filename');
    const uploadPercentage = document.getElementById('upload-percentage');

    // Prevent default drag behaviors
    ['dragenter', 'dragover', 'dragleave', 'drop'].forEach(eventName => {
        dropzone.addEventListener(eventName, preventDefaults, false);
    });

    function preventDefaults(e) {
        e.preventDefault();
        e.stopPropagation();
    }

    ['dragenter', 'dragover'].forEach(eventName => {
        dropzone.addEventListener(eventName, () => dropzone.classList.add('dragover'), false);
    });

    ['dragleave', 'drop'].forEach(eventName => {
        dropzone.addEventListener(eventName, () => dropzone.classList.remove('dragover'), false);
    });

    dropzone.addEventListener('drop', handleDrop, false);
    fileInput.addEventListener('change', (e) => {
        if (e.target.files.length > 0) handleFiles(e.target.files);
    });

    function handleDrop(e) {
        let dt = e.dataTransfer;
        let files = dt.files;
        if (files.length > 0) handleFiles(files);
    }

    function handleFiles(files) {
        const file = files[0]; // Only handle first file for now

        // Basic extension check
        const ext = file.name.split('.').pop().toLowerCase();
        const allowed = ['mp4', 'avi', 'mkv', 'mov'];
        if (!allowed.includes(ext)) {
            showToast('Unsupported file type. Please use .mp4, .avi, .mkv, or .mov', false);
            return;
        }

        uploadFile(file);
    }

    function uploadFile(file) {
        uploadProgressContainer.style.display = 'block';
        uploadFilename.textContent = file.name;
        progressBar.style.width = '0%';
        uploadPercentage.textContent = '0%';

        const formData = new FormData();
        formData.append('video', file);

        const xhr = new XMLHttpRequest();
        xhr.open('POST', '/api/upload', true);

        xhr.upload.onprogress = (e) => {
            if (e.lengthComputable) {
                const percentComplete = Math.round((e.loaded / e.total) * 100);
                progressBar.style.width = percentComplete + '%';
                uploadPercentage.textContent = percentComplete + '%';
            }
        };

        xhr.onload = () => {
            if (xhr.status === 200) {
                showToast('Video uploaded successfully!', true);
                setTimeout(() => {
                    uploadProgressContainer.style.display = 'none';
                    switchView('gallery'); // Switch back to gallery view
                }, 1000);
            } else {
                let msg = 'Upload failed';
                try { msg = JSON.parse(xhr.responseText).message; } catch (e) { }
                showToast(msg, false);
                uploadProgressContainer.style.display = 'none';
            }
        };

        xhr.onerror = () => {
            showToast('Network error during upload', false);
            uploadProgressContainer.style.display = 'none';
        };

        xhr.send(formData);
    }

    // --- Settings Logic ---
    const settingAutostart = document.getElementById('setting-autostart');
    const settingStartupVideo = document.getElementById('setting-startup-video');
    const settingSlider = document.getElementById('setting-slider'); // The visible toggle 

    function updateToggleVisuals(isEnabled) {
        if (isEnabled) {
            settingSlider.classList.add('active');
        } else {
            settingSlider.classList.remove('active');
        }
    }

    async function fetchSettings() {
        try {
            const response = await fetch('/api/settings');
            const data = await response.json();
            if (data.status === 'success') {
                const config = data.settings;
                settingAutostart.checked = !!config.start_on_boot;
                updateToggleVisuals(settingAutostart.checked);
                settingStartupVideo.textContent = config.startup_video || 'None';
            }
        } catch (error) {
            console.error('Error fetching settings:', error);
        }
    }

    settingAutostart.addEventListener('change', async (e) => {
        const isEnabled = e.target.checked;
        updateToggleVisuals(isEnabled); // Optimistic UI update
        try {
            const response = await fetch('/api/settings', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ start_on_boot: isEnabled })
            });
            const data = await response.json();
            if (data.status === 'success') {
                showToast(isEnabled ? 'Autostart Enabled' : 'Autostart Disabled', true);
            } else {
                showToast(data.message, false);
                settingAutostart.checked = !isEnabled; // Revert
                updateToggleVisuals(!isEnabled);
            }
        } catch (error) {
            console.error('Error saving settings:', error);
            showToast('Failed to save settings', false);
            settingAutostart.checked = !isEnabled; // Revert
            updateToggleVisuals(!isEnabled);
        }
    });

    // Initialize
    fetchVideos();
});
