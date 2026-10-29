#include "WinRTCapture.h"
#include "User.h"
#include <stdexcept>
#include <algorithm>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "runtimeobject.lib")

WinRTCapture::WinRTCapture(User* user)
	: m_User(user)
{
	CreateDevice();

	HMONITOR hMon = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
	m_captureItem = CreateCaptureItem(hMon);

	m_screenW = m_captureItem.Size().Width;
	m_screenH = m_captureItem.Size().Height;

	int size = m_User->Network.size;
	m_regionW = (std::min)(size, (std::max)(1, m_screenW));
	m_regionH = (std::min)(size, (std::max)(1, m_screenH));
	m_regionX = (m_screenW - m_regionW) / 2;
	m_regionY = (m_screenH - m_regionH) / 2;

	m_User->Internal.ResolutionW = m_screenW;
	m_User->Internal.ResolutionH = m_screenH;

	m_framePool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
		m_winrtDevice,
		winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
		3,
		m_captureItem.Size()
	);

	m_session = m_framePool.CreateCaptureSession(m_captureItem);
	m_session.StartCapture();

	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = m_regionW;
	desc.Height = m_regionH;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

	HRESULT hr = m_device->CreateTexture2D(&desc, nullptr, m_sharedTexture.put());
	if (FAILED(hr))
		throw std::runtime_error("WinRTCapture: failed to create shared texture");

	winrt::com_ptr<IDXGIResource> dxgiRes;
	m_sharedTexture.as(dxgiRes);
	hr = dxgiRes->GetSharedHandle(&m_sharedHandle);
	if (FAILED(hr))
		throw std::runtime_error("WinRTCapture: failed to get shared handle");
}

WinRTCapture::~WinRTCapture()
{
	if (m_session) m_session.Close();
	if (m_framePool) m_framePool.Close();
	m_stagingTexture = nullptr;
	m_sharedTexture = nullptr;
	m_context = nullptr;
	m_device = nullptr;
	if (m_sharedHandle) CloseHandle(m_sharedHandle);
}

bool WinRTCapture::GetScreen(Frame& frame)
{
	if (!m_framePool) return false;

	auto lastFrame = m_framePool.TryGetNextFrame();
	if (!lastFrame) return false;

	auto frameSurface = lastFrame.Surface();
	auto dxgiAccess = frameSurface.as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
	winrt::com_ptr<ID3D11Texture2D> frameTexture;
	HRESULT hr = dxgiAccess->GetInterface(winrt::guid_of<ID3D11Texture2D>(), frameTexture.put_void());
	if (FAILED(hr)) return false;

	D3D11_BOX box;
	box.left = m_regionX;
	box.top = m_regionY;
	box.front = 0;
	box.right = m_regionX + m_regionW;
	box.bottom = m_regionY + m_regionH;
	box.back = 1;

	m_context->CopySubresourceRegion(
		m_sharedTexture.get(), 0, 0, 0, 0,
		frameTexture.get(), 0, &box
	);

	frame.textureHandle = m_sharedHandle;
	return true;
}

void WinRTCapture::CreateDevice()
{
	D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
	UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

	winrt::check_hresult(
		D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, 0, flags,
			levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
			m_device.put(), nullptr, m_context.put())
	);

	winrt::com_ptr<IDXGIDevice> dxgiDevice;
	m_device.as(dxgiDevice);

	winrt::com_ptr<::IInspectable> inspectable;
	winrt::check_hresult(
		CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(), inspectable.put())
	);
	m_winrtDevice = inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
}

winrt::Windows::Graphics::Capture::GraphicsCaptureItem
WinRTCapture::CreateCaptureItem(HMONITOR hMon)
{
	auto factory = winrt::get_activation_factory<
		winrt::Windows::Graphics::Capture::GraphicsCaptureItem,
		IGraphicsCaptureItemInterop>();

	winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{ nullptr };
	winrt::check_hresult(
		factory->CreateForMonitor(hMon,
			winrt::guid_of<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>(),
			winrt::put_abi(item))
	);
	return item;
}
