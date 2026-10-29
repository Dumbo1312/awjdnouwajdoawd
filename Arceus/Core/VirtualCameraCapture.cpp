#include "VirtualCameraCapture.h"
#include "User.h"
#include <stdexcept>
#include <cstring>
#include <vector>
#include <algorithm>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

VirtualCameraCapture::VirtualCameraCapture(User* user, int deviceIndex)
	: m_User(user)
{
	HRESULT hr = MFStartup(MF_VERSION);
	if (FAILED(hr))
		throw std::runtime_error("VirtualCameraCapture: MFStartup failed");

	IMFAttributes* config = nullptr;
	hr = MFCreateAttributes(&config, 1);
	if (FAILED(hr))
	{
		MFShutdown();
		throw std::runtime_error("VirtualCameraCapture: failed to create attributes");
	}

	hr = config->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
	if (FAILED(hr))
	{
		config->Release();
		MFShutdown();
		throw std::runtime_error("VirtualCameraCapture: failed to set source type");
	}

	IMFActivate** devices = nullptr;
	UINT32 count = 0;
	hr = MFEnumDeviceSources(config, &devices, &count);
	config->Release();
	if (FAILED(hr) || count == 0)
	{
		MFShutdown();
		throw std::runtime_error("VirtualCameraCapture: no video devices found");
	}

	int idx = std::min(deviceIndex, (int)count - 1);
	IMFMediaSource* source = nullptr;
	hr = devices[idx]->ActivateObject(IID_PPV_ARGS(&source));

	for (UINT32 i = 0; i < count; i++)
		devices[i]->Release();
	CoTaskMemFree(devices);

	if (FAILED(hr))
	{
		MFShutdown();
		throw std::runtime_error("VirtualCameraCapture: failed to activate device");
	}

	IMFAttributes* readerConfig = nullptr;
	MFCreateAttributes(&readerConfig, 1);
	if (readerConfig)
	{
		readerConfig->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, FALSE);
	}

	hr = MFCreateSourceReaderFromMediaSource(source, readerConfig, &m_reader);
	if (readerConfig) readerConfig->Release();
	source->Release();

	if (FAILED(hr))
	{
		MFShutdown();
		throw std::runtime_error("VirtualCameraCapture: failed to create source reader");
	}

	hr = m_reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
	if (FAILED(hr))
	{
		SafeRelease(m_reader);
		MFShutdown();
		throw std::runtime_error("VirtualCameraCapture: failed to set video stream");
	}

	IMFMediaType* nativeType = nullptr;
	hr = m_reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &nativeType);
	if (SUCCEEDED(hr))
	{
		UINT32 w = 0, h = 0;
		MFGetAttributeSize(nativeType, MF_MT_FRAME_SIZE, &w, &h);
		nativeType->Release();
	}

	int size = m_User->Network.size;
	m_width = size;
	m_height = size;
	m_User->Internal.ResolutionW = size;
	m_User->Internal.ResolutionH = size;

	IMFMediaType* targetType = nullptr;
	hr = MFCreateMediaType(&targetType);
	if (SUCCEEDED(hr))
	{
		targetType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
		targetType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
		MFSetAttributeSize(targetType, MF_MT_FRAME_SIZE, size, size);
		m_reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, targetType);
		targetType->Release();
	}

	D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
	UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, 0, flags,
		levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
		&m_device, nullptr, &m_context);
	if (FAILED(hr))
	{
		SafeRelease(m_reader);
		MFShutdown();
		throw std::runtime_error("VirtualCameraCapture: failed to create D3D11 device");
	}

	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = size;
	desc.Height = size;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

	hr = m_device->CreateTexture2D(&desc, nullptr, &m_sharedTexture);
	if (FAILED(hr))
	{
		SafeRelease(m_reader);
		MFShutdown();
		throw std::runtime_error("VirtualCameraCapture: failed to create shared texture");
	}

	IDXGIResource* dxgiRes = nullptr;
	m_sharedTexture->QueryInterface(IID_PPV_ARGS(&dxgiRes));
	dxgiRes->GetSharedHandle(&m_sharedHandle);
	dxgiRes->Release();

	D3D11_TEXTURE2D_DESC stDesc{};
	stDesc.Width = size;
	stDesc.Height = size;
	stDesc.MipLevels = 1;
	stDesc.ArraySize = 1;
	stDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	stDesc.SampleDesc.Count = 1;
	stDesc.SampleDesc.Quality = 0;
	stDesc.Usage = D3D11_USAGE_STAGING;
	stDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	hr = m_device->CreateTexture2D(&stDesc, nullptr, &m_stagingTexture);
	if (FAILED(hr))
	{
		SafeRelease(m_sharedTexture);
		SafeRelease(m_reader);
		MFShutdown();
		throw std::runtime_error("VirtualCameraCapture: failed to create staging texture");
	}
}

VirtualCameraCapture::~VirtualCameraCapture()
{
	SafeRelease(m_reader);
	CleanupD3D();
	MFShutdown();
}

void VirtualCameraCapture::CleanupD3D()
{
	SafeRelease(m_stagingTexture);
	SafeRelease(m_sharedTexture);
	SafeRelease(m_context);
	SafeRelease(m_device);
	if (m_sharedHandle) { CloseHandle(m_sharedHandle); m_sharedHandle = nullptr; }
}

bool VirtualCameraCapture::GetScreen(Frame& frame)
{
	if (!m_reader) return false;

	DWORD streamFlags = 0;
	IMFSample* sample = nullptr;
	HRESULT hr = m_reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
		0, nullptr, &streamFlags, nullptr, &sample);
	if (FAILED(hr) || !sample)
		return false;

	IMFMediaBuffer* buffer = nullptr;
	hr = sample->ConvertToContiguousBuffer(&buffer);
	sample->Release();
	if (FAILED(hr))
		return false;

	BYTE* data = nullptr;
	DWORD maxLen = 0, curLen = 0;
	hr = buffer->Lock(&data, &maxLen, &curLen);
	if (FAILED(hr) || curLen == 0)
	{
		buffer->Release();
		return false;
	}

	int size = m_width;
	D3D11_MAPPED_SUBRESOURCE mapped;
	hr = m_context->Map(m_stagingTexture, 0, D3D11_MAP_WRITE, 0, &mapped);
	if (SUCCEEDED(hr))
	{
		uint8_t* dst = static_cast<uint8_t*>(mapped.pData);
		int rowPitch = size * 4;
		for (int y = 0; y < size; y++)
			memcpy(dst + y * mapped.RowPitch, data + y * rowPitch, rowPitch);
		m_context->Unmap(m_stagingTexture, 0);

		m_context->CopySubresourceRegion(m_sharedTexture, 0, 0, 0, 0,
			m_stagingTexture, 0, nullptr);
		frame.textureHandle = m_sharedHandle;
	}

	buffer->Unlock();
	buffer->Release();
	return SUCCEEDED(hr);
}
