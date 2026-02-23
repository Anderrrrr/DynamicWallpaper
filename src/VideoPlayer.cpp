#include "VideoPlayer.h"
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")

template <class T> void SafeRelease(T** ppT)
{
    if (*ppT) {
        (*ppT)->Release();
        *ppT = NULL;
    }
}

VideoPlayer::VideoPlayer(HWND hwnd) :
    m_hwndVideo(hwnd), m_isPlaying(false), m_nRefCount(1),
    m_pSession(NULL), m_pSource(NULL), m_pVideoDisplay(NULL)
{
}

VideoPlayer::~VideoPlayer()
{
    Shutdown();
}

HRESULT VideoPlayer::Initialize()
{
    return MFStartup(MF_VERSION);
}

void VideoPlayer::Shutdown()
{
    CloseSession();
    MFShutdown();
}

HRESULT VideoPlayer::QueryInterface(REFIID riid, void** ppv)
{
    static const QITAB qit[] = {
        QITABENT(VideoPlayer, IMFAsyncCallback),
        { 0 }
    };
    return QISearch(this, qit, riid, ppv);
}

ULONG VideoPlayer::AddRef()
{
    return InterlockedIncrement(&m_nRefCount);
}

ULONG VideoPlayer::Release()
{
    ULONG uCount = InterlockedDecrement(&m_nRefCount);
    if (uCount == 0) {
        delete this;
    }
    return uCount;
}

HRESULT VideoPlayer::GetParameters(DWORD* pdwFlags, DWORD* pdwQueue)
{
    return E_NOTIMPL; // Default queue
}

HRESULT VideoPlayer::Invoke(IMFAsyncResult* pAsyncResult)
{
    if (!m_pSession) return S_OK;

    IMFMediaEvent* pEvent = NULL;
    HRESULT hr = m_pSession->EndGetEvent(pAsyncResult, &pEvent);
    if (SUCCEEDED(hr)) {
        HandleEvent(pEvent);
    }

    if (m_pSession) {
        m_pSession->BeginGetEvent(this, NULL);
    }

    SafeRelease(&pEvent);
    return S_OK;
}

HRESULT VideoPlayer::HandleEvent(IMFMediaEvent* pEvent)
{
    MediaEventType meType = MEUnknown;
    HRESULT hr = pEvent->GetType(&meType);
    if (FAILED(hr)) return hr;

    if (meType == MESessionEnded) {
        // Seamless loop: Seek back to start and play
        PROPVARIANT varStart;
        PropVariantInit(&varStart);
        varStart.vt = VT_I8;
        varStart.hVal.QuadPart = 0;

        hr = m_pSession->Start(&GUID_NULL, &varStart);
        PropVariantClear(&varStart);
    }

    return hr;
}

HRESULT VideoPlayer::OpenFile(const std::wstring& path)
{
    HRESULT hr = CloseSession();
    if (FAILED(hr)) return hr;

    hr = CreateSession();
    if (FAILED(hr)) return hr;

    hr = CreateMediaSource(path, &m_pSource);
    if (FAILED(hr)) return hr;

    IMFTopology* pTopology = NULL;
    hr = CreateTopology(m_pSource, m_hwndVideo, &pTopology);
    if (SUCCEEDED(hr)) {
        hr = m_pSession->SetTopology(0, pTopology);
    }

    SafeRelease(&pTopology);
    return hr;
}

HRESULT VideoPlayer::CreateSession()
{
    HRESULT hr = MFCreateMediaSession(NULL, &m_pSession);
    if (SUCCEEDED(hr)) {
        hr = m_pSession->BeginGetEvent(this, NULL);
    }
    return hr;
}

HRESULT VideoPlayer::CloseSession()
{
    if (m_pVideoDisplay) {
        SafeRelease(&m_pVideoDisplay);
    }
    if (m_pSession) {
        m_pSession->Close();
        m_pSession->Shutdown();
        SafeRelease(&m_pSession);
    }
    if (m_pSource) {
        m_pSource->Shutdown();
        SafeRelease(&m_pSource);
    }
    m_isPlaying = false;
    return S_OK;
}

