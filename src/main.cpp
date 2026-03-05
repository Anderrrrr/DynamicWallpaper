// clang-format off
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <d3d11.h>
#include <dxgi1_2.h>
// clang-format on
#include <atomic>

#include <string>
#include <thread>
#include <vector>

#include "VideoPlayer.h"
#include <sstream>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "shlwapi.lib")

// ── D3DRenderer ──────────────────────────────────────────────────────────────
// Minimal: just swap chain + CopyResource + Present(0,0).
// No VideoProcessor, no Resize, no OutputView caching.
class D3DRenderer {
public:
  D3DRenderer(HWND hwnd)
      : m_hwnd(hwnd), m_pDevice(nullptr), m_pContext(nullptr),
        m_pSwapChain(nullptr), m_pStagingTex(nullptr) {}
  ~D3DRenderer() { Cleanup(); }

  ID3D11Device *GetDevice() const { return m_pDevice; }

  bool Initialize(int width, int height) {
    D3D_FEATURE_LEVEL featureLevels[] = {D3D_FEATURE_LEVEL_11_1,
                                         D3D_FEATURE_LEVEL_11_0};
    HRESULT hr =
        D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                          D3D11_CREATE_DEVICE_BGRA_SUPPORT, featureLevels, 2,
                          D3D11_SDK_VERSION, &m_pDevice, nullptr, &m_pContext);
    if (FAILED(hr)) {
      return false;
    }

    IDXGIDevice *pDXGIDevice = nullptr;
    IDXGIAdapter *pAdapter = nullptr;
    IDXGIFactory2 *pFactory = nullptr;
    m_pDevice->QueryInterface(__uuidof(IDXGIDevice), (void **)&pDXGIDevice);
    pDXGIDevice->GetAdapter(&pAdapter);
    pAdapter->GetParent(__uuidof(IDXGIFactory2), (void **)&pFactory);

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = width;
    sd.Height = height;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    hr = pFactory->CreateSwapChainForHwnd(m_pDevice, m_hwnd, &sd, nullptr,
                                          nullptr, &m_pSwapChain);
    pFactory->Release();
    pAdapter->Release();
    pDXGIDevice->Release();
    if (FAILED(hr)) {
      return false;
    }

    // Pre-create textures for CPU upload
    D3D11_TEXTURE2D_DESC stagingDesc = {};
    stagingDesc.Width = width;
    stagingDesc.Height = height;
    stagingDesc.MipLevels = 1;
    stagingDesc.ArraySize = 1;
    stagingDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.Usage = D3D11_USAGE_DYNAMIC; // DYNAMIC for efficient CPU upload
    stagingDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE; // Required for DYNAMIC
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = m_pDevice->CreateTexture2D(&stagingDesc, nullptr, &m_pStagingTex);

    return true;
  }

  void RenderFrame(const BYTE *pSrcData, UINT srcRowPitch, UINT width,
                   UINT height) {
    if (!m_pSwapChain || !pSrcData || !m_pStagingTex)
      return;

    // 1. Map staging with DISCARD, memcpy
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr =
        m_pContext->Map(m_pStagingTex, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) {
      return;
    }

    const UINT rowBytes = width * 4;
    for (UINT row = 0; row < height; row++) {
      memcpy((BYTE *)mapped.pData + row * mapped.RowPitch,
             pSrcData + row * srcRowPitch, rowBytes);
    }
    m_pContext->Unmap(m_pStagingTex, 0);

    // 2. staging (DYNAMIC) → backbuffer
    ID3D11Texture2D *pBack = nullptr;
    hr = m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&pBack);
    if (SUCCEEDED(hr)) {
      m_pContext->CopyResource(pBack, m_pStagingTex); // Direct copy
      pBack->Release();
    }

    hr = m_pSwapChain->Present(0, 0);
    if (hr == DXGI_STATUS_OCCLUDED) {
      Sleep(33); // window hidden, throttle
    }
  }

  void Cleanup() {
    if (m_pStagingTex) {
      m_pStagingTex->Release();
      m_pStagingTex = nullptr;
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
  }

private:
  HWND m_hwnd;
  ID3D11Device *m_pDevice;
  ID3D11DeviceContext *m_pContext;
  IDXGISwapChain1 *m_pSwapChain;
  ID3D11Texture2D *m_pStagingTex;
};

// ── Globals
// ───────────────────────────────────────────────────────────────────
struct Instance {
  HWND hwnd = nullptr;
  VideoPlayer *player = nullptr;
  D3DRenderer *renderer = nullptr;
};

static Instance g_inst;
static std::atomic<bool> g_bRunning(true);
static HINSTANCE g_hInstance = nullptr;
static std::wstring g_videoPath;

static const char CLASS_NAME[] = "DynamicWallpaperClass";
#define WM_USER_INIT_VIDEO (WM_USER + 3)

// ── Desktop layer helper
// ──────────────────────────────────────────────────────
static HWND g_hwndDefView = nullptr;

