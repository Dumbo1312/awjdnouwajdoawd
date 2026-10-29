#include "ScreenCapture.h"

#include "User.h"
#include "../Utilities/Strings.hpp"
#include "../Utilities/Logger.hpp"

#include <stdexcept>
#include <iostream>
#include <dshow.h>
#include <algorithm>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "strmiids.lib")

#include "VirtualizerSDK.h"

DesktopDuplicationGPU::DesktopDuplicationGPU(User* user)
    : m_User(user)
{
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };
    UINT numFeatureLevels = ARRAYSIZE(featureLevels);
    D3D_FEATURE_LEVEL selectedFeatureLevel;

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        featureLevels,
        numFeatureLevels,
        D3D11_SDK_VERSION,
        &m_lDevice,
        &selectedFeatureLevel,
        &m_lImmediateContext
    );
    if (FAILED(hr))
        throw std::runtime_error(SScreenCapture::DDDevCreation);

    Logger::Progress(SScreenCapture::LogCat, SScreenCapture::LogD3D11Device);

    IDXGIDevice* dxgiDevice = nullptr;
    hr = m_lDevice->QueryInterface(__uuidof(IDXGIDevice),
        reinterpret_cast<void**>(&dxgiDevice));
    if (FAILED(hr))
        throw std::runtime_error(SScreenCapture::DDDxgiDeviceFailed);

    Logger::Progress(SScreenCapture::LogCat, SScreenCapture::LogDXGIDevice);

    IDXGIAdapter* dxgiAdapter = nullptr;
    hr = dxgiDevice->GetParent(__uuidof(IDXGIAdapter),
        reinterpret_cast<void**>(&dxgiAdapter));
    SafeRelease(dxgiDevice);
    if (FAILED(hr))
        throw std::runtime_error(SScreenCapture::DDDxgiAdapterFailed);

    Logger::Progress(SScreenCapture::LogCat, SScreenCapture::LogDXGIAdapter);

    IDXGIOutput* dxgiOutput = nullptr;
    hr = dxgiAdapter->EnumOutputs(0, &dxgiOutput);
    SafeRelease(dxgiAdapter);
    if (FAILED(hr))
        throw std::runtime_error(SScreenCapture::DDOutputFailed);

    Logger::Progress(SScreenCapture::LogCat, SScreenCapture::LogPrimaryDisplay);

    hr = dxgiOutput->GetDesc(&m_lOutputDesc);
    if (FAILED(hr))
    {
        SafeRelease(dxgiOutput);
        throw std::runtime_error(SScreenCapture::DDDescriptionFailed);
    }

    Logger::Progress(SScreenCapture::LogCat, SScreenCapture::LogDisplayInfo);

    IDXGIOutput1* dxgiOutput1 = nullptr;
    hr = dxgiOutput->QueryInterface(__uuidof(IDXGIOutput1),
        reinterpret_cast<void**>(&dxgiOutput1));
    SafeRelease(dxgiOutput);
    if (FAILED(hr))
        throw std::runtime_error(SScreenCapture::DDOutputFailed);

    hr = dxgiOutput1->DuplicateOutput(m_lDevice, &m_lDeskDupl);
    SafeRelease(dxgiOutput1);
    if (FAILED(hr))
        throw std::runtime_error(SScreenCapture::DDDesktopDuplicationFailed);

    Logger::Progress(SScreenCapture::LogCat, SScreenCapture::LogDuplicationInterface);

    m_lDeskDupl->GetDesc(&m_lOutputDuplDesc);

