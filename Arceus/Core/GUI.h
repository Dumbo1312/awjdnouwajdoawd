#pragma once
#include <d3d11.h>
#include <string>
#include <thread>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "../ImGui/imgui.h"
class User;

class Menu 
{
public:
	Menu(bool& running, User* user);

	void Start();

	~Menu();

private:
	bool* m_Running = nullptr;
	User* m_User = nullptr;
	bool m_isVisible = true;
	std::thread m_thread;

	ImVec4 m_Accent = ImColor(191, 0, 255);
	int m_ActiveTab = 0;
	int m_Page = 0;
	float m_TabAlpha = 0.f;
	float m_TabAdd = 0.f;

	WNDCLASSEXW m_wc = { };
	HWND m_hwnd = nullptr;

	bool Create();
	void Destroy();
	void Render();

	void DrawMenu();
	void DrawStartMenu();
	void DrawLoading();
	void OpenDiscord();
	ID3D11ShaderResourceView* m_LogoResource = nullptr;
	ID3D11ShaderResourceView* m_CharacterResource = nullptr;
	int m_CharWidth = 0, m_CharHeight = 0;

	bool LoadTextureFromMemory(const void* data, size_t data_size, ID3D11ShaderResourceView** out_srv, int* out_width = nullptr, int* out_height = nullptr);
	ID3D11Device* m_pd3dDevice = nullptr;
	ID3D11DeviceContext* m_pd3dDeviceContext = nullptr;
	IDXGISwapChain* m_pSwapChain = nullptr;
	ID3D11RenderTargetView* m_mainRenderTargetView = nullptr;

	bool CreateDeviceD3D(HWND hWnd);
	void CleanupDeviceD3D();
	void CreateRenderTarget();
	void CleanupRenderTarget();
};