HRESULT VideoPlayer::CreateMediaSource(const std::wstring& url, IMFMediaSource** ppSource)
{
    IMFSourceResolver* pSourceResolver = NULL;
    IUnknown* pSource = NULL;

    HRESULT hr = MFCreateSourceResolver(&pSourceResolver);
    if (SUCCEEDED(hr)) {
        MF_OBJECT_TYPE ObjectType = MF_OBJECT_INVALID;
        hr = pSourceResolver->CreateObjectFromURL(
            url.c_str(), MF_RESOLUTION_MEDIASOURCE, NULL, &ObjectType, &pSource);
    }

    if (SUCCEEDED(hr)) {
        hr = pSource->QueryInterface(IID_PPV_ARGS(ppSource));
    }

    SafeRelease(&pSourceResolver);
    SafeRelease(&pSource);
    return hr;
}

HRESULT VideoPlayer::CreateTopology(IMFMediaSource* pSource, HWND hVideoWindow, IMFTopology** ppTopology)
{
    IMFTopology* pTopology = NULL;
    IMFPresentationDescriptor* pPD = NULL;
    DWORD cStreams = 0;

    HRESULT hr = MFCreateTopology(&pTopology);
    if (FAILED(hr)) return hr;

    hr = pSource->CreatePresentationDescriptor(&pPD);
    if (FAILED(hr)) {
        SafeRelease(&pTopology);
        return hr;
    }

    hr = pPD->GetStreamDescriptorCount(&cStreams);
    if (SUCCEEDED(hr)) {
        for (DWORD i = 0; i < cStreams; i++) {
            BOOL fSelected = FALSE;
            IMFStreamDescriptor* pSD = NULL;
            hr = pPD->GetStreamDescriptorByIndex(i, &fSelected, &pSD);
            if (SUCCEEDED(hr)) {
                if (fSelected) {
                    hr = AddToTopology(pTopology, pSource, i, hVideoWindow);
                }
                SafeRelease(&pSD);
            }
        }
    }

    if (SUCCEEDED(hr)) {
        *ppTopology = pTopology;
        (*ppTopology)->AddRef();
    }

    SafeRelease(&pTopology);
    SafeRelease(&pPD);
    return hr;
}

HRESULT VideoPlayer::AddToTopology(IMFTopology* pTopology, IMFMediaSource* pSource, DWORD streamIndex, HWND hwndVideo)
{
    IMFTopologyNode* pSourceNode = NULL;
    IMFTopologyNode* pOutputNode = NULL;
    IMFPresentationDescriptor* pPD = NULL;
    IMFStreamDescriptor* pSD = NULL;
    IMFActivate* pRendererActivate = NULL;

    HRESULT hr = pSource->CreatePresentationDescriptor(&pPD);
    if (FAILED(hr)) return hr;

    BOOL fSelected = FALSE;
    hr = pPD->GetStreamDescriptorByIndex(streamIndex, &fSelected, &pSD);
    if (FAILED(hr)) goto done;

    hr = MFCreateTopologyNode(MF_TOPOLOGY_SOURCESTREAM_NODE, &pSourceNode);
    if (FAILED(hr)) goto done;
    hr = pSourceNode->SetUnknown(MF_TOPONODE_SOURCE, pSource);
    hr = pSourceNode->SetUnknown(MF_TOPONODE_PRESENTATION_DESCRIPTOR, pPD);
    hr = pSourceNode->SetUnknown(MF_TOPONODE_STREAM_DESCRIPTOR, pSD);
    
    IMFMediaTypeHandler* pHandler = NULL;
    GUID guidMajorType = GUID_NULL;
    hr = pSD->GetMediaTypeHandler(&pHandler);
    if (SUCCEEDED(hr)) {
        hr = pHandler->GetMajorType(&guidMajorType);
        SafeRelease(&pHandler);
    }

    if (guidMajorType == MFMediaType_Video) {
        hr = MFCreateVideoRendererActivate(hwndVideo, &pRendererActivate);
        if (FAILED(hr)) goto done;

        hr = MFCreateTopologyNode(MF_TOPOLOGY_OUTPUT_NODE, &pOutputNode);
        if (FAILED(hr)) goto done;
        hr = pOutputNode->SetObject(pRendererActivate);
    } else if (guidMajorType == MFMediaType_Audio) {
        hr = MFCreateAudioRendererActivate(&pRendererActivate);
        if (FAILED(hr)) goto done;

        hr = MFCreateTopologyNode(MF_TOPOLOGY_OUTPUT_NODE, &pOutputNode);
        if (FAILED(hr)) goto done;
        hr = pOutputNode->SetObject(pRendererActivate);
    } else {
        goto done; // Ignore other streams
    }

    if (pOutputNode) {
        hr = pTopology->AddNode(pSourceNode);
        hr = pTopology->AddNode(pOutputNode);
        hr = pSourceNode->ConnectOutput(0, pOutputNode, 0);
    }

done:
    SafeRelease(&pSourceNode);
    SafeRelease(&pOutputNode);
    SafeRelease(&pPD);
    SafeRelease(&pSD);
    SafeRelease(&pRendererActivate);
    return hr;
}