VIRTUALIZER_MUTATE_ONLY_START

    m_User->Internal.ResolutionW = m_lOutputDuplDesc.ModeDesc.Width;
    m_User->Internal.ResolutionH = m_lOutputDuplDesc.ModeDesc.Height;

    m_CropBox.left = m_User->Internal.ResolutionW / 2 - m_User->Network.size / 2;
    m_CropBox.top = m_User->Internal.ResolutionH / 2 - m_User->Network.size / 2;
    m_CropBox.right = m_CropBox.left + m_User->Network.size;
    m_CropBox.bottom = m_CropBox.top + m_User->Network.size;
    m_CropBox.front = 0;
    m_CropBox.back = 1;

    Logger::Progress(SScreenCapture::LogCat, SScreenCapture::LogCaptureArea);

    D3D11_TEXTURE2D_DESC sharedTextureDesc = {};
    sharedTextureDesc.Width = m_CropBox.right - m_CropBox.left;
    sharedTextureDesc.Height = m_CropBox.bottom - m_CropBox.top;
    sharedTextureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sharedTextureDesc.ArraySize = 1;
    sharedTextureDesc.MipLevels = 1;
    sharedTextureDesc.SampleDesc.Count = 1;
    sharedTextureDesc.SampleDesc.Quality = 0;
    sharedTextureDesc.Usage = D3D11_USAGE_DEFAULT;
    sharedTextureDesc.CPUAccessFlags = 0;
    sharedTextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    sharedTextureDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

VIRTUALIZER_MUTATE_ONLY_END

    hr = m_lDevice->CreateTexture2D(&sharedTextureDesc, nullptr, &m_sharedTexture);
    if (FAILED(hr))
        throw std::runtime_error(SScreenCapture::DDSharedTexture);

    Logger::Progress(SScreenCapture::LogCat, SScreenCapture::LogSharedTexture);

    IDXGIResource* sharedResource = nullptr;
    hr = m_sharedTexture->QueryInterface(__uuidof(IDXGIResource),
        reinterpret_cast<void**>(&sharedResource));
    if (FAILED(hr))
        throw std::runtime_error(SScreenCapture::DDSharedInterface);

    hr = sharedResource->GetSharedHandle(&m_sharedHandle);
    SafeRelease(sharedResource);
    if (FAILED(hr))
        throw std::runtime_error(SScreenCapture::DDSharedHandle);

    Logger::Progress(SScreenCapture::LogCat, SScreenCapture::LogSharedHandle);
    Logger::Progress(SScreenCapture::LogCat, SScreenCapture::LogCreation + std::to_wstring(m_lOutputDuplDesc.ModeDesc.Width) + L"x" + std::to_wstring(m_lOutputDuplDesc.ModeDesc.Height));
}

bool DesktopDuplicationGPU::GetScreen(Frame& frame)
{
    IDXGIResource* desktopResource = nullptr;
    DXGI_OUTDUPL_FRAME_INFO frameInfo = {};

    HRESULT hr = m_lDeskDupl->AcquireNextFrame(33, &frameInfo, &desktopResource);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT)
        return false;

    if (SUCCEEDED(hr) && frameInfo.AccumulatedFrames == 0)
    {
        m_lDeskDupl->ReleaseFrame();
        return false;
    }

    if (hr == DXGI_ERROR_ACCESS_LOST || hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
    {
        Sleep(50);
        RecreateOutputDuplication();
        return false;
    }

    if (FAILED(hr))
    {
        throw std::runtime_error(SScreenCapture::DDFailedToAcquireFrame);
        return false;
    }

    hr = desktopResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&m_lAcquiredDesktopImage));
    SafeRelease(desktopResource); 

    m_lImmediateContext->CopySubresourceRegion(
        m_sharedTexture,
        0,
        0, 0, 0,
        m_lAcquiredDesktopImage,
        0,
        &m_CropBox
    );

    m_lDeskDupl->ReleaseFrame();
    SafeRelease(m_lAcquiredDesktopImage);

    frame.textureHandle = m_sharedHandle;

    return true;
}

