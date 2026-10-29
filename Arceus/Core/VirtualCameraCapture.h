#pragma once
#include "ScreenCapture.h"
#include <d3d11.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <string>

class User;

class VirtualCameraCapture : public ScreenCapture
{
public:
	VirtualCameraCapture(User* user, int deviceIndex = 0);
	~VirtualCameraCapture();
	bool GetScreen(Frame& frame) override;
private:
	User* m_User;
	int m_width = 0, m_height = 0;
	IMFSourceReader* m_reader = nullptr;
	ID3D11Device* m_device = nullptr;
	ID3D11DeviceContext* m_context = nullptr;
	ID3D11Texture2D* m_sharedTexture = nullptr;
	ID3D11Texture2D* m_stagingTexture = nullptr;
	HANDLE m_sharedHandle = nullptr;

	void CleanupD3D();
	template<typename T>
	void SafeRelease(T*& ptr)
	{
		if (ptr) { ptr->Release(); ptr = nullptr; }
	}
};
