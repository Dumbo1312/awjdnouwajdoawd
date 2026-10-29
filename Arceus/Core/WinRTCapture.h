#pragma once
#include "ScreenCapture.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <string>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/base.h>

class User;

class WinRTCapture : public ScreenCapture
{
public:
	WinRTCapture(User* user);
	~WinRTCapture();
	bool GetScreen(Frame& frame) override;
private:
	User* m_User;
	winrt::com_ptr<ID3D11Device> m_device;
	winrt::com_ptr<ID3D11DeviceContext> m_context;
	winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_winrtDevice{ nullptr };
	winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_captureItem{ nullptr };
	winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool m_framePool{ nullptr };
	winrt::Windows::Graphics::Capture::GraphicsCaptureSession m_session{ nullptr };
	winrt::com_ptr<ID3D11Texture2D> m_sharedTexture;
	winrt::com_ptr<ID3D11Texture2D> m_stagingTexture;
	HANDLE m_sharedHandle = nullptr;
	int m_screenW = 0, m_screenH = 0;
	int m_regionW = 0, m_regionH = 0, m_regionX = 0, m_regionY = 0;

	void CreateDevice();
	winrt::Windows::Graphics::Capture::GraphicsCaptureItem CreateCaptureItem(HMONITOR hMon);

	template<typename T>
	void SafeRelease(T*& ptr)
	{
		if (ptr) { ptr->Release(); ptr = nullptr; }
	}
};