void DesktopDuplicationGPU::RecreateOutputDuplication()
{
    SafeRelease(m_lDeskDupl);
    SafeRelease(m_sharedTexture);
    if (m_sharedHandle)
    {
        CloseHandle(m_sharedHandle);
        m_sharedHandle = nullptr;
    }

    IDXGIFactory1* factory = nullptr;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1),
        reinterpret_cast<void**>(&factory));
    if (FAILED(hr))
        return;

    IDXGIAdapter1* adapter = nullptr;
    hr = factory->EnumAdapters1(0, &adapter);
    if (FAILED(hr))
    {
        SafeRelease(factory);
        return;
    }

    IDXGIOutput* output = nullptr;
    hr = adapter->EnumOutputs(0, &output);
    if (FAILED(hr))
    {
        SafeRelease(adapter);
        SafeRelease(factory);
        return;
    }

    IDXGIOutput1* output1 = nullptr;
    hr = output->QueryInterface(__uuidof(IDXGIOutput1),
        reinterpret_cast<void**>(&output1));
    if (FAILED(hr))
    {
        SafeRelease(output);
        SafeRelease(adapter);
        SafeRelease(factory);
        return;
    }

    hr = output1->DuplicateOutput(m_lDevice, &m_lDeskDupl);
    if (SUCCEEDED(hr))
    {
        m_lDeskDupl->GetDesc(&m_lOutputDuplDesc);

VIRTUALIZER_MUTATE_ONLY_START

        m_User->Internal.ResolutionW = m_lOutputDuplDesc.ModeDesc.Width;
        m_User->Internal.ResolutionH = m_lOutputDuplDesc.ModeDesc.Height;

        m_CropBox.left = m_User->Internal.ResolutionW / 2 - m_User->Network.size / 2;
        m_CropBox.top = m_User->Internal.ResolutionH / 2 - m_User->Network.size / 2;
        m_CropBox.right = m_CropBox.left + m_User->Network.size;
        m_CropBox.bottom = m_CropBox.top + m_User->Network.size;
        m_CropBox.front = 0;
        m_CropBox.back = 1;

        D3D11_TEXTURE2D_DESC sharedTextureDesc = {};
        sharedTextureDesc.Width = m_CropBox.right - m_CropBox.left;
        sharedTextureDesc.Height = m_CropBox.bottom - m_CropBox.top;
        sharedTextureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sharedTextureDesc.ArraySize = 1;
        sharedTextureDesc.MipLevels = 1;
        sharedTextureDesc.SampleDesc.Count = 1;
        sharedTextureDesc.SampleDesc.Quality = 0;
        sharedTextureDesc.Usage = D3D11_USAGE_DEFAULT;
        sharedTextureDesc.CPUAccessFlags = 0;
        sharedTextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        sharedTextureDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

VIRTUALIZER_MUTATE_ONLY_END

        hr = m_lDevice->CreateTexture2D(&sharedTextureDesc, nullptr, &m_sharedTexture);
        if (FAILED(hr))
        {
            SafeRelease(output1);
            SafeRelease(output);
            SafeRelease(adapter);
            SafeRelease(factory);
            return;
        }

        IDXGIResource* sharedResource = nullptr;
        hr = m_sharedTexture->QueryInterface(__uuidof(IDXGIResource),
            reinterpret_cast<void**>(&sharedResource));
        if (FAILED(hr))
        {
            SafeRelease(output1);
            SafeRelease(output);
            SafeRelease(adapter);
            SafeRelease(factory);
            return;
        }

        hr = sharedResource->GetSharedHandle(&m_sharedHandle);
        SafeRelease(sharedResource);

        if (FAILED(hr))
        {
            SafeRelease(output1);
            SafeRelease(output);
            SafeRelease(adapter);
            SafeRelease(factory);
            return;
        }

        m_User->UseSettings();
        Logger::Progress(SScreenCapture::LogCat, SScreenCapture::LogRecreation + std::to_wstring(m_lOutputDuplDesc.ModeDesc.Width) + L"x" + std::to_wstring(m_lOutputDuplDesc.ModeDesc.Height));
    }

    SafeRelease(output1);
    SafeRelease(output);
    SafeRelease(adapter);
    SafeRelease(factory);
}

DesktopDuplicationGPU::~DesktopDuplicationGPU()
{
    SafeRelease(m_lAcquiredDesktopImage);
    SafeRelease(m_lDeskDupl);
    SafeRelease(m_lImmediateContext);
    SafeRelease(m_lDevice);
}


