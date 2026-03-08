#define _CRT_SECURE_NO_WARNINGS
// clang-format off
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
// clang-format on
#include <atomic>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "VideoPlayer.h"

void LogMsg(const std::string &msg) {
  printf("%s\n", msg.c_str());
  fflush(stdout);
  OutputDebugStringA((msg + "\n").c_str());
}

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "shlwapi.lib")

// ── D3DRenderer ──────────────────────────────────────────────────────────────
class D3DRenderer {
public:
  D3DRenderer(HWND hwnd)
      : m_hwnd(hwnd), m_pDevice(nullptr), m_pContext(nullptr),
        m_pSwapChain(nullptr), m_pVS(nullptr), m_pPS(nullptr),
        m_pSampler(nullptr) {}
  ~D3DRenderer() { Cleanup(); }

  ID3D11Device *GetDevice() const { return m_pDevice; }

  bool Initialize(int width, int height) {
    D3D_FEATURE_LEVEL featureLevels[] = {D3D_FEATURE_LEVEL_11_1,
                                         D3D_FEATURE_LEVEL_11_0};
    HRESULT hr =
        D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                          D3D11_CREATE_DEVICE_BGRA_SUPPORT, featureLevels, 2,
                          D3D11_SDK_VERSION, &m_pDevice, nullptr, &m_pContext);
    if (FAILED(hr))
      return false;

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
    if (FAILED(hr))
      return false;

    const char *shaderSrc = "struct VOut {\n"
                            "    float4 pos : SV_Position;\n"
                            "    float2 uv : TEXCOORD0;\n"
                            "};\n"
                            "VOut VS(uint id : SV_VertexID) {\n"
                            "    VOut output;\n"
                            "    output.uv = float2((id << 1) & 2, id & 2);\n"
                            "    output.pos = float4(output.uv * float2(2, -2) "
                            "+ float2(-1, 1), 0, 1);\n"
                            "    return output;\n"
                            "}\n"
                            "Texture2D tex : register(t0);\n"
                            "SamplerState sam : register(s0);\n"
                            "float4 PS(VOut input) : SV_Target {\n"
                            "    return tex.Sample(sam, input.uv);\n"
                            "}\n";

    ID3DBlob *vsBlob = nullptr, *psBlob = nullptr, *errBlob = nullptr;
    hr = D3DCompile(shaderSrc, strlen(shaderSrc), nullptr, nullptr, nullptr,
                    "VS", "vs_4_0", 0, 0, &vsBlob, &errBlob);
    if (FAILED(hr)) {
      if (errBlob)
        errBlob->Release();
      return false;
    }
    m_pDevice->CreateVertexShader(vsBlob->GetBufferPointer(),
                                  vsBlob->GetBufferSize(), nullptr, &m_pVS);
    vsBlob->Release();

    hr = D3DCompile(shaderSrc, strlen(shaderSrc), nullptr, nullptr, nullptr,
                    "PS", "ps_4_0", 0, 0, &psBlob, &errBlob);
    if (FAILED(hr)) {
      if (errBlob)
        errBlob->Release();
      return false;
    }
    m_pDevice->CreatePixelShader(psBlob->GetBufferPointer(),
                                 psBlob->GetBufferSize(), nullptr, &m_pPS);
    psBlob->Release();

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = sampDesc.AddressV = sampDesc.AddressW =
        D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    m_pDevice->CreateSamplerState(&sampDesc, &m_pSampler);

    m_viewport.Width = (FLOAT)width;
    m_viewport.Height = (FLOAT)height;
    m_viewport.MinDepth = 0.0f;
    m_viewport.MaxDepth = 1.0f;
    m_viewport.TopLeftX = m_viewport.TopLeftY = 0;

