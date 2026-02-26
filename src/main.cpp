// clang-format off
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1.h>
// clang-format on
#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "VideoPlayer.h"
#include <fstream>
#include <sstream>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")

void LogMsg(const std::string &msg) {
  std::ofstream out("C:\\Users\\ander\\Dynamic_wallpaper\\debug_log.txt",
                    std::ios::app);
  out << msg << std::endl;
}

class D2DRenderer {
public:
  D2DRenderer(HWND hwnd)
      : m_hwnd(hwnd), m_pDevice(nullptr), m_pContext(nullptr),
        m_pSwapChain(nullptr), m_pRenderTarget(nullptr), m_pBitmap(nullptr),
        m_videoWidth(0), m_videoHeight(0), m_pD2DFactory(nullptr) {}
  ~D2DRenderer() { Cleanup(); }

  bool Initialize(int width, int height) {
    LogMsg("D2DRenderer::Initialize: Enter");
    HRESULT hr;
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1};

    LogMsg("D2DRenderer::Initialize: Calling D3D11CreateDevice");
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                           D3D11_CREATE_DEVICE_BGRA_SUPPORT, featureLevels, 3,
                           D3D11_SDK_VERSION, &m_pDevice, nullptr, &m_pContext);
    if (FAILED(hr)) {
      LogMsg("D3D11CreateDevice failed: " + std::to_string(hr));
      return false;
    }

    LogMsg("D2DRenderer::Initialize: Getting DXGI objects");
    IDXGIDevice *pDXGIDevice = nullptr;
    hr =
        m_pDevice->QueryInterface(__uuidof(IDXGIDevice), (void **)&pDXGIDevice);
    if (FAILED(hr) || !pDXGIDevice) {
      LogMsg("QueryInterface IDXGIDevice failed");
      return false;
    }

    IDXGIAdapter *pAdapter = nullptr;
    hr = pDXGIDevice->GetAdapter(&pAdapter);
    if (FAILED(hr) || !pAdapter) {
      LogMsg("GetAdapter failed");
      return false;
    }

    IDXGIFactory2 *pFactory = nullptr;
    hr = pAdapter->GetParent(__uuidof(IDXGIFactory2), (void **)&pFactory);
    if (FAILED(hr) || !pFactory) {
      LogMsg("GetParent IDXGIFactory2 failed");
      return false;
    }

    DXGI_SWAP_CHAIN_DESC1 swapDesc = {};
    swapDesc.Width = width;
    swapDesc.Height = height;
    swapDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount = 2;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapDesc.AlphaMode =
        DXGI_ALPHA_MODE_IGNORE; // Changed to IGNORE for opaque WS_POPUP

    LogMsg("D2DRenderer::Initialize: Calling CreateSwapChainForHwnd");
    hr = pFactory->CreateSwapChainForHwnd(m_pDevice, m_hwnd, &swapDesc, nullptr,
                                          nullptr, &m_pSwapChain);

    pFactory->Release();
    pAdapter->Release();
    pDXGIDevice->Release();

    if (FAILED(hr)) {
      LogMsg("CreateSwapChainForHwnd failed: " +
             std::to_string((unsigned int)hr));
      return false;
    }

    LogMsg("D2DRenderer::Initialize: Calling D2D1CreateFactory");
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory);
    if (FAILED(hr)) {
      LogMsg("D2D1CreateFactory failed: " + std::to_string((unsigned int)hr));
      return false;
    }

    LogMsg("D2DRenderer::Initialize: Calling CreateRenderTarget");
    CreateRenderTarget();
    LogMsg("D2DRenderer::Initialize: Success");
    return true;
  }

  void CreateRenderTarget() {
    IDXGISurface *pBackBuffer = nullptr;
    m_pSwapChain->GetBuffer(0, __uuidof(IDXGISurface), (void **)&pBackBuffer);

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED));

    m_pD2DFactory->CreateDxgiSurfaceRenderTarget(pBackBuffer, &props,
                                                 &m_pRenderTarget);
    pBackBuffer->Release();
  }

  void Resize(int width, int height) {
    if (!m_pSwapChain)
      return;
    if (m_pRenderTarget) {
      m_pRenderTarget->Release();
      m_pRenderTarget = nullptr;
    }
    m_pSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    CreateRenderTarget();
  }

  void RenderFrame(const BYTE *pData, UINT width, UINT height, DWORD cbLength) {
    if (!m_pRenderTarget)
      return;

    if (!m_pBitmap || m_videoWidth != width || m_videoHeight != height) {
      if (m_pBitmap)
        m_pBitmap->Release();
      m_pBitmap = nullptr;

      D2D1_BITMAP_PROPERTIES props = {};
      props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
      props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
      props.dpiX = 96.0f;
      props.dpiY = 96.0f;

      UINT32 pitch = cbLength / height;
      m_pRenderTarget->CreateBitmap(D2D1::SizeU(width, height), pData, pitch,
                                    &props, &m_pBitmap);
      m_videoWidth = width;
      m_videoHeight = height;
    } else {
      UINT32 pitch = cbLength / height;
      D2D1_RECT_U destRect = D2D1::RectU(0, 0, width, height);
      m_pBitmap->CopyFromMemory(&destRect, pData, pitch);
    }

    m_pRenderTarget->BeginDraw();
    m_pRenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::Black));

    D2D1_SIZE_F rtSize = m_pRenderTarget->GetSize();
    float srcAspect = (float)width / height;
    float dstAspect = rtSize.width / rtSize.height;

    D2D1_RECT_F destRect = D2D1::RectF(0, 0, rtSize.width, rtSize.height);
    D2D1_RECT_F srcRect = D2D1::RectF(0, 0, (float)width, (float)height);

    if (srcAspect > dstAspect) {
      float visibleWidth = height * dstAspect;
      float margin = (width - visibleWidth) / 2.0f;
      srcRect.left = margin;
      srcRect.right = width - margin;
    } else if (srcAspect < dstAspect) {
      float visibleHeight = width / dstAspect;
      float margin = (height - visibleHeight) / 2.0f;
      srcRect.top = margin;
      srcRect.bottom = height - margin;
    }

    m_pRenderTarget->DrawBitmap(m_pBitmap, destRect, 1.0f,
                                D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                                &srcRect);

    m_pRenderTarget->EndDraw();
    m_pSwapChain->Present(1, 0); // VSYNC
  }

  void Cleanup() {
    if (m_pBitmap) {
      m_pBitmap->Release();
      m_pBitmap = nullptr;
    }
    if (m_pRenderTarget) {
      m_pRenderTarget->Release();
      m_pRenderTarget = nullptr;
    }
    if (m_pSwapChain) {
      m_pSwapChain->Release();
      m_pSwapChain = nullptr;
    }
    if (m_pContext) {
      m_pContext->Release();
      m_pContext = nullptr;
    }
    if (m_pDevice) {
      m_pDevice->Release();
      m_pDevice = nullptr;
    }
    if (m_pD2DFactory) {
      m_pD2DFactory->Release();
      m_pD2DFactory = nullptr;
    }
  }