static HWND GetDesktopLayer() {
  HWND progman = FindWindow("Progman", nullptr);
  if (!progman) {
    return nullptr;
  }

  DWORD_PTR result = 0;
  SendMessageTimeout(progman, 0x052C, 0x0D, 0x01, SMTO_NORMAL, 1000, &result);

  for (int i = 0; i < 20 && !g_hwndDefView; ++i) {
    g_hwndDefView = FindWindowEx(progman, NULL, "SHELLDLL_DefView", NULL);
    if (!g_hwndDefView) {
      EnumWindows(
          [](HWND h, LPARAM) -> BOOL {
            HWND p = FindWindowEx(h, NULL, "SHELLDLL_DefView", NULL);
            if (p) {
              g_hwndDefView = p;
              return FALSE;
            }
            return TRUE;
          },
          0);
    }
    if (!g_hwndDefView)
      Sleep(1000);
  }

  return progman;
}

// ── Render thread helpers ──────────────────────────────────────────────────

static void ProcessFrame() {
  const BYTE *pData = nullptr;
  UINT32 pitch = 0, w = 0, h = 0;
  if (g_inst.player &&
      g_inst.player->GetNextFrameCPU(&pData, &pitch, &w, &h) == S_OK && pData) {
    g_inst.renderer->RenderFrame(pData, pitch, w, h);
    g_inst.player->UnlockFrame();
  }
}

// QPC-paced at 60 fps. Never blocks on vsync.
static void RenderThreadLogic() {
  LARGE_INTEGER freq, last;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&last);
  const LONGLONG kTick = freq.QuadPart / 60;

  while (g_bRunning) {
    ProcessFrame();

    // Sleep until next 16.67ms boundary
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    LONGLONG rem = kTick - (now.QuadPart - last.QuadPart);
    if (rem > 0) {
      LONGLONG ms = rem * 1000 / freq.QuadPart;
      if (ms > 1)
        Sleep((DWORD)(ms - 1));
      do {
        QueryPerformanceCounter(&now);
      } while ((now.QuadPart - last.QuadPart) < kTick && g_bRunning);
    }
    last.QuadPart += kTick;
    QueryPerformanceCounter(&now);
    if (now.QuadPart - last.QuadPart > kTick * 4)
      last = now; // catch-up guard
  }
}

static void RenderThread() {
  unsigned int code = 0;
  __try {
    RenderThreadLogic();
  } __except (code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
  }
}

// ── Window proc
// ───────────────────────────────────────────────────────────────
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
  case WM_ERASEBKGND:
    return 1; // D3D handles background

  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProc(hwnd, msg, wp, lp);
}

// ── WinMain
// ───────────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
  SetProcessDPIAware();
  g_hInstance = hInst;

  if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED)))
    return -1;

  // --- Parse command line: first argument = video path ---
  {
    int argc;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc >= 2) {
      g_videoPath = argv[1];
    } else {
      // fallback: same folder as exe
      char exePath[MAX_PATH];
      GetModuleFileNameA(NULL, exePath, MAX_PATH);
      PathRemoveFileSpecA(exePath);
      PathRemoveFileSpecA(exePath);
      PathRemoveFileSpecA(exePath);
      std::string fb =
          std::string(exePath) + "\\pixel-rain-traffic.3840x2160.mp4";
      g_videoPath = std::wstring(fb.begin(), fb.end());
    }
    LocalFree(argv);
  }

  int W = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  int H = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  int X = GetSystemMetrics(SM_XVIRTUALSCREEN);
  int Y = GetSystemMetrics(SM_YVIRTUALSCREEN);

  // --- Register window class ---
  WNDCLASS wc = {};
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = hInst;
  wc.lpszClassName = CLASS_NAME;
  wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  RegisterClass(&wc);

  // --- Create wallpaper window behind desktop icons ---
  HWND progman = GetDesktopLayer();

  g_inst.hwnd = CreateWindowEx(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, CLASS_NAME,
                               "Dynamic Wallpaper", WS_POPUP | WS_VISIBLE, X, Y,
                               W, H, NULL, NULL, hInst, nullptr);

  if (!g_inst.hwnd) {
    CoUninitialize();
    return -1;
  }

  if (progman && g_hwndDefView) {
    SetParent(g_inst.hwnd, progman);
    SetWindowPos(g_inst.hwnd, g_hwndDefView, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  } else {
    SetWindowPos(g_inst.hwnd, HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }

  // --- Init D3D + VideoPlayer ---
  g_inst.renderer = new D3DRenderer(g_inst.hwnd);
  g_inst.player = new VideoPlayer();

  if (!g_inst.renderer->Initialize(W, H)) {
    CoUninitialize();
    return -1;
  }
  if (FAILED(g_inst.player->Initialize(g_inst.renderer->GetDevice()))) {
    CoUninitialize();
    return -1;
  }

  if (SUCCEEDED(g_inst.player->OpenFile(g_videoPath))) {
    g_inst.player->Play();
  } else {
    // Failed to open file
  }

  // --- Start render thread ---
  std::thread rt(RenderThread);

  // --- Message loop ---
  MSG msg = {};
  while (GetMessage(&msg, NULL, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  g_bRunning = false;
  rt.join();

  g_inst.player->Shutdown();
  delete g_inst.player;
  delete g_inst.renderer;

  CoUninitialize();
  return 0;
}