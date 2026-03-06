#pragma once
// Require Windows 8+ API surface for MF D3D11 integration.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x06020000 // NTDDI_WIN8
#endif

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
#include <vector>
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
  void Rewind();

  bool IsPlaying() const { return m_isPlaying; }

  // Retrieves the next video frame as a GPU texture.
  // ppTexture is AddRef'd - caller must Release it.
  // Returns S_OK for new frame, S_FALSE if no new frame yet (or not playing).
  HRESULT GetNextFrameGPU(ID3D11Texture2D **ppTexture, UINT *pSubresourceIndex);

private:
  void RequestSample();

  // The callback class that Media Foundation uses to deliver samples.
  class SourceReaderCallback : public IMFSourceReaderCallback {
  public:
    SourceReaderCallback(VideoPlayer *player)
        : m_player(player), m_refCount(1) {}
    virtual ~SourceReaderCallback() {}

    // IUnknown methods
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

    // IMFSourceReaderCallback methods
    STDMETHODIMP OnReadSample(HRESULT hrStatus, DWORD dwStreamIndex,
                              DWORD dwStreamFlags, LONGLONG llTimestamp,
                              IMFSample *pSample) override;
    STDMETHODIMP OnFlush(DWORD) override { return S_OK; }
    STDMETHODIMP OnEvent(DWORD, IMFMediaEvent *) override { return S_OK; }

  private:
    VideoPlayer *m_player;
    long m_refCount;
  };

  friend class SourceReaderCallback;

  HRESULT OnReadSample(HRESULT hrStatus, DWORD dwStreamIndex,
                       DWORD dwStreamFlags, LONGLONG llTimestamp,
                       IMFSample *pSample);

  std::recursive_mutex m_mutex;

  // We keep these for initializing the reader, but won't use context during
  // frame retrieval.
  ID3D11Device *m_pDevice;
  ID3D11DeviceContext *m_pContext;

  IMFDXGIDeviceManager *m_pDeviceManager;
  UINT m_resetToken;

  IMFSourceReader *m_pReader;
  SourceReaderCallback *m_pCallback;

  IMFSample *m_pCurrentSample;
  IMFMediaBuffer *m_pLockedBuffer;

  std::queue<IMFSample *> m_sampleQueue;

  bool m_isPlaying;
  UINT32 m_width;
  UINT32 m_height;

  // Synchronization and timing
  std::atomic<bool> m_pendingRewind;
  std::atomic<int> m_pendingReads;

  LONGLONG m_qpcStart;
  LONGLONG m_currentSamplePts;
  LONGLONG m_playbackStartHns;

  // Our OWN copy of the current frame, blit from MF's shared pool texture.
  // This prevents MF from overwriting the frame we're currently displaying.
  ID3D11Texture2D
      *m_pStagingTexture; // owned, single-slice, always subresource 0
  ID3D11Texture2D
      *m_pCurrentGPUTexture; // reference into MF pool (kept only during blit)
  UINT m_currentSubresourceIndex;

  // First-frame anchor for robust PTS-based frame pacing
  LONGLONG m_firstFrameQpc; // wall clock when first frame was displayed
  LONGLONG m_firstFramePts; // PTS of that first frame
  LONGLONG m_hnsFrameDuration;
};