private:
  HWND m_hwnd;
  ID3D11Device *m_pDevice;
  ID3D11DeviceContext *m_pContext;
  IDXGISwapChain1 *m_pSwapChain;
  ID2D1RenderTarget *m_pRenderTarget;
  ID2D1Bitmap *m_pBitmap;
  UINT m_videoWidth;
  UINT m_videoHeight;
  ID2D1Factory *m_pD2DFactory;
};

// Globals
struct MonitorInstance {
  HWND hwnd;
  VideoPlayer *player;
  D2DRenderer *renderer;
  RECT rect;
};

std::vector<MonitorInstance> g_Monitors;
std::atomic<bool> g_bRunning(true);
HINSTANCE g_hInstance = nullptr;
const char CLASS_NAME[] = "DynamicWallpaperClass";

std::vector<std::wstring> g_VideoPaths;
bool g_bSpanMode = true;

#define WM_USER_PLAY (WM_USER + 1)
#define WM_USER_PAUSE (WM_USER + 2)
#define WM_USER_INIT_VIDEO (WM_USER + 3)

HWND g_hwndDefView = nullptr;

HWND GetDesktopLayer() {
  HWND progman = FindWindow("Progman", nullptr);
  if (!progman) {
    LogMsg("ERROR: Progman not found!");
    return nullptr;
  }

  DWORD_PTR result = 0;
  // Send 0x052C to Progman to split the desktop (create WorkerW)
  SendMessageTimeout(progman, 0x052C, 0x0D, 0x01, SMTO_NORMAL, 1000, &result);

  // Retry loop because Explorer might take a moment
  for (int i = 0; i < 10; ++i) {
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

    if (g_hwndDefView) {
      LogMsg("Successfully found DefView on attempt " + std::to_string(i + 1));
      break;
    }
    Sleep(500);
  }

  if (!g_hwndDefView) {
    LogMsg("WARNING: Failed to find SHELLDLL_DefView. Z-ordering under icons "
           "may fail.");
  }

  return progman;
}

