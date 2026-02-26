#include "VideoPlayer.h"
#include <propvarutil.h>
#include <shlwapi.h>

extern void LogMsg(const std::string &msg);

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "Propsys.lib")

template <class T> void SafeRelease(T **ppT) {
  if (*ppT) {
    (*ppT)->Release();
    *ppT = NULL;
  }
}

VideoPlayer::VideoPlayer()
    : m_pReader(NULL), m_isPlaying(false), m_width(0), m_height(0),
      m_hnsDuration(0), m_playbackStartHns(0), m_pCurrentSample(NULL),
      m_currentSamplePts(0), m_pDeviceManager(NULL), m_resetToken(0),
      m_pendingReads(0) {
  QueryPerformanceFrequency(&m_qpcFrequency);
  m_qpcStart.QuadPart = 0;
  m_pCallback = new SourceReaderCallback(this);
}

VideoPlayer::~VideoPlayer() { Shutdown(); }

HRESULT VideoPlayer::Initialize(ID3D11Device *pDevice) {
  HRESULT hr = MFStartup(MF_VERSION);
  if (FAILED(hr))
    return hr;

  hr = MFCreateDXGIDeviceManager(&m_resetToken, &m_pDeviceManager);
  if (FAILED(hr))
    return hr;

  hr = m_pDeviceManager->ResetDevice(pDevice, m_resetToken);
  return hr;
}

void VideoPlayer::Shutdown() {
  std::lock_guard<std::mutex> lock(m_mutex);
  SafeRelease(&m_pCurrentSample);
  while (!m_sampleQueue.empty()) {
    IMFSample *s = m_sampleQueue.front();
    SafeRelease(&s);
    m_sampleQueue.pop();
  }
  SafeRelease(&m_pReader);
  SafeRelease(&m_pDeviceManager);
  SafeRelease(&m_pCallback);
  MFShutdown();
}

HRESULT VideoPlayer::OpenFile(const std::wstring &path) {
  std::lock_guard<std::mutex> lock(m_mutex);

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

  hr = pAttributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, m_pDeviceManager);
  hr = pAttributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
  hr = pAttributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, m_pCallback);

  hr = MFCreateSourceReaderFromURL(path.c_str(), pAttributes, &m_pReader);
  SafeRelease(&pAttributes);

  if (FAILED(hr))
    return hr;

  IMFMediaType *pMediaType = NULL;
  hr = MFCreateMediaType(&pMediaType);
  if (SUCCEEDED(hr)) {
    pMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    pMediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    hr = m_pReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                        NULL, pMediaType);
    SafeRelease(&pMediaType);
  }

  if (FAILED(hr))
    return hr;

  IMFMediaType *pCurrentType = NULL;
  hr = m_pReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                      &pCurrentType);
  if (SUCCEEDED(hr)) {
    MFGetAttributeSize(pCurrentType, MF_MT_FRAME_SIZE, &m_width, &m_height);
    SafeRelease(&pCurrentType);
  }

  PROPVARIANT varDuration;
  PropVariantInit(&varDuration);
  hr = m_pReader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE,
                                           MF_PD_DURATION, &varDuration);
  if (SUCCEEDED(hr)) {
    hr = PropVariantToInt64(varDuration, &m_hnsDuration);
    PropVariantClear(&varDuration);
  }

  m_isPlaying = false;
  return S_OK;
}

void VideoPlayer::RequestSample() {
  if (!m_isPlaying || !m_pReader)
    return;
  while ((m_sampleQueue.size() + m_pendingReads) < MAX_BUFFER) {
    HRESULT hr = m_pReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0,
                                       NULL, NULL, NULL, NULL);
    if (SUCCEEDED(hr)) {
      m_pendingReads++;
    } else {
      break;
    }
  }
}

void VideoPlayer::Play() {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_isPlaying && m_pReader) {
    m_isPlaying = true;
    QueryPerformanceCounter(&m_qpcStart);
    m_playbackStartHns = m_currentSamplePts;
    RequestSample();
  }
}

void VideoPlayer::Pause() {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_isPlaying = false;
}

HRESULT VideoPlayer::Rewind() {
  if (!m_pReader)
    return E_UNEXPECTED;

  while (!m_sampleQueue.empty()) {
    IMFSample *s = m_sampleQueue.front();
    SafeRelease(&s);
    m_sampleQueue.pop();
  }

  PROPVARIANT var;
  PropVariantInit(&var);
  var.vt = VT_I8;
  var.hVal.QuadPart = 0;

  HRESULT hr = m_pReader->SetCurrentPosition(GUID_NULL, var);
  PropVariantClear(&var);

  if (SUCCEEDED(hr)) {
    SafeRelease(&m_pCurrentSample);

    if (m_isPlaying) {
      QueryPerformanceCounter(&m_qpcStart);
    }
    m_playbackStartHns = 0;
    m_currentSamplePts = 0;

    RequestSample();
  }
  return hr;
}

HRESULT VideoPlayer::OnReadSample(HRESULT hrStatus, DWORD dwStreamIndex,
                                  DWORD dwStreamFlags, LONGLONG llTimestamp,
                                  IMFSample *pSample) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_pendingReads > 0)
    m_pendingReads--;

  if (FAILED(hrStatus)) {
    return S_OK;
  }

  if (dwStreamFlags & MF_SOURCE_READERF_ENDOFSTREAM) {
    Rewind();
    return S_OK;
  }

  if (pSample) {
    pSample->AddRef();
    m_sampleQueue.push(pSample);
    RequestSample();
  }

  return S_OK;
}

HRESULT VideoPlayer::GetNextFrame(ID3D11Texture2D **ppTexture, UINT32 *pdwWidth,
                                  UINT32 *pdwHeight) {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (!m_isPlaying || !m_pReader || m_sampleQueue.empty()) {
    return S_FALSE;
  }

  LARGE_INTEGER qpcNow;
  QueryPerformanceCounter(&qpcNow);
  LONGLONG elapsedHns = (qpcNow.QuadPart - m_qpcStart.QuadPart) * 10000000 /
                        m_qpcFrequency.QuadPart;
  LONGLONG currentVideoTime = m_playbackStartHns + elapsedHns;

  IMFSample *pFrontSample = m_sampleQueue.front();
  LONGLONG sampleTime = 0;
  pFrontSample->GetSampleTime(&sampleTime);

  if (sampleTime <= currentVideoTime) {
    SafeRelease(&m_pCurrentSample);
    m_pCurrentSample = pFrontSample;
    m_sampleQueue.pop();

    m_currentSamplePts = sampleTime;

    RequestSample();
    IMFMediaBuffer *pBuffer = NULL;
    HRESULT hr = m_pCurrentSample->GetBufferByIndex(0, &pBuffer);
    if (SUCCEEDED(hr)) {
      IMFDXGIBuffer *pDXGIBuffer = NULL;
      hr = pBuffer->QueryInterface(__uuidof(IMFDXGIBuffer),
                                   (void **)&pDXGIBuffer);
      if (SUCCEEDED(hr)) {
        hr = pDXGIBuffer->GetResource(__uuidof(ID3D11Texture2D),
                                      (void **)ppTexture);
        pDXGIBuffer->Release();
      }
      pBuffer->Release();
    }

    if (SUCCEEDED(hr)) {
      *pdwWidth = m_width;
      *pdwHeight = m_height;
    }
    return hr;
  } else {
    return S_FALSE;
  }
}

void VideoPlayer::UnlockFrame() {
  std::lock_guard<std::mutex> lock(m_mutex);
  SafeRelease(&m_pCurrentSample);
}