static const CLSID CLSID_SampleGrabber_ = { 0xC1F73A28, 0xECB3, 0x4F0E, {0x94,0x7C,0x8A,0x0C,0x7A,0xBC,0x58,0x9F} };
static const CLSID CLSID_NullRenderer_ = { 0xC1F73A29, 0xECB3, 0x4F0E, {0x94,0x7C,0x8A,0x0C,0x7A,0xBC,0x58,0x9F} };
static const IID IID_ISampleGrabber_ = { 0x6B652FFF, 0x11FE, 0x4fce, {0x92,0xAD,0x02,0x66,0xB5,0xD7,0xC7,0x8F} };
static const IID IID_ISampleGrabberCB_ = { 0x8A1B25C9, 0x0B2A, 0x42C6, {0xAF,0x13,0x32,0x35,0xC3,0x73,0x76,0x5B} };

struct ISampleGrabberCB : IUnknown
{
	virtual STDMETHODIMP SampleCB(double SampleTime, IMediaSample* pSample) = 0;
	virtual STDMETHODIMP BufferCB(double SampleTime, BYTE* pBuffer, long BufferLen) = 0;
};

struct ISampleGrabber : IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE SetOneShot(BOOL OneShot) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetMediaType(const AM_MEDIA_TYPE* pType) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetConnectedMediaType(AM_MEDIA_TYPE* pType) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetBufferSamples(BOOL BufferThem) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetCurrentBuffer(long* pBufferSize, long* pBuffer) = 0;
	virtual HRESULT STDMETHODCALLTYPE GetCurrentSample(IMediaSample** ppSample) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetCallback(ISampleGrabberCB* pCallback, long WhichMethodToCallback) = 0;
};

class CGB_Callback : public ISampleGrabberCB
{
public:
	volatile bool frameReady = false;
	BYTE* frameData = nullptr;
	int frameWidth = 0, frameHeight = 0, frameStride = 0;
	CRITICAL_SECTION cs;

	CGB_Callback() { InitializeCriticalSection(&cs); }
	~CGB_Callback() { DeleteCriticalSection(&cs); delete[] frameData; }

	STDMETHODIMP_(ULONG) AddRef() { return 1; }
	STDMETHODIMP_(ULONG) Release() { return 2; }
	STDMETHODIMP QueryInterface(REFIID riid, void** ppv)
	{
		if (riid == IID_ISampleGrabberCB_ || riid == IID_IUnknown)
		{ *ppv = (void*)this; return S_OK; }
		return E_NOINTERFACE;
	}

	STDMETHODIMP SampleCB(double, IMediaSample*) { return E_NOTIMPL; }

	STDMETHODIMP BufferCB(double, BYTE* buffer, long len)
	{
		EnterCriticalSection(&cs);
		if (!frameData || len > frameWidth * frameHeight * 4 + 1024)
		{ delete[] frameData; frameData = new BYTE[len]; frameStride = frameWidth * 3; }
		memcpy(frameData, buffer, len);
		frameReady = true;
		LeaveCriticalSection(&cs);
		return S_OK;
	}
};

static HRESULT GetPin(IBaseFilter* pFilter, PIN_DIRECTION dir, IPin** ppPin)
{
	IEnumPins* pEnum = nullptr;
	HRESULT hr = pFilter->EnumPins(&pEnum);
	if (FAILED(hr)) return hr;
	IPin* pPin = nullptr;
	while (pEnum->Next(1, &pPin, NULL) == S_OK)
	{
		PIN_DIRECTION pinDir;
		pPin->QueryDirection(&pinDir);
		if (pinDir == dir) { *ppPin = pPin; pEnum->Release(); return S_OK; }
		pPin->Release();
	}
	pEnum->Release();
	return E_FAIL;
}