bool IsFullscreenAppRunning() {
  HWND hwnd = GetForegroundWindow();
  if (!hwnd)
    return false;

  char className[256] = {0};
  GetClassNameA(hwnd, className, sizeof(className));

  // Ignore desktop and system shell windows
  if (hwnd == GetDesktopWindow() || hwnd == GetShellWindow())
    return false;

  if (strcmp(className, "WorkerW") == 0 || strcmp(className, "Progman") == 0 ||
      strcmp(className, "Shell_TrayWnd") == 0 ||
      strcmp(className, "Shell_SecondaryTrayWnd") == 0 ||
      strcmp(className, "Windows.UI.Core.CoreWindow") == 0) {
    return false;
  }

  // Must be visible
  if (!IsWindowVisible(hwnd))
    return false;

  // Ignore tool windows
  LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
  if (exStyle & WS_EX_TOOLWINDOW)
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
  MonitorInstance *pMon = nullptr;
  if (uMsg == WM_CREATE) {
    CREATESTRUCT *pcs = (CREATESTRUCT *)lParam;
    pMon = (MonitorInstance *)pcs->lpCreateParams;
    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pMon);
  } else {
    pMon = (MonitorInstance *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
  }

  switch (uMsg) {
  case WM_USER_INIT_VIDEO:
    if (pMon && pMon->player) {
      if (!g_VideoPaths.empty()) {
        HRESULT hrOpen =
            pMon->player->OpenFile(g_VideoPaths[0]); // TODO: handle index
        if (SUCCEEDED(hrOpen)) {
          LogMsg("OpenFile succeeded.");
          pMon->player->Play();
        } else {
          LogMsg("OpenFile failed.");
        }
      }
    }
    return 0;

  case WM_USER_PLAY:
    if (pMon && pMon->player && !pMon->player->IsPlaying()) {
      pMon->player->Play();
    }
    return 0;
  case WM_USER_PAUSE:
    if (pMon && pMon->player && pMon->player->IsPlaying()) {
      pMon->player->Pause();
    }
    return 0;
  case WM_SIZE:
    if (pMon && pMon->renderer) {
      pMon->renderer->Resize(LOWORD(lParam), HIWORD(lParam));
    }
    return 0;

  case WM_ERASEBKGND:
    // We draw the background using D3D11
    return 1;

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

  int width = rc.right - rc.left;
  int height = rc.bottom - rc.top;
  int x = rc.left;
  int y = rc.top;

  size_t monitorIndex = g_Monitors.size();

  HWND progman = GetDesktopLayer();

  MonitorInstance mon = {0};
  mon.rect = rc;

  mon.hwnd = CreateWindowEx(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, CLASS_NAME,
                            "Dynamic Wallpaper", WS_POPUP | WS_VISIBLE, x, y,
                            width, height, NULL, NULL, g_hInstance,
                            &mon); // pass mon loosely, will patch it up

  if (mon.hwnd != NULL) {
    if (progman && g_hwndDefView) {
      SetParent(mon.hwnd, progman);
      SetWindowPos(mon.hwnd, g_hwndDefView, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else {
      SetWindowPos(mon.hwnd, HWND_BOTTOM, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    mon.player = new VideoPlayer();
    mon.renderer = new D2DRenderer(mon.hwnd);

    // Patch the userdata properly now that we have allocated the objects
    g_Monitors.push_back(mon);
    SetWindowLongPtr(mon.hwnd, GWLP_USERDATA, (LONG_PTR)&g_Monitors.back());
    MonitorInstance &refMon = g_Monitors.back();

    if (refMon.renderer->Initialize(width, height)) {
      if (SUCCEEDED(refMon.player->Initialize())) {
        PostMessage(refMon.hwnd, WM_USER_INIT_VIDEO, 0, 0);
      }
    } else {
      LogMsg("Failed to initialize D2D Renderer for monitor!");
    }
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
      g_bSpanMode = true;
      g_VideoPaths.push_back(argv[1]);
    }
  }

  if (g_VideoPaths.empty()) {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    PathRemoveFileSpecA(path);
    PathRemoveFileSpecA(path);
    PathRemoveFileSpecA(path);
    std::string fb = std::string(path) + "\\pixel-rain-traffic.3840x2160.mp4";
    g_VideoPaths.push_back(std::wstring(fb.begin(), fb.end()));
  }
  LocalFree(argv);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR pCmdLine,
                   int nCmdShow) {
  SetProcessDPIAware();

  g_hInstance = hInstance;
  HRESULT hr = CoInitializeEx(
      NULL, COINIT_MULTITHREADED); // Apartment not needed since no EVR
  if (FAILED(hr))
    return -1;

  LogMsg("--- Application Started (D3D11/D2D Bypass) ---");
  ParseCommandLine();

  WNDCLASS wc = {};
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = hInstance;
  wc.lpszClassName = CLASS_NAME;
  wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  RegisterClass(&wc);

  if (g_bSpanMode) {
    int screenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    int screenX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int screenY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    LogMsg("Span mode. Screen: " + std::to_string(screenWidth) + "x" +
           std::to_string(screenHeight));

    HWND progman = GetDesktopLayer();

    MonitorInstance mon = {0};
    mon.rect = {screenX, screenY, screenX + screenWidth,
                screenY + screenHeight};

    mon.hwnd = CreateWindowEx(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, CLASS_NAME,
                              "Dynamic Wallpaper", WS_POPUP | WS_VISIBLE,
                              screenX, screenY, screenWidth, screenHeight, NULL,
                              NULL, hInstance, &mon);

    if (mon.hwnd != NULL) {
      if (progman && g_hwndDefView) {
        SetParent(mon.hwnd, progman);
        SetWindowPos(mon.hwnd, g_hwndDefView, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
      } else {
        SetWindowPos(mon.hwnd, HWND_BOTTOM, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
      }

      mon.player = new VideoPlayer();
      mon.renderer = new D2DRenderer(mon.hwnd);

      g_Monitors.push_back(mon);
      SetWindowLongPtr(mon.hwnd, GWLP_USERDATA, (LONG_PTR)&g_Monitors[0]);

      if (g_Monitors[0].renderer->Initialize(screenWidth, screenHeight)) {
        LogMsg("Span mode D2D Renderer initialized.");
        if (SUCCEEDED(g_Monitors[0].player->Initialize())) {
          LogMsg("Span mode player initialized. Posting WM_USER_INIT_VIDEO.");
          PostMessage(mon.hwnd, WM_USER_INIT_VIDEO, 0, 0);
        } else {
          LogMsg("Failed to initialize player in span mode.");
        }
      } else {
        LogMsg("Failed to initialize D2D Renderer in span mode.");
      }
    }
  } else {
    EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, 0);
  }

  if (g_Monitors.empty()) {
    MessageBoxA(NULL, "Failed to create any video surfaces.", "Error", MB_OK);
    CoUninitialize();
    return -1;
  }

  std::thread monitorThread(MonitorThread);

  MSG msg = {};
  while (g_bRunning) {
    bool hasMessage = false;
    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        g_bRunning = false;
        break;
      }
      TranslateMessage(&msg);
      DispatchMessage(&msg);
      hasMessage = true;
    }

    bool frameRendered = false;
    for (auto &mon : g_Monitors) {
      if (mon.player && mon.renderer) {
        BYTE *pData = NULL;
        DWORD cbLength = 0;
        UINT32 width = 0, height = 0;
        HRESULT hrFrame =
            mon.player->GetNextFrame(&pData, &cbLength, &width, &height);
        if (hrFrame == S_OK && pData) {
          mon.renderer->RenderFrame(pData, width, height, cbLength);
          mon.player->UnlockFrame();
          frameRendered = true;
        }
      }
    }

    // Sleep dynamically to yield CPU if not busy
    if (!hasMessage && !frameRendered) {
      Sleep(1);
    }
  }

  monitorThread.join();

  for (auto &mon : g_Monitors) {
    if (mon.renderer) {
      delete mon.renderer;
    }
    if (mon.player) {
      mon.player->Shutdown();
      delete mon.player;
    }
  }

  CoUninitialize();
  return 0;
}
