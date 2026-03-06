#define _CRT_SECURE_NO_WARNINGS
#include "VideoPlayer.h"
#include <propvarutil.h>
#include <shlwapi.h>

extern void LogMsg(const std::string &msg);

#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "Propsys.lib")

static const GUID IID_ID3D11Multithread_Local = {
    0x9B7E4E00,
    0x342C,
    0x4106,
    {0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89, 0xF0}};
struct ID3D11Multithread_Local : public IUnknown {
  virtual VOID STDMETHODCALLTYPE Enter() = 0;
  virtual VOID STDMETHODCALLTYPE Leave() = 0;
  virtual BOOL STDMETHODCALLTYPE SetMultithreadProtected(BOOL bMTProtect) = 0;
  virtual BOOL STDMETHODCALLTYPE GetMultithreadProtected() = 0;
};

template <class T> void SafeRelease(T **ppT) {
  if (*ppT) {
    (*ppT)->Release();
    *ppT = NULL;
  }
}

STDMETHODIMP VideoPlayer::SourceReaderCallback::OnReadSample(
    HRESULT hrStatus, DWORD dwStreamIndex, DWORD dwStreamFlags,
    LONGLONG llTimestamp, IMFSample *pSample) {
  return m_player->OnReadSample(hrStatus, dwStreamIndex, dwStreamFlags,
                                llTimestamp, pSample);
}

VideoPlayer::VideoPlayer()
    : m_pReader(NULL), m_isPlaying(false), m_width(0), m_height(0),
      m_playbackStartHns(0), m_pCurrentSample(NULL), m_pLockedBuffer(NULL),
      m_currentSamplePts(0), m_pDevice(NULL), m_pContext(NULL),
      m_pDeviceManager(NULL), m_resetToken(0), m_pendingReads(0),
      m_pCurrentGPUTexture(NULL), m_currentSubresourceIndex(0),
      m_pStagingTexture(NULL), m_firstFrameQpc(0), m_firstFramePts(0),
      m_hnsFrameDuration(333333) {
  m_pendingRewind = false;
  m_qpcStart = 0;
  m_pCallback = new SourceReaderCallback(this);
  timeBeginPeriod(1);
}

VideoPlayer::~VideoPlayer() {
  timeEndPeriod(1);
  Shutdown();
}

HRESULT VideoPlayer::Initialize(ID3D11Device *pDevice) {
  HRESULT hr = MFStartup(MF_VERSION);
  if (FAILED(hr))
    return hr;
  m_pDevice = pDevice;
  m_pDevice->AddRef();
  m_pDevice->GetImmediateContext(&m_pContext);
  ID3D11Multithread_Local *pMT = nullptr;
  if (SUCCEEDED(pDevice->QueryInterface(IID_ID3D11Multithread_Local,
                                        (void **)&pMT))) {
    pMT->SetMultithreadProtected(TRUE);
    pMT->Release();
  }
  hr = MFCreateDXGIDeviceManager(&m_resetToken, &m_pDeviceManager);
  if (FAILED(hr))
    return hr;
  return m_pDeviceManager->ResetDevice(pDevice, m_resetToken);
}

void VideoPlayer::Shutdown() {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  SafeRelease(&m_pCurrentSample);
  SafeRelease(&m_pCurrentGPUTexture);
  SafeRelease(&m_pStagingTexture); // release our owned staging texture
  if (m_pLockedBuffer) {
    m_pLockedBuffer->Unlock();
    SafeRelease(&m_pLockedBuffer);
  }
  while (!m_sampleQueue.empty()) {
    IMFSample *s = m_sampleQueue.front();
    SafeRelease(&s);
    m_sampleQueue.pop();
  }
  SafeRelease(&m_pReader);
  SafeRelease(&m_pDeviceManager);
  SafeRelease(&m_pCallback);
  SafeRelease(&m_pContext);
  SafeRelease(&m_pDevice);
  MFShutdown();
}

