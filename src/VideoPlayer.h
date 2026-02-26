#pragma once
#include <atomic>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mutex>
#include <queue>
#include <string>
#include <windows.h>

class VideoPlayer {
public:
  VideoPlayer();
  ~VideoPlayer();

  HRESULT Initialize(ID3D11Device *pDevice);
  void Shutdown();

  HRESULT OpenFile(const std::wstring &path);
  void Play();
  void Pause();

  bool IsPlaying() const { return m_isPlaying; }

  // Retrieves the next video frame if it's time to display it.
  // Returns S_OK and sets pData if a new frame is ready.
  // Returns S_FALSE if there's no new frame yet (wait for next tick).
  HRESULT GetNextFrame(ID3D11Texture2D **ppTexture, UINT32 *pdwWidth,
                       UINT32 *pdwHeight);

  // Call after copying data from GetNextFrame
  void UnlockFrame();

private:
  HRESULT Rewind();
  void RequestSample();

  class SourceReaderCallback : public IMFSourceReaderCallback {
  public:
    SourceReaderCallback(VideoPlayer *player)
        : m_player(player), m_refCount(1) {}
    virtual ~SourceReaderCallback() {}

    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override {
      if (!ppv)
        return E_POINTER;
      if (riid == IID_IUnknown || riid == IID_IMFSourceReaderCallback) {
        *ppv = static_cast<IMFSourceReaderCallback *>(this);
        AddRef();
        return S_OK;
      }
      *ppv = NULL;
      return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override {
      return InterlockedIncrement(&m_refCount);
    }
    STDMETHODIMP_(ULONG) Release() override {
      ULONG uCount = InterlockedDecrement(&m_refCount);
      if (uCount == 0)
        delete this;
      return uCount;
    }

    STDMETHODIMP OnReadSample(HRESULT hrStatus, DWORD dwStreamIndex,
                              DWORD dwStreamFlags, LONGLONG llTimestamp,
                              IMFSample *pSample) override {
      return m_player->OnReadSample(hrStatus, dwStreamIndex, dwStreamFlags,
                                    llTimestamp, pSample);
    }
    STDMETHODIMP OnFlush(DWORD dwStreamIndex) override { return S_OK; }
    STDMETHODIMP OnEvent(DWORD dwStreamIndex, IMFMediaEvent *pEvent) override {
      return S_OK;
    }

  private:
    VideoPlayer *m_player;
    long m_refCount;
  };

  friend class SourceReaderCallback;

  HRESULT OnReadSample(HRESULT hrStatus, DWORD dwStreamIndex,
                       DWORD dwStreamFlags, LONGLONG llTimestamp,
                       IMFSample *pSample);

  IMFSourceReader *m_pReader;
  bool m_isPlaying;

  UINT32 m_width;
  UINT32 m_height;
  LONGLONG m_hnsDuration;

  // Clock sync
  LARGE_INTEGER m_qpcFrequency;
  LARGE_INTEGER m_qpcStart;
  LONGLONG m_playbackStartHns; // Video time when playback started

  // Current sample
  IMFSample *m_pCurrentSample;
  LONGLONG m_currentSamplePts;

  IMFDXGIDeviceManager *m_pDeviceManager;
  UINT m_resetToken;

  IMFSourceReaderCallback *m_pCallback;
  std::queue<IMFSample *> m_sampleQueue;
  int m_pendingReads;
  const int MAX_BUFFER = 4;

  std::mutex m_mutex;
};