HRESULT VideoPlayer::Play()
{
    if (!m_pSession) return E_UNEXPECTED;

    PROPVARIANT varStart;
    PropVariantInit(&varStart);
    varStart.vt = VT_EMPTY;

    HRESULT hr = m_pSession->Start(&GUID_NULL, &varStart);
    if (SUCCEEDED(hr)) {
        m_isPlaying = true;
    }
    return hr;
}

HRESULT VideoPlayer::Pause()
{
    if (!m_pSession || !m_isPlaying) return S_OK;

    HRESULT hr = m_pSession->Pause();
    if (SUCCEEDED(hr)) {
        m_isPlaying = false;
    }
    return hr;
}

HRESULT VideoPlayer::ResizeVideo(WORD width, WORD height)
{
    if (!m_pSession) return E_UNEXPECTED;

    if (!m_pVideoDisplay) {
        MFGetService(m_pSession, MR_VIDEO_RENDER_SERVICE, 
                     IID_PPV_ARGS(&m_pVideoDisplay));
    }

    if (m_pVideoDisplay) {
        // Use MFVideoARMode_PreservePicture to preserve aspect ratio.
        // EVR will letterbox or pillarbox if we don't adjust the source rectangle
        // To fill the screen and crop instead of letterbox, we can set AR mode and Pan&Scan.
        m_pVideoDisplay->SetAspectRatioMode(MFVideoARMode_PreservePicture);
        
        // This makes it fill the destination RECT without stretching, cropping the overflow.
        // It requires the video to support Pan & Scan, but setting the destination rect does the trick.
        RECT rcDest = { 0, 0, width, height };

        // For true "Fill screen and crop edges" (similar to object-fit: cover in CSS):
        // We can let EVR preserve picture, but we would need to calculate the crop ourselves 
        // to map to the normalized source rect.
        // However, a simple MFVideoARMode_PreservePicture | MFVideoARMode_NonLinearStretch is not crop.
        // Wait, EVR's default "letterbox" is not what the user wants. The user wants "fill the screen".
        // Let's manually calculate a normalized SrcRect to achieve "Crop to Fill".
        
        // Let's get the native video size first
        SIZE nativeSize = {0, 0};
        SIZE arSize = {0, 0};
        if (SUCCEEDED(m_pVideoDisplay->GetNativeVideoSize(&nativeSize, &arSize)) && nativeSize.cx > 0 && nativeSize.cy > 0)
        {
            float srcAspect = (float)nativeSize.cx / nativeSize.cy;
            float dstAspect = (float)width / height;

            MFVideoNormalizedRect srcRect = { 0.0f, 0.0f, 1.0f, 1.0f };

            if (srcAspect > dstAspect) {
                // Video is wider than screen -> crop left and right
                float visibleWidth = dstAspect / srcAspect;
                float margin = (1.0f - visibleWidth) / 2.0f;
                srcRect.left = margin;
                srcRect.right = 1.0f - margin;
            } else if (srcAspect < dstAspect) {
                // Video is taller than screen -> crop top and bottom
                float visibleHeight = srcAspect / dstAspect;
                float margin = (1.0f - visibleHeight) / 2.0f;
                srcRect.top = margin;
                srcRect.bottom = 1.0f - margin;
            }

            // Tell EVR not to letterbox, just paint our cropped source rect to the full dest rect
            m_pVideoDisplay->SetAspectRatioMode(MFVideoARMode_None);
            m_pVideoDisplay->SetVideoPosition(&srcRect, &rcDest);
        }
        else
        {
            // Fallback if we can't get size: just stretch
            m_pVideoDisplay->SetAspectRatioMode(MFVideoARMode_None);
            m_pVideoDisplay->SetVideoPosition(NULL, &rcDest);
        }
    }
    return S_OK;
}