CaptureCardCapture::CaptureCardCapture(User* user, int deviceIndex)
    : m_User(user), m_DeviceIndex(deviceIndex)
{
	auto coinit = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	bool needUninit = SUCCEEDED(coinit);

	D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
	auto hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
		levels, 2, D3D11_SDK_VERSION, &m_lDevice, nullptr, &m_lImmediateContext);
	if (FAILED(hr)) { if (needUninit) CoUninitialize(); throw std::runtime_error("Failed to create D3D11 device"); }

	ICreateDevEnum* pDevEnum = nullptr;
	hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER, IID_ICreateDevEnum, (void**)&pDevEnum);
	if (FAILED(hr)) { if (needUninit) CoUninitialize(); throw std::runtime_error("Failed to create device enumerator"); }

	IEnumMoniker* pEnum = nullptr;
	hr = pDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pEnum, 0);
	pDevEnum->Release();
	if (hr != S_OK || !pEnum) { if (needUninit) CoUninitialize(); throw std::runtime_error("No video capture devices found"); }

	IMoniker* pMoniker = nullptr;
	int idx = 0;
	bool found = false;
	while (pEnum->Next(1, &pMoniker, NULL) == S_OK)
	{
		if (idx++ == deviceIndex) { found = true; break; }
		pMoniker->Release();
	}
	pEnum->Release();
	if (!found) { if (needUninit) CoUninitialize(); throw std::runtime_error("Capture card device index not found"); }

	hr = pMoniker->BindToObject(0, 0, IID_IBaseFilter, (void**)&m_pCaptureFilter);
	pMoniker->Release();
	if (FAILED(hr)) { if (needUninit) CoUninitialize(); throw std::runtime_error("Failed to bind capture device"); }

	hr = CoCreateInstance(CLSID_CaptureGraphBuilder2, NULL, CLSCTX_INPROC_SERVER, IID_ICaptureGraphBuilder2, (void**)&m_pBuilder);
	if (FAILED(hr)) { if (needUninit) CoUninitialize(); throw std::runtime_error("Failed to create capture graph builder"); }

	hr = CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, IID_IGraphBuilder, (void**)&m_pGraph);
	if (FAILED(hr)) { if (needUninit) CoUninitialize(); throw std::runtime_error("Failed to create filter graph"); }

	m_pBuilder->SetFiltergraph(m_pGraph);
	m_pGraph->AddFilter(m_pCaptureFilter, L"Video Capture");

	hr = CoCreateInstance(CLSID_SampleGrabber_, NULL, CLSCTX_INPROC_SERVER, IID_IBaseFilter, (void**)&m_pGrabberFilter);
	if (FAILED(hr)) { if (needUninit) CoUninitialize(); throw std::runtime_error("Failed to create sample grabber"); }

	hr = m_pGrabberFilter->QueryInterface(IID_ISampleGrabber_, (void**)&m_pGrabber);
	if (FAILED(hr) || !m_pGrabber) { if (needUninit) CoUninitialize(); throw std::runtime_error("Failed to get sample grabber interface"); }

	AM_MEDIA_TYPE mt;
	ZeroMemory(&mt, sizeof(AM_MEDIA_TYPE));
	mt.majortype = MEDIATYPE_Video;
	mt.subtype = MEDIASUBTYPE_RGB24;
	mt.formattype = FORMAT_VideoInfo;
	m_pGrabber->SetMediaType(&mt);
	m_pGraph->AddFilter(m_pGrabberFilter, L"Sample Grabber");

	hr = CoCreateInstance(CLSID_NullRenderer_, NULL, CLSCTX_INPROC_SERVER, IID_IBaseFilter, (void**)&m_pNullRenderer);
	if (SUCCEEDED(hr)) m_pGraph->AddFilter(m_pNullRenderer, L"Null Renderer");

	auto* cb = new CGB_Callback();
	m_callback = cb;
	m_pGrabber->SetCallback(cb, 1);

	hr = m_pBuilder->RenderStream(&PIN_CATEGORY_CAPTURE, &MEDIATYPE_Video, m_pCaptureFilter, m_pGrabberFilter, m_pNullRenderer);
	if (FAILED(hr)) { if (needUninit) CoUninitialize(); throw std::runtime_error("Failed to render capture stream"); }

	AM_MEDIA_TYPE connectedMt;
	if (SUCCEEDED(m_pGrabber->GetConnectedMediaType(&connectedMt)) && connectedMt.formattype == FORMAT_VideoInfo
		&& connectedMt.cbFormat >= sizeof(VIDEOINFOHEADER))
	{
		auto* vih = (VIDEOINFOHEADER*)connectedMt.pbFormat;
		m_Width = vih->bmiHeader.biWidth;
		m_Height = abs(vih->bmiHeader.biHeight);
		m_Stride = m_Width * 3;
		cb->frameWidth = m_Width;
		cb->frameHeight = m_Height;
	}
	if (connectedMt.pbFormat) CoTaskMemFree(connectedMt.pbFormat);

	m_pGraph->QueryInterface(IID_IMediaControl, (void**)&m_pControl);
	if (m_pControl) m_pControl->Run();
}

