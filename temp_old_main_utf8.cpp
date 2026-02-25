// clang-format off
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <commctrl.h> // Added for ListView functions
// clang-format on
#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "VideoPlayer.h"
#include <fstream>
#include <sstream>

void LogMsg(const std::string &msg) {
  std::ofstream out("C:\\DynamicWallpaperSource\\wallpaper_log.txt",
                    std::ios::app);
  out << msg << std::endl;
}

// Globals
struct MonitorInstance {
  HWND hwnd;
  VideoPlayer *player;
  RECT rect;
};

std::vector<MonitorInstance> g_Monitors;
HWND g_hwndWorkerW = nullptr;
HWND g_hwndDefView = nullptr;
std::atomic<bool> g_bRunning(true);
HWND g_TargetBackground = nullptr;
HINSTANCE g_hInstance = nullptr;
const char CLASS_NAME[] = "DynamicWallpaperClass";

std::vector<std::wstring> g_VideoPaths;
bool g_bSpanMode = true;

#define WM_USER_PLAY (WM_USER + 1)
#define WM_USER_PAUSE (WM_USER + 2)

// Callback to find the specific WorkerW that acts as the desktop background
// layer
// Global to store a fallback WorkerW
HWND g_hwndFallbackWorkerW = nullptr;

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
  HWND p = FindWindowEx(hwnd, NULL, "SHELLDLL_DefView", NULL);
  if (p != NULL) {
    g_hwndWorkerW = FindWindowEx(NULL, hwnd, "WorkerW", NULL);
  }
  return TRUE;
}