HRESULT VideoPlayer::OpenFile(const std::wstring &path) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  SafeRelease(&m_pCurrentSample);
  while (!m_sampleQueue.empty()) {
    IMFSample *s = m_sampleQueue.front();
    SafeRelease(&s);
    m_sampleQueue.pop();
  }
  m_pendingReads = 0;
  SafeRelease(&m_pReader);
  IMFAttributes *pAttributes = NULL;
  MFCreateAttributes(&pAttributes, 3);
  pAttributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, m_pDeviceManager);
  pAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING,
                         TRUE);
  pAttributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, m_pCallback);
  HRESULT hr =
      MFCreateSourceReaderFromURL(path.c_str(), pAttributes, &m_pReader);
  SafeRelease(&pAttributes);
  if (FAILED(hr))
    return hr;
  IMFMediaType *pMediaType = NULL;
  MFCreateMediaType(&pMediaType);
  pMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  pMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
  m_pReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL,
                                 pMediaType);
  SafeRelease(&pMediaType);
  IMFMediaType *pCurrentType = NULL;
  if (SUCCEEDED(m_pReader->GetCurrentMediaType(
          MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pCurrentType))) {
    MFGetAttributeSize(pCurrentType, MF_MT_FRAME_SIZE, &m_width, &m_height);
    UINT32 num = 0, den = 1;
    if (SUCCEEDED(
            MFGetAttributeRatio(pCurrentType, MF_MT_FRAME_RATE, &num, &den)) &&
        num > 0)
      m_hnsFrameDuration = 10000000LL * den / num;
    SafeRelease(&pCurrentType);
  }
  return S_OK;
}

void VideoPlayer::Play() {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_isPlaying && m_pReader) {
    m_isPlaying = true;
    m_firstFrameQpc = 0; // <-- Add this line
    m_firstFramePts = 0; // <-- Add this line
    RequestSample();
    RequestSample();
  }
}

void VideoPlayer::Pause() {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  m_isPlaying = false;
}

void VideoPlayer::Rewind() {
  if (!m_pReader)
    return;
  while (!m_sampleQueue.empty()) {
    IMFSample *s = m_sampleQueue.front();
    SafeRelease(&s);
    m_sampleQueue.pop();
  }
  PROPVARIANT var;
  PropVariantInit(&var);
  var.vt = VT_I8;
  var.hVal.QuadPart = 0;
  m_pReader->SetCurrentPosition(GUID_NULL, var);
  PropVariantClear(&var);
  SafeRelease(&m_pCurrentSample);
  SafeRelease(&m_pCurrentGPUTexture);
  m_firstFrameQpc = 0;
  m_firstFramePts = 0;
  m_currentSamplePts = 0;
  RequestSample();
}

void VideoPlayer::RequestSample() {
  if (!m_isPlaying || !m_pReader)
    return;
  while ((m_sampleQueue.size() + m_pendingReads) < 10) {
    if (SUCCEEDED(m_pReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                                        NULL, NULL, NULL, NULL)))
      m_pendingReads++;
    else
      break;
  }
}

HRESULT VideoPlayer::OnReadSample(HRESULT hrStatus, DWORD dwStreamIndex,
                                  DWORD dwStreamFlags, LONGLONG llTimestamp,
                                  IMFSample *pSample) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (m_pendingReads > 0)
    m_pendingReads--;
  if (FAILED(hrStatus))
    return S_OK;
  if (dwStreamFlags & MF_SOURCE_READERF_ENDOFSTREAM) {
    m_pendingRewind = true;
    return S_OK;
  }
  if (pSample) {
    pSample->AddRef();
    m_sampleQueue.push(pSample);
    RequestSample();
  }
  return S_OK;
}

