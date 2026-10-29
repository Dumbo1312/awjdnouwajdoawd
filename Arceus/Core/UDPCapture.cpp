#include "UDPCapture.h"
#include "CrashLog.h"
#include "User.h"
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <thread>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "WindowsCodecs.lib")

UDPCapture::UDPCapture(User* user, const std::string& ip, int port)
	: m_User(user), m_ip(ip), m_port(port)
{
	m_width = m_User->Network.size;
	m_height = m_User->Network.size;

	HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
		CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
		reinterpret_cast<void**>(&m_wicFactory));
	if (FAILED(hr))
		throw std::runtime_error("UDPCapture: failed to create WIC factory");

	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		throw std::runtime_error("UDPCapture: WSAStartup failed");

	m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (m_socket == INVALID_SOCKET)
	{
		WSACleanup();
		throw std::runtime_error("UDPCapture: failed to create socket");
	}

	int bufsize = MAX_FRAME_SIZE;
	setsockopt(m_socket, SOL_SOCKET, SO_RCVBUF, (char*)&bufsize, sizeof(bufsize));

	u_long nonblock = 1;
	ioctlsocket(m_socket, FIONBIO, &nonblock);

	sockaddr_in local{};
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = INADDR_ANY;
	local.sin_port = htons(port);

	if (bind(m_socket, (sockaddr*)&local, sizeof(local)) == SOCKET_ERROR)
	{
		closesocket(m_socket);
		WSACleanup();
		throw std::runtime_error("UDPCapture: bind failed");
	}

	D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
	UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

	hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, 0, flags,
		levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
		&m_device, nullptr, &m_context);
	if (FAILED(hr))
	{
		closesocket(m_socket);
		WSACleanup();
		throw std::runtime_error("UDPCapture: failed to create D3D11 device");
	}

	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = m_width;
	desc.Height = m_height;
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
		closesocket(m_socket);
		WSACleanup();
		throw std::runtime_error("UDPCapture: failed to create shared texture");
	}

	IDXGIResource* dxgiRes = nullptr;
	m_sharedTexture->QueryInterface(IID_PPV_ARGS(&dxgiRes));
	dxgiRes->GetSharedHandle(&m_sharedHandle);
	dxgiRes->Release();

	D3D11_TEXTURE2D_DESC stDesc{};
	stDesc.Width = m_width;
	stDesc.Height = m_height;
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
		closesocket(m_socket);
		WSACleanup();
		throw std::runtime_error("UDPCapture: failed to create staging texture");
	}

	m_User->Internal.ResolutionW = m_width;
	m_User->Internal.ResolutionH = m_height;

	m_connected = true;
	m_recvThread = std::thread(&UDPCapture::ReceiveThread, this);
}

UDPCapture::~UDPCapture()
{
	m_stop = true;
	m_connected = false;
	if (m_recvThread.joinable())
		m_recvThread.join();
	if (m_socket != INVALID_SOCKET)
		closesocket(m_socket);
	CleanupD3D();
	SafeRelease(m_wicFactory);
	WSACleanup();
}

void UDPCapture::CleanupD3D()
{
	SafeRelease(m_stagingTexture);
	SafeRelease(m_sharedTexture);
	SafeRelease(m_context);
	SafeRelease(m_device);
	if (m_sharedHandle) { CloseHandle(m_sharedHandle); m_sharedHandle = nullptr; }
}

bool UDPCapture::DecodeJPEG(const std::vector<uint8_t>& jpeg, std::vector<uint8_t>& bgra, int& w, int& h)
{
	if (!m_wicFactory || jpeg.empty())
		return false;

	IWICStream* stream = nullptr;
	HRESULT hr = m_wicFactory->CreateStream(&stream);
	if (FAILED(hr)) return false;

	hr = stream->InitializeFromMemory(const_cast<BYTE*>(jpeg.data()), (DWORD)jpeg.size());
	if (FAILED(hr)) { stream->Release(); return false; }

	IWICBitmapDecoder* decoder = nullptr;
	hr = m_wicFactory->CreateDecoderFromStream(stream, nullptr,
		WICDecodeMetadataCacheOnDemand, &decoder);
	stream->Release();
	if (FAILED(hr)) return false;

	IWICBitmapFrameDecode* frame = nullptr;
	hr = decoder->GetFrame(0, &frame);
	decoder->Release();
	if (FAILED(hr)) return false;

	UINT srcW, srcH;
	frame->GetSize(&srcW, &srcH);
	w = srcW;
	h = srcH;

	IWICFormatConverter* converter = nullptr;
	hr = m_wicFactory->CreateFormatConverter(&converter);
	if (FAILED(hr)) { frame->Release(); return false; }

	hr = converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
		WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
	frame->Release();
	if (FAILED(hr)) { converter->Release(); return false; }

	bgra.resize(srcW * srcH * 4);
	hr = converter->CopyPixels(nullptr, srcW * 4, (UINT)bgra.size(), bgra.data());
	converter->Release();
	return SUCCEEDED(hr);
}