HWND GetWallpaperWindow() {
  HWND progman = FindWindow("Progman", nullptr);
  if (progman) {
    // ?潮??潸翰???惜
    SendMessageTimeout(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, nullptr);
    Sleep(100); // 蝔凝蝑?銝銝?  }

  HWND workerW = nullptr;
  EnumWindows(
      [](HWND hwnd, LPARAM lParam) -> BOOL {
        HWND defView = FindWindowEx(hwnd, NULL, "SHELLDLL_DefView", NULL);
        if (defView != NULL) {
          HWND *target = (HWND *)lParam;
          *target = FindWindowEx(NULL, hwnd, "WorkerW", NULL);
          return FALSE;
        }
        return TRUE;
      },
      (LPARAM)&workerW);

  if (workerW != nullptr) {
    LogMsg("Successfully found the background WorkerW via DefView.");
    ShowWindow(workerW, SW_SHOW); // ?儭?閫??嚗撥?園＊蝷箇撣?    return workerW;
  }

  // ??寞?嚗??曄征??WorkerW
  HWND fallbackWorker = nullptr;
  EnumWindows(
      [](HWND hwnd, LPARAM lParam) -> BOOL {
        char cls[256];
        GetClassNameA(hwnd, cls, sizeof(cls));
        if (std::string(cls) == "WorkerW") {
          if (FindWindowEx(hwnd, NULL, NULL, NULL) == NULL) {
            HWND *target = (HWND *)lParam;
            *target = hwnd;
            return FALSE;
          }
        }
        return TRUE;
      },
      (LPARAM)&fallbackWorker);

  if (fallbackWorker != nullptr) {
    LogMsg("Found fallback WorkerW (empty layer).");
    ShowWindow(fallbackWorker, SW_SHOW); // ?儭?閫??嚗撥?園＊蝷箇撣?    return fallbackWorker;
  }

  LogMsg("Fallback: using Progman directly");
  return progman;
}

bool IsFullscreenAppRunning() {
  HWND hwnd = GetForegroundWindow();
  if (!hwnd)
    return false;

  char className[256] = {0};
  GetClassNameA(hwnd, className, sizeof(className));

  // Ignore desktop foreground windows
  if (strcmp(className, "WorkerW") == 0 || strcmp(className, "Progman") == 0)
    return false;

  RECT rc;
  GetWindowRect(hwnd, &rc);
  int width = rc.right - rc.left;
  int height = rc.bottom - rc.top;

  int screenWidth = GetSystemMetrics(SM_CXSCREEN);
  int screenHeight = GetSystemMetrics(SM_CYSCREEN);

  return (width == screenWidth && height == screenHeight);
}

void MonitorThread() {
  while (g_bRunning) {
    bool hide = IsFullscreenAppRunning();
    // LogMsg("MonitorThread heartbeat. hide=" + std::to_string(hide));
    for (auto &mon : g_Monitors) {
      if (hide) {
        PostMessage(mon.hwnd, WM_USER_PAUSE, 0, 0);
      } else {
        PostMessage(mon.hwnd, WM_USER_PLAY, 0, 0);
      }
    }
    Sleep(1000); // Check once per second
  }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam,
                            LPARAM lParam) {
  VideoPlayer *player = nullptr;
  if (uMsg == WM_CREATE) {
    CREATESTRUCT *pcs = (CREATESTRUCT *)lParam;
    player = (VideoPlayer *)pcs->lpCreateParams;
    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)player);
  } else {
    player = (VideoPlayer *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
  }

  switch (uMsg) {
  case WM_USER_PLAY:
    if (player && !player->IsPlaying()) {
      player->Play();
    }
    return 0;
  case WM_USER_PAUSE:
    if (player && player->IsPlaying()) {
      player->Pause();
    }
    return 0;
  case WM_SIZE:
    if (player) {
      player->ResizeVideo(LOWORD(lParam), HIWORD(lParam));
    }
    return 0;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// Callback for EnumDisplayMonitors
BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor,
                              LPRECT lprcMonitor, LPARAM dwData) {
  RECT rc = *lprcMonitor;

  MapWindowPoints(NULL, g_TargetBackground, (LPPOINT)&rc, 2);
  int width = rc.right - rc.left;
  int height = rc.bottom - rc.top;
  int x = rc.left; // 頧????撠?X
  int y = rc.top;  // 頧????撠?Y

  size_t monitorIndex = g_Monitors.size();

  // Choose video path
  std::wstring videoPath = L"";
  if (!g_VideoPaths.empty()) {
    videoPath = g_VideoPaths[monitorIndex % g_VideoPaths.size()];
  }

  MonitorInstance mon = {0};
  mon.rect = rc;

  // Create window for this monitor  // Calculate relative coordinates for child
  // window if necessary

  mon.hwnd = CreateWindowEx(0, CLASS_NAME, "Dynamic Wallpaper",
                            WS_CHILD | WS_VISIBLE, x, y, width, height,
                            g_TargetBackground, NULL, g_hInstance, NULL);

  if (mon.hwnd != NULL) {
    // Manually force it to be inside the selected layer
    SetParent(mon.hwnd, g_TargetBackground);

    SetWindowPos(mon.hwnd, HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    mon.player = new VideoPlayer(mon.hwnd);
    SetWindowLongPtr(mon.hwnd, GWLP_USERDATA, (LONG_PTR)mon.player);

    if (SUCCEEDED(mon.player->Initialize())) {
      if (SUCCEEDED(mon.player->OpenFile(videoPath))) {
        mon.player->ResizeVideo(width, height);
        mon.player->Play();
      }
    }

    g_Monitors.push_back(mon);
  }

  return TRUE;
}

void ParseCommandLine() {
  int argc;
  LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argc > 1) {
    std::wstring modeFlag = argv[1];
    if (modeFlag == L"--span") {
      g_bSpanMode = true;
      if (argc > 2) {
        g_VideoPaths.push_back(argv[2]);
      }
    } else if (modeFlag == L"--monitors") {
      g_bSpanMode = false;
      for (int i = 2; i < argc; ++i) {
        g_VideoPaths.push_back(argv[i]);
      }
    } else {
      // Default to span if no flag but has video
      g_bSpanMode = true;
      g_VideoPaths.push_back(argv[1]);
    }
  }

  if (g_VideoPaths.empty()) {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    PathRemoveFileSpecA(path); // DynamicWallpaper.exe
    PathRemoveFileSpecA(path); // Release
    PathRemoveFileSpecA(path); // build_msvc
    std::string fb = std::string(path) + "\\pixel-rain-traffic.3840x2160.mp4";
    g_VideoPaths.push_back(std::wstring(fb.begin(), fb.end()));
  }
  LocalFree(argv);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine,
                   int nCmdShow) {
  // Be DPI aware so GetSystemMetrics returns true 4K resolution instead of
  // scaled 1080p
  SetProcessDPIAware();

  g_hInstance = hInstance;
  HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
  if (FAILED(hr))
    return -1;

  LogMsg("--- Application Started ---");
  ParseCommandLine();

  LogMsg("Video Paths count: " + std::to_string(g_VideoPaths.size()));
  if (!g_VideoPaths.empty()) {
    std::wstring vp = g_VideoPaths[0];
    LogMsg("Video[0]: " + std::string(vp.begin(), vp.end()));
  }

  g_TargetBackground = GetWallpaperWindow();
  if (!g_TargetBackground) {
    // If GetWallpaperWindow returns NULL, we try to use Progman as a fallback
    // for SetParent. This is for cases where WorkerW isn't found or DefView is
    // used.
    g_TargetBackground = FindWindow("Progman", nullptr);
    if (!g_TargetBackground) {
      LogMsg("Failed to find Progman as a fallback target.");
      MessageBoxA(NULL, "Failed to find desktop background window.", "Error",
                  MB_OK);
      CoUninitialize();
      return -1;
    }
    LogMsg(
        "GetWallpaperWindow returned NULL. Using Progman as fallback target.");
  }
  LogMsg("g_TargetBackground handle: " +
         std::to_string((unsigned long long)g_TargetBackground));

  WNDCLASS wc = {};
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = hInstance;
  wc.lpszClassName = CLASS_NAME;
  wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  RegisterClass(&wc);

  if (g_bSpanMode) {
    RECT parentRect;
    GetClientRect(g_TargetBackground, &parentRect);
    int clientWidth = parentRect.right - parentRect.left;
    int clientHeight = parentRect.bottom - parentRect.top;

    MonitorInstance mon = {0};
    mon.rect = parentRect;
    mon.hwnd = CreateWindowEx(
        0, CLASS_NAME, "Dynamic Wallpaper", WS_CHILD | WS_VISIBLE, 0, 0,
        clientWidth, clientHeight, g_TargetBackground, NULL, hInstance, NULL);

    if (mon.hwnd != NULL) {
      // Manually force it to be inside the selected layer
      SetParent(mon.hwnd, g_TargetBackground);

      SetWindowPos(mon.hwnd, HWND_BOTTOM, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
      mon.player = new VideoPlayer(mon.hwnd);
      SetWindowLongPtr(mon.hwnd, GWLP_USERDATA, (LONG_PTR)mon.player);

      LogMsg("Initializing player for span mode");
      HRESULT hrInit = mon.player->Initialize();
      if (SUCCEEDED(hrInit)) {
        HRESULT hrOpen = mon.player->OpenFile(g_VideoPaths[0]);
        if (SUCCEEDED(hrOpen)) {
          LogMsg("OpenFile succeeded");
          mon.player->ResizeVideo(clientWidth, clientHeight);
          mon.player->Play();
        } else {
          LogMsg("OpenFile failed: " + std::to_string(hrOpen));
        }
      } else {
        LogMsg("Initialize failed: " + std::to_string(hrInit));
      }
      g_Monitors.push_back(mon);
    }
  } else {
    // Enumerate physical monitors and spawn independent windows
    EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, 0);
  }

  if (g_Monitors.empty()) {
    MessageBoxA(NULL, "Failed to create any video surfaces.", "Error", MB_OK);
    CoUninitialize();
    return -1;
  }

  // Start background thread for auto-pause magic
  std::thread monitorThread(MonitorThread);

  // Standard message loop
  MSG msg = {};
  while (GetMessage(&msg, NULL, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  // Cleanup
  g_bRunning = false;
  monitorThread.join();

  for (auto &mon : g_Monitors) {
    if (mon.player) {
      mon.player->Shutdown();
      delete mon.player;
    }
  }

  CoUninitialize();
  return 0;
}
