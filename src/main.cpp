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
#define WM_USER_INIT_VIDEO (WM_USER + 3)

// Callback to find the specific WorkerW that acts as the desktop background
// layer
// Global to store a fallback WorkerW
HWND g_hwndFallbackWorkerW = nullptr;

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
  HWND p = FindWindowEx(hwnd, NULL, "SHELLDLL_DefView", NULL);
  if (p != NULL) {
    g_hwndDefView = p; // 儲存 DefView 序號，稍後用於 Z-Order 定位
    g_hwndWorkerW = FindWindowEx(NULL, hwnd, "WorkerW", NULL);
  }
  return TRUE;
}

HWND GetWallpaperWindow() {
  HWND progman = FindWindow("Progman", nullptr);
  if (!progman) {
    LogMsg("ERROR: Progman not found!");
    return nullptr;
  }

  DWORD_PTR result = 0;
  SendMessageTimeout(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, &result);
  Sleep(500); // 延長等待時間，25H2 有時動作比較慢

  g_hwndDefView = FindWindowEx(progman, NULL, "SHELLDLL_DefView", NULL);
  if (!g_hwndDefView) {
    EnumWindows(
        [](HWND hwnd, LPARAM lParam) -> BOOL {
          HWND p = FindWindowEx(hwnd, NULL, "SHELLDLL_DefView", NULL);
          if (p != NULL) {
            g_hwndDefView = p;
            return FALSE;
          }
          return TRUE;
        },
        0);
  }

  HWND workerW = FindWindowEx(progman, NULL, "WorkerW", NULL);
  if (!workerW) {
    EnumWindows(
        [](HWND hwnd, LPARAM lParam) -> BOOL {
          char className[256];
          GetClassNameA(hwnd, className, sizeof(className));
          if (strcmp(className, "WorkerW") == 0 &&
              GetWindow(hwnd, GW_CHILD) == NULL) {
            *(HWND *)lParam = hwnd;
            return FALSE;
          }
          return TRUE;
        },
        (LPARAM)&workerW);
  }
  g_hwndWorkerW = workerW;

  HWND target = workerW ? workerW : progman;

  LONG pStyle = GetWindowLong(progman, GWL_STYLE);
  SetWindowLong(progman, GWL_STYLE, pStyle | WS_CLIPCHILDREN);
  LogMsg("Applied WS_CLIPCHILDREN to Progman.");

  if (target && target != progman) {
    LONG tStyle = GetWindowLong(target, GWL_STYLE);
    SetWindowLong(target, GWL_STYLE, tStyle | WS_CLIPCHILDREN);
    LogMsg("Applied WS_CLIPCHILDREN to WorkerW.");
  }

  LogMsg("Returning final target canvas.");
  return target;
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
  case WM_USER_INIT_VIDEO:
    if (player) {
      if (!g_VideoPaths.empty()) {
        HRESULT hrOpen = player->OpenFile(g_VideoPaths[0]);
        if (SUCCEEDED(hrOpen)) {
          LogMsg("OpenFile succeeded. Waiting for Topology Ready...");
        } else {
          LogMsg("OpenFile failed: 0x" + [&]() {
            std::ostringstream ss;
            ss << std::hex << hrOpen;
            return ss.str();
          }());
        }
      }
    }
    return 0;

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

  case WM_ERASEBKGND: {
    HDC hdc = (HDC)wParam;
    RECT rc;
    GetClientRect(hwnd, &rc);
    FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
    return 1; // 告訴 Windows 我們已經把背景塗黑了，不要再閃白光
  }
  case WM_DESTROY: {
    PostQuitMessage(0);
    return 0;
  }
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
  int x = rc.left; // 轉換過後的相對 X
  int y = rc.top;  // 轉換過後的相對 Y

  size_t monitorIndex = g_Monitors.size();

  // Choose video path
  std::wstring videoPath = L"";
  if (!g_VideoPaths.empty()) {
    videoPath = g_VideoPaths[monitorIndex % g_VideoPaths.size()];
  }

  MonitorInstance mon = {0};
  mon.rect = rc;

  // Create window for this monitor  // Calculate relative coordinates for
  // child window if necessary

  mon.hwnd = CreateWindowEx(
      0, CLASS_NAME, "Dynamic Wallpaper",
      WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, x, y, width,
      height, g_TargetBackground, NULL, g_hInstance, NULL);

  if (mon.hwnd != NULL) {
    if (g_TargetBackground == FindWindow("Progman", nullptr) && g_hwndDefView) {
      SetWindowPos(mon.hwnd, g_hwndDefView, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    mon.player = new VideoPlayer(mon.hwnd);
    SetWindowLongPtr(mon.hwnd, GWLP_USERDATA, (LONG_PTR)mon.player);

    if (SUCCEEDED(mon.player->Initialize())) {
      PostMessage(mon.hwnd, WM_USER_INIT_VIDEO, 0, 0);

      if (g_hwndDefView) {
        ShowWindow(g_hwndDefView, SW_HIDE);
        Sleep(20);
        ShowWindow(g_hwndDefView, SW_SHOWNORMAL);
        LogMsg("DefView refreshed to clear snapshot.");
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
  // USER INSTRUCTION 1: EVR requires STA (Apartment) threading model.
  HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
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
    LogMsg(
        "GetWallpaperWindow returned NULL. Failed locating background HWND.");
  }
  LogMsg("g_TargetBackground handle: " +
         std::to_string((unsigned long long)g_TargetBackground));

  // 確保背景視窗可見與重繪，讓 EVR 得以渲染
  if (g_TargetBackground) {
    ShowWindow(g_TargetBackground, SW_SHOW);
    UpdateWindow(g_TargetBackground);
    LogMsg("TargetBackground Show/Update called.");
  }

  WNDCLASS wc = {};
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = hInstance;
  wc.lpszClassName = CLASS_NAME;
  wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  RegisterClass(&wc);

  if (g_bSpanMode) {
    int screenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    LogMsg("Span mode. Screen: " + std::to_string(screenWidth) + "x" +
           std::to_string(screenHeight));

    MonitorInstance mon = {0};
    mon.rect = {0, 0, screenWidth, screenHeight};

    // Strategy: Standalone popup + DefView reparenting + magic pink colorkey
    // 1. Popup at HWND_BOTTOM → video visible
    // 2. SetParent(DefView, ourPopup) → icons on top of video
    // 3. SysListView32 uses magic pink as BG + WS_EX_LAYERED + LWA_COLORKEY
    //    → DWM makes pink transparent → video shows through
    //    → ListView can erase properly → no selection trails

    mon.hwnd = CreateWindowEx(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, CLASS_NAME, "Dynamic Wallpaper",
        WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, screenWidth,
        screenHeight, NULL, NULL, hInstance, NULL);

    if (mon.hwnd != NULL) {
      LogMsg("Created standalone popup: " +
             std::to_string((unsigned long long)mon.hwnd));

      // Position at HWND_BOTTOM
      SetWindowPos(mon.hwnd, HWND_BOTTOM, 0, 0, screenWidth, screenHeight,
                   SWP_NOACTIVATE | SWP_SHOWWINDOW);
      LogMsg("Positioned at HWND_BOTTOM.");

      // Reparent DefView into our popup so icons render on top of video
      if (g_hwndDefView) {
        HWND oldParent = SetParent(g_hwndDefView, mon.hwnd);
        LogMsg("Reparented DefView. OldParent: " +
               std::to_string((unsigned long long)oldParent));

        SetWindowPos(g_hwndDefView, HWND_TOP, 0, 0, screenWidth, screenHeight,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);

        // Kill DefView's own background so it doesn't paint over video
        SetClassLongPtr(g_hwndDefView, GCLP_HBRBACKGROUND,
                        (LONG_PTR)GetStockObject(HOLLOW_BRUSH));

        COLORREF magicPink = RGB(255, 0, 255);

        // Set SysListView32 BG to magic pink + colorkey
        HWND hListView =
            FindWindowEx(g_hwndDefView, NULL, "SysListView32", NULL);
        if (hListView) {
          SendMessage(hListView, LVM_SETBKCOLOR, 0, (LPARAM)magicPink);
          SendMessage(hListView, LVM_SETTEXTBKCOLOR, 0, (LPARAM)magicPink);

          // Colorkey on SysListView32: DWM makes pink pixels transparent
          LONG lvExStyle = GetWindowLong(hListView, GWL_EXSTYLE);
          SetWindowLong(hListView, GWL_EXSTYLE, lvExStyle | WS_EX_LAYERED);
          SetLayeredWindowAttributes(hListView, magicPink, 0, LWA_COLORKEY);

          RedrawWindow(hListView, NULL, NULL,
                       RDW_ERASE | RDW_INVALIDATE | RDW_ALLCHILDREN);
          LogMsg("Magic pink colorkey on ListView + hollow DefView BG.");
        }
      }

      mon.player = new VideoPlayer(mon.hwnd);
      SetWindowLongPtr(mon.hwnd, GWLP_USERDATA, (LONG_PTR)mon.player);

      LogMsg("Initializing player for span mode");
      HRESULT hrInit = mon.player->Initialize();
      if (SUCCEEDED(hrInit)) {
        PostMessage(mon.hwnd, WM_USER_INIT_VIDEO, 0, 0);
      } else {
        LogMsg("Initialize failed: " + std::to_string(hrInit));
      }
      g_Monitors.push_back(mon);
    } else {
      LogMsg("ERROR: CreateWindowEx failed! Error code: " +
             std::to_string(GetLastError()));
    }
  } else {
    // 復原原本的多螢幕模式邏輯
    EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, 0);
  }

  if (g_Monitors.empty()) {
    MessageBoxA(NULL, "Failed to create any video surfaces.", "Error", MB_OK);
    CoUninitialize();
    return -1;
  }

  // Start background thread for auto-pause magic
  // USER INSTRUCTION 2: Comment out monitorThread to prevent early pause
  // logic std::thread monitorThread(MonitorThread);

  // Standard message loop
  MSG msg = {};
  while (GetMessage(&msg, NULL, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  // Cleanup
  g_bRunning = false;
  // monitorThread.join();

  for (auto &mon : g_Monitors) {
    if (mon.player) {
      mon.player->Shutdown();
      delete mon.player;
    }
  }

  CoUninitialize();
  return 0;
}