    return true;
  }

  void RenderFrame(ID3D11Texture2D *pTexture, UINT subresourceIndex) {
    if (!m_pSwapChain || !pTexture)
      return;
    ID3D11Texture2D *pBack = nullptr;
    if (FAILED(m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                       (void **)&pBack)))
      return;

    ID3D11RenderTargetView *pRTV = nullptr;
    m_pDevice->CreateRenderTargetView(pBack, nullptr, &pRTV);
    pBack->Release();

    D3D11_TEXTURE2D_DESC texDesc;
    pTexture->GetDesc(&texDesc);
    ID3D11ShaderResourceView *pSRV = nullptr;
    if (texDesc.ArraySize > 1) {
      D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
      srvDesc.Format = texDesc.Format;
      srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
      srvDesc.Texture2DArray.MostDetailedMip = 0;
      srvDesc.Texture2DArray.MipLevels = 1;
      srvDesc.Texture2DArray.FirstArraySlice = subresourceIndex;
      srvDesc.Texture2DArray.ArraySize = 1;
      m_pDevice->CreateShaderResourceView(pTexture, &srvDesc, &pSRV);
    } else {
      D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
      srvDesc.Format = texDesc.Format;
      srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      srvDesc.Texture2D.MostDetailedMip = 0;
      srvDesc.Texture2D.MipLevels = 1;
      m_pDevice->CreateShaderResourceView(pTexture, &srvDesc, &pSRV);
    }

    if (pSRV) {
      m_pContext->OMSetRenderTargets(1, &pRTV, nullptr);
      m_pContext->RSSetViewports(1, &m_viewport);
      m_pContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      m_pContext->VSSetShader(m_pVS, nullptr, 0);
      m_pContext->PSSetShader(m_pPS, nullptr, 0);
      m_pContext->PSSetShaderResources(0, 1, &pSRV);
      m_pContext->PSSetSamplers(0, 1, &m_pSampler);
      m_pContext->Draw(3, 0);
      ID3D11ShaderResourceView *nullSRV[1] = {nullptr};
      m_pContext->PSSetShaderResources(0, 1, nullSRV);
      m_pContext->OMSetRenderTargets(0, nullptr, nullptr);
      pSRV->Release();
    }
    pRTV->Release();
    m_pSwapChain->Present(1, 0);
  }

  void Cleanup() {
    if (m_pSampler) {
      m_pSampler->Release();
      m_pSampler = nullptr;
    }
    if (m_pPS) {
      m_pPS->Release();
      m_pPS = nullptr;
    }
    if (m_pVS) {
      m_pVS->Release();
      m_pVS = nullptr;
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
  ID3D11VertexShader *m_pVS;
  ID3D11PixelShader *m_pPS;
  ID3D11SamplerState *m_pSampler;
  D3D11_VIEWPORT m_viewport;
};

// ── Globals ──────────────────────────────────────────────────────────────────
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
static HWND g_hwndDefView = nullptr;

static HWND GetDesktopLayer() {
  HWND progman = FindWindow("Progman", nullptr);
  if (!progman)
    return nullptr;
  DWORD_PTR result = 0;
  SendMessageTimeout(progman, 0x052C, 0x0D, 0x01, SMTO_NORMAL, 1000, &result);
  for (int i = 0; i < 10 && !g_hwndDefView; ++i) {
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
      Sleep(500);
  }
  return progman;
}

static void ProcessFrame() {
  ID3D11Texture2D *pTexture = nullptr;
  UINT subresource = 0;
  if (g_inst.player) {
    g_inst.player->GetNextFrameGPU(&pTexture, &subresource);
    if (pTexture) {
      g_inst.renderer->RenderFrame(pTexture, subresource);
      pTexture->Release();
    }
  }
}

static void RenderThreadLogic() {
  LARGE_INTEGER freq, last, now;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&last);
  const LONGLONG kTick = freq.QuadPart / 60;
  while (g_bRunning) {
    // 計算距離下一次 deadline 還剩多少時間
    QueryPerformanceCounter(&now);
    LONGLONG rem = kTick - (now.QuadPart - last.QuadPart);
    if (rem > 0) {
      LONGLONG ms = rem * 1000 / freq.QuadPart;
      if (ms > 1)
        Sleep((DWORD)(ms - 1));
      // spin-wait 剩下的零頭
      do {
        QueryPerformanceCounter(&now);
      } while ((now.QuadPart - last.QuadPart) < kTick && g_bRunning);
    }
    // 更新 deadline
    last.QuadPart += kTick;
    // 如果落後超過 4 幀（例如系統繁忙），直接重置避免補幀風暴
    QueryPerformanceCounter(&now);
    if (now.QuadPart - last.QuadPart > kTick * 4)
      last = now;
    // 在等待結束後才執行這一幀，確保時序正確
    ProcessFrame();
  }
}

static void RenderThread() {
  unsigned int code = 0;
  __try {
    RenderThreadLogic();
  } __except (code = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
  }
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_ERASEBKGND)
    return 1;
  if (msg == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProc(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
  SetProcessDPIAware();
  g_hInstance = hInst;

  if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED)))
    return -1;
  {
    int argc;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc >= 2) {
      g_videoPath = argv[1];
    } else {
      // 如果沒有參數，嘗試在同目錄找預設影片
      char exeDir[MAX_PATH];
      GetModuleFileNameA(NULL, exeDir, MAX_PATH);
      PathRemoveFileSpecA(exeDir);
      std::string fb =
          std::string(exeDir) + "\\pixel-rain-traffic.3840x2160.mp4";
      g_videoPath = std::wstring(fb.begin(), fb.end());
    }
    LocalFree(argv);
  }

  int W = GetSystemMetrics(SM_CXVIRTUALSCREEN),
      H = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  int X = GetSystemMetrics(SM_XVIRTUALSCREEN),
      Y = GetSystemMetrics(SM_YVIRTUALSCREEN);

  WNDCLASS wc = {};
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = hInst;
  wc.lpszClassName = CLASS_NAME;
  wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
  RegisterClass(&wc);

  try {
    HWND progman = GetDesktopLayer();
    g_inst.hwnd = CreateWindowEx(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, CLASS_NAME, "Dynamic Wallpaper",
        WS_POPUP | WS_VISIBLE, X, Y, W, H, NULL, NULL, hInst, nullptr);

    if (!g_inst.hwnd) {
      CoUninitialize();
      return -1;
    }

    // 注入桌面層
    if (progman && g_hwndDefView) {
      SetParent(g_inst.hwnd, progman);
      SetWindowPos(g_inst.hwnd, g_hwndDefView, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else {
      SetWindowPos(g_inst.hwnd, HWND_BOTTOM, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    g_inst.renderer = new D3DRenderer(g_inst.hwnd);
    g_inst.player = new VideoPlayer();
    if (!g_inst.renderer->Initialize(W, H) ||
        FAILED(g_inst.player->Initialize(g_inst.renderer->GetDevice()))) {
      CoUninitialize();
      return -1;
    }
    if (SUCCEEDED(g_inst.player->OpenFile(g_videoPath)))
      g_inst.player->Play();

    std::thread rt(RenderThread);
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
  } catch (const std::exception &e) {
    LogMsg(std::string("CRASH: std::exception caught: ") + e.what());
    return -1;
  } catch (...) {
    LogMsg("CRASH: Unknown C++ exception caught.");
    return -1;
  }
}