bool UDPCapture::GetScreen(Frame& frame)
{
	std::vector<uint8_t> jpeg;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_queue.empty())
			return false;
		jpeg = std::move(m_queue.front());
		m_queue.pop();
	}

	std::vector<uint8_t> bgra;
	int imgW = 0, imgH = 0;
	if (!DecodeJPEG(jpeg, bgra, imgW, imgH) || bgra.empty())
		return false;

	if (imgW != m_width || imgH != m_height)
	{
		std::vector<uint8_t> resized(m_width * m_height * 4);
		float sx = (float)imgW / m_width;
		float sy = (float)imgH / m_height;
		for (int y = 0; y < m_height; y++)
		{
			int sy_in = std::min((int)(y * sy), imgH - 1);
			const uint8_t* srcRow = bgra.data() + sy_in * imgW * 4;
			uint8_t* dst = resized.data() + y * m_width * 4;
			for (int x = 0; x < m_width; x++)
			{
				int sx_in = std::min((int)(x * sx), imgW - 1);
				((uint32_t*)dst)[x] = ((const uint32_t*)srcRow)[sx_in];
			}
		}
		bgra = std::move(resized);
	}

	D3D11_MAPPED_SUBRESOURCE mapped;
	HRESULT hr = m_context->Map(m_stagingTexture, 0, D3D11_MAP_WRITE, 0, &mapped);
	if (FAILED(hr))
		return false;

	uint8_t* dst = static_cast<uint8_t*>(mapped.pData);
	int rowPitch = m_width * 4;
	for (int y = 0; y < m_height; y++)
		memcpy(dst + y * mapped.RowPitch, bgra.data() + y * rowPitch, rowPitch);
	m_context->Unmap(m_stagingTexture, 0);

	m_context->CopySubresourceRegion(m_sharedTexture, 0, 0, 0, 0,
		m_stagingTexture, 0, nullptr);

	frame.textureHandle = m_sharedHandle;
	return true;
}

void UDPCapture::ReceiveThread()
{
	CrashLog::Log(("THREAD UDPCapture::ReceiveThread tid=" + std::to_string(GetCurrentThreadId())).c_str());
	std::vector<uint8_t> buffer(MAX_FRAME_SIZE);
	std::vector<uint8_t> frameData;

	while (!m_stop)
	{
		int bytes = recvfrom(m_socket, (char*)buffer.data(), (int)buffer.size(), 0, nullptr, nullptr);
		if (bytes == SOCKET_ERROR)
		{
			int err = WSAGetLastError();
			if (err == WSAEWOULDBLOCK)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				continue;
			}
			break;
		}
		if (bytes <= 0) continue;

		frameData.insert(frameData.end(), buffer.begin(), buffer.begin() + bytes);
		if (frameData.size() > MAX_FRAME_SIZE * 2)
		{
			frameData.clear();
			continue;
		}

		size_t start = 0;
		bool foundStart = false;
		for (size_t i = 0; i + 1 < frameData.size(); i++)
		{
			if (frameData[i] == 0xFF && frameData[i + 1] == 0xD8)
			{
				start = i;
				foundStart = true;
				break;
			}
		}
		if (!foundStart) continue;

		size_t end = frameData.size();
		bool foundEnd = false;
		for (size_t i = start + 2; i + 1 < frameData.size(); i++)
		{
			if (frameData[i] == 0xFF && frameData[i + 1] == 0xD9)
			{
				end = i + 2;
				foundEnd = true;
				break;
			}
		}
		if (!foundEnd) continue;

		std::vector<uint8_t> jpeg(frameData.begin() + start, frameData.begin() + end);
		frameData.clear();

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			while (m_queue.size() >= MAX_QUEUE_SIZE)
				m_queue.pop();
			m_queue.push(std::move(jpeg));
		}
	}
}