bool CaptureCardCapture::GetScreen(Frame& frame)
{
	auto* cb = (CGB_Callback*)m_callback;
	if (!cb || !cb->frameReady || !m_lDevice) return false;

	if (!m_stagingTexture)
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = m_Width;
		desc.Height = m_Height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		if (FAILED(m_lDevice->CreateTexture2D(&desc, nullptr, &m_stagingTexture))) return false;

		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.CPUAccessFlags = 0;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		if (FAILED(m_lDevice->CreateTexture2D(&desc, nullptr, &m_sharedTexture))) return false;

		IDXGIResource* pResource = nullptr;
		if (SUCCEEDED(m_sharedTexture->QueryInterface(__uuidof(IDXGIResource), (void**)&pResource)))
		{
			pResource->GetSharedHandle(&m_sharedHandle);
			pResource->Release();
		}
	}

	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(m_lImmediateContext->Map(m_stagingTexture, 0, D3D11_MAP_READ, 0, &mapped))) return false;

	int srcStride = m_Stride;
	int dstStride = mapped.RowPitch;

	EnterCriticalSection(&cb->cs);
	bool hasFrame = cb->frameReady;
	BYTE* src = cb->frameData;
	if (hasFrame) cb->frameReady = false;
	LeaveCriticalSection(&cb->cs);

	if (!hasFrame || !src) { m_lImmediateContext->Unmap(m_stagingTexture, 0); return false; }

	BYTE* dst = (BYTE*)mapped.pData;
	for (int y = 0; y < m_Height; y++)
		for (int x = 0; x < m_Width; x++)
		{
			int si = y * srcStride + x * 3;
			int di = y * dstStride + x * 4;
			dst[di + 2] = src[si + 0];
			dst[di + 1] = src[si + 1];
			dst[di + 0] = src[si + 2];
			dst[di + 3] = 255;
		}
	m_lImmediateContext->Unmap(m_stagingTexture, 0);
	m_lImmediateContext->CopyResource(m_sharedTexture, m_stagingTexture);
	frame.textureHandle = m_sharedHandle;
	return true;
}

CaptureCardCapture::~CaptureCardCapture()
{
	if (m_pControl) { m_pControl->Stop(); m_pControl->Release(); m_pControl = nullptr; }
	delete (CGB_Callback*)m_callback; m_callback = nullptr;
	SafeRelease(m_pGrabber); SafeRelease(m_pGrabberFilter); SafeRelease(m_pNullRenderer);
	SafeRelease(m_pCaptureFilter); SafeRelease(m_pBuilder);
	if (m_sharedTexture) { m_sharedTexture->Release(); m_sharedTexture = nullptr; }
	if (m_stagingTexture) { m_stagingTexture->Release(); m_stagingTexture = nullptr; }
	if (m_sharedHandle) { CloseHandle(m_sharedHandle); m_sharedHandle = nullptr; }
	if (m_lImmediateContext) { m_lImmediateContext->Release(); m_lImmediateContext = nullptr; }
	if (m_lDevice) { m_lDevice->Release(); m_lDevice = nullptr; }
	if (m_pGraph) { auto p = GetPin; (void)p; m_pGraph->Release(); m_pGraph = nullptr; }
}
