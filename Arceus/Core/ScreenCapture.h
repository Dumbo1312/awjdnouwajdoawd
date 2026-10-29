#pragma once
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>

class User;

class Frame
{
public:
	HANDLE textureHandle = nullptr;

	Frame() = default;
};

class ScreenCapture
{
public:
	virtual bool GetScreen(Frame& frame) = 0;
};

class DesktopDuplicationGPU : public ScreenCapture
{
public:
	DesktopDuplicationGPU(User* User);

	~DesktopDuplicationGPU();

	bool GetScreen(Frame& frame) override;

private:
	User* m_User;

	D3D11_BOX m_CropBox = {};

	ID3D11Device* m_lDevice = nullptr;
	ID3D11DeviceContext* m_lImmediateContext = nullptr;

	IDXGIOutputDuplication* m_lDeskDupl = nullptr;
	ID3D11Texture2D* m_lAcquiredDesktopImage = nullptr;
	ID3D11Texture2D* m_sharedTexture = nullptr;
	HANDLE m_sharedHandle = nullptr;

	DXGI_OUTPUT_DESC m_lOutputDesc = {};
	DXGI_OUTDUPL_DESC m_lOutputDuplDesc = {};

	void RecreateOutputDuplication();

	template<typename T>
	void SafeRelease(T*& ptr)
	{
		if (ptr)
		{
			ptr->Release();
			ptr = nullptr;
		}
	}
};

struct IGraphBuilder;
struct ICaptureGraphBuilder2;
struct IMediaControl;
struct IBaseFilter;
struct ISampleGrabber;

class CaptureCardCapture : public ScreenCapture
{
public:
	CaptureCardCapture(User* user, int deviceIndex);
	~CaptureCardCapture();

	bool GetScreen(Frame& frame) override;

private:
	User* m_User;
	int m_DeviceIndex;

	ID3D11Device* m_lDevice = nullptr;
	ID3D11DeviceContext* m_lImmediateContext = nullptr;

	ID3D11Texture2D* m_sharedTexture = nullptr;
	ID3D11Texture2D* m_stagingTexture = nullptr;
	HANDLE m_sharedHandle = nullptr;

	IGraphBuilder* m_pGraph = nullptr;
	ICaptureGraphBuilder2* m_pBuilder = nullptr;
	IMediaControl* m_pControl = nullptr;
	IBaseFilter* m_pCaptureFilter = nullptr;
	ISampleGrabber* m_pGrabber = nullptr;
	IBaseFilter* m_pGrabberFilter = nullptr;
	IBaseFilter* m_pNullRenderer = nullptr;

	BYTE* m_pFrameBuffer = nullptr;
	LONG m_FrameBufferSize = 0;
	int m_Width = 0;
	int m_Height = 0;
	int m_Stride = 0;
	D3D11_BOX m_CropBox = {};

	bool m_Initialized = false;
	void* m_callback = nullptr;

	template<typename T>
	void SafeRelease(T*& ptr)
	{
		if (ptr)
		{
			ptr->Release();
			ptr = nullptr;
		}
	}
};