HRESULT VideoPlayer::GetNextFrameGPU(ID3D11Texture2D **ppTexture,
                                     UINT *pSubresourceIndex) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (m_pendingRewind.exchange(false))
    Rewind();

  if (m_isPlaying && m_pReader) {
    LARGE_INTEGER qpcNow, qpcFreq;
    QueryPerformanceFrequency(&qpcFreq);
    int framesPopped = 0;
    while (!m_sampleQueue.empty()) {
      QueryPerformanceCounter(&qpcNow);
      IMFSample *pPeek = m_sampleQueue.front();
      LONGLONG samplePts = 0;
      pPeek->GetSampleTime(&samplePts);
      if (m_firstFrameQpc == 0) {
        m_firstFrameQpc = qpcNow.QuadPart;
        m_firstFramePts = samplePts;
      }
      LONGLONG elapsedHns =
          (qpcNow.QuadPart - m_firstFrameQpc) * 10000000LL / qpcFreq.QuadPart;
      LONGLONG relativePts = samplePts - m_firstFramePts;

      if (elapsedHns - relativePts > m_hnsFrameDuration * 2) {
        m_firstFrameQpc = qpcNow.QuadPart;
        m_firstFramePts = samplePts;
        elapsedHns = 0;
        relativePts = 0;
      }

      if (relativePts > elapsedHns)
        break;

      framesPopped++;
      IMFSample *pChosenSample = m_sampleQueue.front();
      m_sampleQueue.pop();
      m_currentSamplePts = samplePts;

      IMFMediaBuffer *pBuffer = nullptr;
      if (SUCCEEDED(pChosenSample->GetBufferByIndex(0, &pBuffer))) {
        IMFDXGIBuffer *pDXGIBuffer = nullptr;
        if (SUCCEEDED(pBuffer->QueryInterface(__uuidof(IMFDXGIBuffer),
                                              (void **)&pDXGIBuffer))) {
          ID3D11Texture2D *pMFTex = nullptr;
          UINT subres = 0;
          pDXGIBuffer->GetSubresourceIndex(&subres);
          if (SUCCEEDED(pDXGIBuffer->GetResource(__uuidof(ID3D11Texture2D),
                                                 (void **)&pMFTex)) &&
              pMFTex) {
            D3D11_TEXTURE2D_DESC mfDesc;
            pMFTex->GetDesc(&mfDesc);

            // (Re)create our staging texture if size or format changed
            if (!m_pStagingTexture) {
              D3D11_TEXTURE2D_DESC stagDesc = {};
              stagDesc.Width = mfDesc.Width;
              stagDesc.Height = mfDesc.Height;
              stagDesc.MipLevels = 1;
              stagDesc.ArraySize = 1;
              stagDesc.Format = mfDesc.Format;
              stagDesc.SampleDesc.Count = 1;
              stagDesc.Usage = D3D11_USAGE_DEFAULT;
              stagDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
              m_pDevice->CreateTexture2D(&stagDesc, nullptr,
                                         &m_pStagingTexture);
            }

            if (m_pStagingTexture) {
              // Blit this slice into our owned texture (subresource 0)
              UINT srcSub = D3D11CalcSubresource(0, subres, mfDesc.MipLevels);
              m_pContext->CopySubresourceRegion(
                  m_pStagingTexture, 0, // dst: our texture, slice 0
                  0, 0, 0,              // dst offset
                  pMFTex, srcSub,       // src: MF pool texture + slice
                  nullptr);             // full region
            }
            pMFTex->Release(); // release MF's texture immediately
          }
          pDXGIBuffer->Release();
        }
        pBuffer->Release();
      }
      pChosenSample->Release();
      RequestSample();
    }
  }

  // Return our owned staging texture (subresource 0), not MF's shared pool
  // texture. m_pCurrentGPUTexture is no longer stored long-term; staging
  // texture holds the frame.
  if (m_pStagingTexture) {
    m_pStagingTexture->AddRef();
    *ppTexture = m_pStagingTexture;
    *pSubresourceIndex = 0;
    return S_OK;
  }
  return S_FALSE;
}
