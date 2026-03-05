#include "VideoPlayer.h"
#include <propvarutil.h>
#include <shlwapi.h>

#include <timeapi.h>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "Propsys.lib")

// IID: {9B7E4E00-342C-4106-A19F-4F2704F689F0}
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

// ─────────────────────────────────────────────────────────────────────────────
// SourceReaderCallback Implementation
// ─────────────────────────────────────────────────────────────────────────────

STDMETHODIMP VideoPlayer::SourceReaderCallback::OnReadSample(
    HRESULT hrStatus, DWORD dwStreamIndex, DWORD dwStreamFlags,
    LONGLONG llTimestamp, IMFSample *pSample) {
  return m_player->OnReadSample(hrStatus, dwStreamIndex, dwStreamFlags,
                                llTimestamp, pSample);
}

// ─────────────────────────────────────────────────────────────────────────────
// VideoPlayer Implementation
// ─────────────────────────────────────────────────────────────────────────────

VideoPlayer::VideoPlayer()
    : m_pReader(NULL), m_isPlaying(false), m_width(0), m_height(0),
      m_playbackStartHns(0), m_pCurrentSample(NULL), m_pLockedBuffer(NULL),
      m_currentSamplePts(0), m_pDevice(NULL), m_pContext(NULL),
      m_pDeviceManager(NULL), m_resetToken(0), m_pendingReads(0) {
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
  if (FAILED(hr)) {
    return hr;
  }

  m_pDevice = pDevice;
  m_pDevice->AddRef();
  m_pDevice->GetImmediateContext(&m_pContext);

  // Enable D3D11 multithread protection
  ID3D11Multithread_Local *pMT = nullptr;
  if (SUCCEEDED(pDevice->QueryInterface(IID_ID3D11Multithread_Local,
                                        (void **)&pMT))) {
    pMT->SetMultithreadProtected(TRUE);
    pMT->Release();
  }

  // Create DXGI device manager so MF can initialize its pipeline
  hr = MFCreateDXGIDeviceManager(&m_resetToken, &m_pDeviceManager);
  if (FAILED(hr)) {
    return hr;
  }

  hr = m_pDeviceManager->ResetDevice(pDevice, m_resetToken);
  if (FAILED(hr)) {
    return hr;
  }

  return S_OK;
}

void VideoPlayer::Shutdown() {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  SafeRelease(&m_pCurrentSample);
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
  HRESULT hr = MFCreateAttributes(&pAttributes, 3);
  if (FAILED(hr))
    return hr;

  // Provide D3D manager so MF can run the HW decoder pipeline
  pAttributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, m_pDeviceManager);
  // Advanced video processing lets MF pick HW decoder + colour converter.
  // Do NOT combine with MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS.
  pAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING,
                         TRUE);
  pAttributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, m_pCallback);

  hr = MFCreateSourceReaderFromURL(path.c_str(), pAttributes, &m_pReader);
  SafeRelease(&pAttributes);
  if (FAILED(hr)) {
    return hr;
  }

  // Request BGRA (RGB32) output — MF will colour-convert for us
  IMFMediaType *pMediaType = NULL;
  hr = MFCreateMediaType(&pMediaType);
  if (SUCCEEDED(hr)) {
    pMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    pMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    hr = m_pReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                        NULL, pMediaType);
    SafeRelease(&pMediaType);
    if (FAILED(hr)) {
      return hr;
    }
  }

  IMFMediaType *pCurrentType = NULL;
  hr = m_pReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                      &pCurrentType);
  if (SUCCEEDED(hr)) {
    MFGetAttributeSize(pCurrentType, MF_MT_FRAME_SIZE, &m_width, &m_height);
    SafeRelease(&pCurrentType);
  }

  m_isPlaying = false;
  return S_OK;
}

void VideoPlayer::Play() {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);
  if (!m_isPlaying && m_pReader) {
    m_isPlaying = true;
    QueryPerformanceCounter((LARGE_INTEGER *)&m_qpcStart);
    m_playbackStartHns = m_currentSamplePts;
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
  if (m_isPlaying) {
    QueryPerformanceCounter((LARGE_INTEGER *)&m_qpcStart);
  }
  m_playbackStartHns = 0;
  m_currentSamplePts = 0;
  RequestSample();
}

void VideoPlayer::RequestSample() {
  if (!m_isPlaying || !m_pReader)
    return;
  const int MAX_BUFFER = 10;
  while ((m_sampleQueue.size() + m_pendingReads) < MAX_BUFFER) {
    if (SUCCEEDED(m_pReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                                        NULL, NULL, NULL, NULL))) {
      m_pendingReads++;
    } else {
      break;
    }
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

HRESULT VideoPlayer::GetNextFrameCPU(const BYTE **ppData, UINT32 *pRowPitch,
                                     UINT32 *pdwWidth, UINT32 *pdwHeight) {
  std::lock_guard<std::recursive_mutex> lock(m_mutex);

  static bool s_logged = false;
  if (!s_logged) {
    s_logged = true;
  }

  if (m_pendingRewind.exchange(false)) {
    Rewind();
    return S_FALSE;
  }

  if (!m_isPlaying || !m_pReader || m_sampleQueue.empty()) {
    return S_FALSE;
  }

  IMFSample *pChosenSample = m_sampleQueue.front();
  m_sampleQueue.pop();

  if (!pChosenSample) {
    return S_FALSE;
  }

  SafeRelease(&m_pCurrentSample);
  m_pCurrentSample = pChosenSample;
  m_currentSamplePts = 0;
  m_pCurrentSample->GetSampleTime(&m_currentSamplePts);

  RequestSample();

  IMFMediaBuffer *pBuffer = nullptr;
  HRESULT hr = m_pCurrentSample->ConvertToContiguousBuffer(&pBuffer);
  if (FAILED(hr))
    return hr;

  BYTE *pSrc = nullptr;
  DWORD cbMaxLength = 0, cbCurrentLength = 0;
  hr = pBuffer->Lock(&pSrc, &cbMaxLength, &cbCurrentLength);
  if (FAILED(hr)) {
    pBuffer->Release();
    return hr;
  }

  LONG pitch = (LONG)(m_width * 4);
  MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, m_width, &pitch);
  UINT absPitch = (UINT)(pitch < 0 ? -pitch : pitch);

  // 複製到內部 buffer
  size_t needed = (size_t)absPitch * m_height;
  if (m_frameCopyBuf.size() < needed)
    m_frameCopyBuf.resize(needed);

  // Safeguard against over-reading the source buffer
  size_t copySize = (cbCurrentLength < needed) ? cbCurrentLength : needed;
  memcpy(m_frameCopyBuf.data(), pSrc, copySize);

  // 立刻 unlock，不再需要 MF buffer
  pBuffer->Unlock();
  pBuffer->Release();
  SafeRelease(&m_pCurrentSample); // sample 也可以立刻放掉

  *ppData = m_frameCopyBuf.data();
  *pRowPitch = absPitch;
  *pdwWidth = m_width;
  *pdwHeight = m_height;

  return S_OK;
}

void VideoPlayer::UnlockFrame() {
  // buffer 已在 GetNextFrameCPU 裡 unlock，這裡什麼都不用做
}
