#pragma once
#include <evr.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <string>
#include <windows.h>


// VideoPlayer class encapsulates Windows Media Foundation (MF) to play video to
// a specific window.
class VideoPlayer : public IMFAsyncCallback {
public:
  VideoPlayer(HWND hwnd);
  virtual ~VideoPlayer();

  HRESULT Initialize();
  void Shutdown();

  HRESULT OpenFile(const std::wstring &path);
  HRESULT Play();
  HRESULT Pause();

  bool IsPlaying() const { return m_isPlaying; }

  HRESULT ResizeVideo(WORD width, WORD height);
  HRESULT
  SetVideoWindow(HWND hwnd); // 直接將 EVR 渲染導向指定視窗（WorkerW/Progman）

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID riid, void **ppv);
  STDMETHODIMP_(ULONG) AddRef();
  STDMETHODIMP_(ULONG) Release();

  // IMFAsyncCallback
  STDMETHODIMP GetParameters(DWORD *pdwFlags, DWORD *pdwQueue);
  STDMETHODIMP Invoke(IMFAsyncResult *pAsyncResult);

private:
  HRESULT CreateSession();
  HRESULT CloseSession();
  HRESULT CreateMediaSource(const std::wstring &url, IMFMediaSource **ppSource);
  HRESULT CreateTopology(IMFMediaSource *pSource, HWND hVideoWindow,
                         IMFTopology **ppTopology);
  HRESULT AddToTopology(IMFTopology *pTopology, IMFMediaSource *pSource,
                        DWORD streamIndex, HWND hwndVideo);
  HRESULT HandleEvent(IMFMediaEvent *pEvent);

  HWND m_hwndVideo;
  bool m_isPlaying;
  long m_nRefCount;

  IMFMediaSession *m_pSession;
  IMFMediaSource *m_pSource;
  IMFVideoDisplayControl *m_pVideoDisplay;
};
