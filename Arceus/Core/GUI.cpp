#pragma once
#include "../Core/GUI.h"
#include "CrashLog.h"

#include "../Core/User.h"
#include "../Core/ResourceExtract.h"
#include "../Games/Game.h"
#include "../Utilities/Strings.hpp"
#include "../Utilities/Logger.hpp"

#include "../resource.h"

#include <Windows.h>
#include <thread>
#include <filesystem>
#include <string>

#include "../ImGui/imgui_impl_win32.h"
#include "../ImGui/imgui_impl_dx11.h"
#include "../ImGui/imgui_internal.h"
#include "../ImGui/imgui_freetype.h"
#include "../ImGui/custom_widgets.hpp"
#include "../ImGui/font.h"
#include "../ImGui/image.h"
#include "../ImGui/character_image.h"
#define STB_IMAGE_IMPLEMENTATION
#include "../ImGui/stb_image.h"

#include "../Utilities/Utilities.h"
#include "../Obfusk8/Instrumentation/materialization/state/Obfusk8Core.hpp"
#include "VirtualizerSDK.h"

#include <dshow.h>
#pragma comment(lib, "strmiids.lib")

static std::vector<std::string> EnumerateCaptureDevices()
{
	std::vector<std::string> devices;
	
	HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	bool needUninit = SUCCEEDED(hr);
	
	ICreateDevEnum* pDevEnum = nullptr;
	hr = CoCreateInstance(CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER,
		IID_ICreateDevEnum, (void**)&pDevEnum);
	
	if (SUCCEEDED(hr))
	{
		IEnumMoniker* pEnum = nullptr;
		hr = pDevEnum->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &pEnum, 0);
		
		if (hr == S_OK && pEnum)
		{
			IMoniker* pMoniker = nullptr;
			while (pEnum->Next(1, &pMoniker, NULL) == S_OK)
			{
				IPropertyBag* pPropBag = nullptr;
				hr = pMoniker->BindToStorage(0, 0, IID_IPropertyBag, (void**)&pPropBag);
				
				if (SUCCEEDED(hr))
				{
					VARIANT varName;
					VariantInit(&varName);
					hr = pPropBag->Read(L"FriendlyName", &varName, 0);
					
					if (SUCCEEDED(hr))
					{
						int len = WideCharToMultiByte(CP_UTF8, 0, varName.bstrVal, -1, NULL, 0, NULL, NULL);
						std::string name(len - 1, 0);
						WideCharToMultiByte(CP_UTF8, 0, varName.bstrVal, -1, &name[0], len, NULL, NULL);
						devices.push_back(name);
					}
					VariantClear(&varName);
					pPropBag->Release();
				}
				pMoniker->Release();
			}
			pEnum->Release();
		}
		pDevEnum->Release();
	}
	
	if (needUninit)
		CoUninitialize();
	
	if (devices.empty())
		devices.push_back("No capture devices found");

	return devices;
}

struct CaptureWindow
{
	HWND        hwnd;
	std::string title;
};

static std::vector<CaptureWindow> EnumerateCaptureWindows()
{
	std::vector<CaptureWindow> windows;

	EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL
	{
		auto* out = reinterpret_cast<std::vector<CaptureWindow>*>(lp);

		if (!IsWindowVisible(hwnd))                 return TRUE;
		if (GetWindow(hwnd, GW_OWNER) != nullptr)   return TRUE;

		LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
		if (exStyle & WS_EX_TOOLWINDOW)             return TRUE;

		DWORD pid = 0;
		GetWindowThreadProcessId(hwnd, &pid);
		if (pid == GetCurrentProcessId())           return TRUE;

		char title[256];
		int len = GetWindowTextA(hwnd, title, sizeof(title));
		if (len <= 0)                               return TRUE;

		RECT rc{};
		if (GetWindowRect(hwnd, &rc)) {
			if ((rc.right - rc.left) < 64 || (rc.bottom - rc.top) < 64)
				return TRUE;
		}

		out->push_back({ hwnd, std::string(title, len) });
		return TRUE;
	}, reinterpret_cast<LPARAM>(&windows));

	return windows;
}


static void RefreshObsWindows(User* user)
{
	std::vector<CaptureWindow> real = EnumerateCaptureWindows();


		? "No windows found - open your game first"
		: "Select an application");

	for (const auto& w : real)
	{
	}

	{
		{
			break;
		}
	}
}

namespace Combobox
{

	inline std::vector<std::string> aimHeightList =
	{
		SMenu::CBBChest.get().c_str(),
		SMenu::CBBNeck.get().c_str(),
		SMenu::CBBHead.get().c_str()
	};

	inline std::vector<std::string> inputMethodList =
	{
		"Driver Input",
		"Win32",
		"GHub",
		"Razer",
		"RPBox",
		"Makcu"
	};

	inline std::vector<std::string> gameList =
	{
		"Fortnite",
		"Valorant",
		"CS2",
		"Rainbow Six",
		"COD",
		"Warzone",
		"Rust",
		"Custom Game"
	};

	inline std::vector<std::string> captureMethodList =
	{
		"Desktop Duplication",
				"WinRT Capture",
				"Capture Card"
	};

	{
		"Circle",
		"Square"
	};
}

static int FilterNumbers(ImGuiInputTextCallbackData* data);
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace font
{
	ImFont* Icons = nullptr;
	ImFont* inter_semibold = nullptr;
}

Menu::Menu(bool& running, User* user)
	: m_Running(&running), m_User(user), m_isVisible(true), m_hwnd(nullptr)
{
	strcpy_s(m_User->Appearance.accentHex, "FFC800");
	m_Accent = ImColor(255, 200, 0);
	
	m_thread = std::thread(&Menu::Start, this);
	Logger::Progress(SMenu::LogCat, SMenu::LogMenuThread);
}

Menu::~Menu() 
{
	*m_Running = false;
	if (m_thread.joinable())
		m_thread.join();
	
	Destroy();
}

void Menu::Start() 
{
	CrashLog::Log(("THREAD GUI::Menu::Start tid=" + std::to_string(GetCurrentThreadId())).c_str());
VIRTUALIZER_MUTATE_ONLY_START

	bool keyWasPressed = false;
	std::chrono::steady_clock::time_point keyPressStartTime;
	const int holdDurationMs = 3000;

	while (*m_Running) 
	{
		bool keyIsPressed = (GetAsyncKeyState(m_User->Binds.toggleMenu) & 0x8000) != 0;

		if (keyIsPressed && !keyWasPressed)
		{
			keyPressStartTime = std::chrono::steady_clock::now();
			keyWasPressed = true;
		}
		else if (!keyIsPressed && keyWasPressed)
		{
			auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - keyPressStartTime
			).count();

			if (duration < holdDurationMs)
				m_isVisible = !m_isVisible;
			
			keyWasPressed = false;
		}
		else if (keyIsPressed && keyWasPressed)
		{
			auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - keyPressStartTime
			).count();

			if (duration >= holdDurationMs)
			{
				*m_Running = false;
				break;
			}
		}

VIRTUALIZER_MUTATE_ONLY_END

		if (m_isVisible)
		{
			if (m_hwnd == nullptr) 
			{
				MSG msg;
				while (::PeekMessage(&msg, m_hwnd, WM_QUIT, WM_QUIT, PM_REMOVE));
				if (!Create())
				{
					*m_Running = false;
					continue;
				}
			}

			MSG msg;
			while (::PeekMessage(&msg, m_hwnd, 0U, 0U, PM_REMOVE))
			{
				if (msg.message == WM_QUIT)
				{
					*m_Running = false;
					break;
				}
				::TranslateMessage(&msg);
				::DispatchMessage(&msg);
			}

			if (!*m_Running) continue;

			Render();
		}
		else {
			if (m_hwnd != nullptr)
				Destroy();

			Sleep(50);
		}
	}

	Destroy();
}

bool Menu::Create()
{
VIRTUALIZER_MUTATE_ONLY_START

	std::wstring RandomWClass(RandomWString(10));

	m_wc =
	{
		sizeof(m_wc),
		CS_CLASSDC,
		WndProc, 0L, 0L,
		GetModuleHandle(nullptr),
		LoadIconW(GetModuleHandle(nullptr), MAKEINTRESOURCEW(IDI_APP)), nullptr,
		nullptr, nullptr,
		RandomWClass.c_str(),
		LoadIconW(GetModuleHandle(nullptr), MAKEINTRESOURCEW(IDI_APP))
	};

VIRTUALIZER_MUTATE_ONLY_END
	if (!::RegisterClassExW(&m_wc))
		return false;
	Logger::Progress(SMenu::LogCat, SMenu::LogWindowClass);

VIRTUALIZER_MUTATE_ONLY_START
	{
		int scrW = ::GetSystemMetrics(SM_CXSCREEN);
		int scrH = ::GetSystemMetrics(SM_CYSCREEN);
		int winW = 1050, winH = 570;
		int winX = (scrW - winW) / 2;
		int winY = (scrH - winH) / 2;
		m_hwnd = ::CreateWindowExW(
			WS_EX_LAYERED,
			m_wc.lpszClassName,
			nullptr,
			WS_POPUP,
			winX, winY, winW, winH,
			nullptr, nullptr,
			m_wc.hInstance, nullptr);
	}

	::SetLayeredWindowAttributes(m_hwnd, RGB(0, 0, 0), 255, LWA_ALPHA);

	Logger::Progress(SMenu::LogCat, SMenu::LogWindowCreated);

	{
		HMODULE dwmapi = LoadLibraryA("dwmapi.dll");
		if (dwmapi) {
			typedef HRESULT(WINAPI* DwmSetWindowAttributeFn)(HWND, DWORD, LPCVOID, DWORD);
			typedef HRESULT(WINAPI* DwmExtendFrameIntoClientAreaFn)(HWND, const void*);
			
			auto pDwmSetWindowAttribute = (DwmSetWindowAttributeFn)GetProcAddress(dwmapi, "DwmSetWindowAttribute");
			auto pDwmExtendFrameIntoClientArea = (DwmExtendFrameIntoClientAreaFn)GetProcAddress(dwmapi, "DwmExtendFrameIntoClientArea");

			if (pDwmSetWindowAttribute) {
				int preference = 2;
				pDwmSetWindowAttribute(m_hwnd, 33, &preference, sizeof(preference));
			}

			if (pDwmExtendFrameIntoClientArea) {
				struct MARGINS { int cxLeftWidth; int cxRightWidth; int cyTopHeight; int cyBottomHeight; };
				MARGINS margins = { -1, -1, -1, -1 };
				pDwmExtendFrameIntoClientArea(m_hwnd, &margins);
			}
			FreeLibrary(dwmapi);
		}
	}

	HRGN region = CreateRoundRectRgn(0, 0, 1050, 570, 12, 12);
	SetWindowRgn(m_hwnd, region, TRUE);


	std::wstring().swap(RandomWClass);

VIRTUALIZER_MUTATE_ONLY_END

	if (!CreateDeviceD3D(m_hwnd)) 
	{
		Destroy();
		return false;
	}
	Logger::Progress(SMenu::LogCat, SMenu::LogD3DDevice);

VIRTUALIZER_MUTATE_ONLY_START

	::ShowWindow(m_hwnd, SW_SHOWDEFAULT);
	::SetForegroundWindow(m_hwnd);
	::UpdateWindow(m_hwnd);
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	io.IniFilename = NULL;

	Logger::Progress(SMenu::LogCat, SMenu::LogImGuiContext);

	ImFontConfig cfg;
	cfg.FontBuilderFlags = ImGuiFreeTypeBuilderFlags_ForceAutoHint | ImGuiFreeTypeBuilderFlags_LightHinting | ImGuiFreeTypeBuilderFlags_LoadColor;
	cfg.FontDataOwnedByAtlas = false;
	font::inter_semibold = io.Fonts->AddFontFromMemoryTTF(inter_semibold, sizeof(inter_semibold), 15.f, &cfg, io.Fonts->GetGlyphRangesCyrillic());
	font::Icons = io.Fonts->AddFontFromMemoryTTF(Icons, sizeof(Icons), 17.f, &cfg, io.Fonts->GetGlyphRangesCyrillic());
	Logger::Progress(SMenu::LogCat, SMenu::LogFontsLoaded);

	ImGui_ImplWin32_Init(m_hwnd);
	{
		ImGuiIO& io = ImGui::GetIO();
		io.ClipboardUserData = (void*)m_hwnd;
		io.GetClipboardTextFn = [](void* user_data) -> const char*
		{
			HWND hwnd = (HWND)user_data;
			if (!::OpenClipboard(hwnd))
				return "";
			HANDLE hData = ::GetClipboardData(CF_UNICODETEXT);
			if (!hData) { ::CloseClipboard(); return ""; }
			static std::string buf;
			buf.clear();
			if (const WCHAR* w = (const WCHAR*)::GlobalLock(hData))
			{
				int len = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
				if (len > 0) { buf.resize(len); ::WideCharToMultiByte(CP_UTF8, 0, w, -1, &buf[0], len, NULL, NULL); }
				::GlobalUnlock(hData);
			}
			::CloseClipboard();
			return buf.c_str();
		};
		io.SetClipboardTextFn = [](void* user_data, const char* text)
		{
			HWND hwnd = (HWND)user_data;
			if (!::OpenClipboard(hwnd))
				return;
			int wlen = ::MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
			HGLOBAL hg = ::GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)wlen * sizeof(WCHAR));
			if (!hg) { ::CloseClipboard(); return; }
			WCHAR* w = (WCHAR*)::GlobalLock(hg);
			if (w) { ::MultiByteToWideChar(CP_UTF8, 0, text, -1, w, wlen); ::GlobalUnlock(hg); }
			::EmptyClipboard();
			if (!::SetClipboardData(CF_UNICODETEXT, hg))
				::GlobalFree(hg);
			::CloseClipboard();
		};
	}
	ImGui_ImplDX11_Init(m_pd3dDevice, m_pd3dDeviceContext);
	Logger::Progress(SMenu::LogCat, SMenu::LogImGuiBackends);

VIRTUALIZER_MUTATE_ONLY_END

	if (!LoadTextureFromMemory(logoData, sizeof(logoData), &m_LogoResource))
		return false;

	if (!LoadTextureFromMemory(characterData, sizeof(characterData), &m_CharacterResource, &m_CharWidth, &m_CharHeight))
		return false;

	return true;
}

void Menu::Destroy()
{
	if (m_hwnd == nullptr) 
		return;

VIRTUALIZER_MUTATE_ONLY_START

	if (m_LogoResource) { m_LogoResource->Release(); m_LogoResource = nullptr; }
	if (m_CharacterResource) { m_CharacterResource->Release(); m_CharacterResource = nullptr; }
	ImGui::GetIO().Fonts->Clear();
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	CleanupDeviceD3D();
	::DestroyWindow(m_hwnd);
	::UnregisterClassW(m_wc.lpszClassName, m_wc.hInstance);

	m_hwnd = nullptr;
	memset(&m_wc, 0, sizeof(m_wc));

VIRTUALIZER_MUTATE_ONLY_END
}

OBF_METHOD(void, Menu::Render, (), {
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	Security::CheckSecurity();

	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->Pos);
	ImGui::SetNextWindowSize(ImVec2(1050.f, viewport->Size.y));

	{
		c::bg::background = ImLerp(c::bg::background, ImColor(10, 10, 10, 180), ImGui::GetIO().DeltaTime * 12.f);
		c::separator = ImLerp(c::separator, ImColor(40, 35, 5, 150), ImGui::GetIO().DeltaTime * 12.f);

		c::accent = ImLerp(c::accent, m_Accent, ImGui::GetIO().DeltaTime * 12.f);

		c::elements::background_hovered = ImLerp(c::elements::background_hovered, ImColor(60, 50, 5, 220), ImGui::GetIO().DeltaTime * 25.f);
		c::elements::background = ImLerp(c::elements::background, ImColor(30, 25, 5, 180), ImGui::GetIO().DeltaTime * 25.f);

		c::checkbox::mark = ImLerp(c::checkbox::mark, ImColor(255, 200, 0, 255), ImGui::GetIO().DeltaTime * 12.f);

		c::child::background = ImLerp(c::child::background, ImColor(15, 13, 3, 200), ImGui::GetIO().DeltaTime * 12.f);
		c::child::cap = ImLerp(c::child::cap, ImColor(28, 24, 5, 220), ImGui::GetIO().DeltaTime * 12.f);

		c::page::text_hov = ImLerp(c::page::text_hov, ImColor(255, 230, 100), ImGui::GetIO().DeltaTime * 12.f);
		c::page::text = ImLerp(c::page::text, ImColor(200, 170, 80), ImGui::GetIO().DeltaTime * 12.f);

		c::page::background_active = ImLerp(c::page::background_active, ImColor(50, 42, 5, 200), ImGui::GetIO().DeltaTime * 25.f);
		c::page::background = ImLerp(c::page::background, ImColor(22, 18, 3, 150), ImGui::GetIO().DeltaTime * 25.f);

		c::text::text_active = ImLerp(c::text::text_active, ImColor(255, 255, 255), ImGui::GetIO().DeltaTime * 12.f);
		c::text::text_hov = ImLerp(c::text::text_hov, ImColor(255, 220, 100), ImGui::GetIO().DeltaTime * 12.f);
		c::text::text = ImLerp(c::text::text, ImColor(200, 170, 80), ImGui::GetIO().DeltaTime * 12.f);
	}

	if (m_isVisible)
	{
		switch (m_User->Internal.appState)
		{
		case AppState::Loading:
			DrawLoading();
			break;
		case AppState::StartMenu:
			DrawStartMenu();
			break;
		case AppState::Running:
			DrawMenu();
			break;
		}
	}

	ImGui::Render();
	const float clear_color_with_alpha[4] = { 0.f, 0.f, 0.f, 0.f };
	m_pd3dDeviceContext->OMSetRenderTargets(1, &m_mainRenderTargetView, nullptr);
	m_pd3dDeviceContext->ClearRenderTargetView(m_mainRenderTargetView, clear_color_with_alpha);
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	m_pSwapChain->Present(1, 0);
})

void Menu::DrawMenu()
{ 
	ImGui::Begin("1", m_Running,
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoResize);

	ImGuiStyle* style = &ImGui::GetStyle();

	const ImVec2& pos = ImGui::GetWindowPos();
	const ImVec2& region = ImGui::GetContentRegionMax();
	const ImVec2& spacing = style->ItemSpacing;

	style->WindowPadding = ImVec2(0, 0);
	style->ItemSpacing = ImVec2(20, 20);
	style->WindowBorderSize = 0;
	style->WindowRounding = 12.f;
	style->FrameRounding = 6.f;
	style->GrabRounding = 6.f;
	style->ScrollbarSize = 10.f;
	style->ScrollbarRounding = 6.f;

	float hue = fmodf(ImGui::GetTime() * 0.1f, 1.0f);
	ImColor borderColor = ImColor::HSV(hue, 0.8f, 1.0f, 1.0f);
	GetWindowDrawList()->AddRect(pos + ImVec2(4, 4), pos + ImVec2(region.x - 4, region.y - 4), borderColor, c::bg::rounding, ImDrawFlags_RoundCornersAll, 4.0f);

	GetWindowDrawList()->AddRectFilled(pos, pos + ImVec2(region), ImGui::GetColorU32(c::bg::background), c::bg::rounding);

	if (m_CharacterResource && m_CharHeight > 0)
	{
		float img_h = region.y * 0.85f;
		float img_w = img_h * ((float)m_CharWidth / m_CharHeight);
		ImVec2 img_pos = pos + ImVec2(region.x - img_w - 110, pos.y + region.y - img_h - 90);
		GetWindowDrawList()->AddImage(m_CharacterResource, img_pos, img_pos + ImVec2(img_w, img_h));
	}

	SetCursorPos(ImVec2(spacing.x, 10));
	custom::BeginGroup();
	ImGui::BeginChild("LeftContent", ImVec2(547.f, region.y - 80.f), false);

	m_TabAlpha = ImClamp(m_TabAlpha + (4.f * ImGui::GetIO().DeltaTime * (m_Page == m_ActiveTab ? 1.f : -1.f)), 0.f, 1.f);
	if (m_TabAlpha == 0.f && m_TabAdd == 0.f) m_ActiveTab = m_Page;
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, m_TabAlpha * style->Alpha);

	switch (m_ActiveTab)
	{
	case 0:
	{
		ImGui::SetCursorPosX(spacing.x);
		custom::BeginGroup();
		{
			custom::Child(SMenu::HipFireTab.get().c_str(), ImVec2((GetContentRegionMax().x - spacing.x * 3) / 2, 200.f), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
			{
				float f = m_User->HipFire.strength * 100.0f;
				custom::SliderFloat(SMenu::Strength.get().c_str(), &f, 1.0f, 100.0f, 1.0f, "%.0f%%");
				m_User->HipFire.strength = f / 100.0f;

				custom::Separator_line();

				if (custom::Combo(SMenu::AimHeight.get().c_str(), reinterpret_cast<int*>(&m_User->HipFire.aimHeightChoice), Combobox::aimHeightList, Combobox::aimHeightList.size()))
					m_User->HipFire.aimHeight = AIMH2FLOAT.at(m_User->HipFire.aimHeightChoice);

				custom::Separator_line();

				custom::SliderInt(SMenu::FOV.get().c_str(), &m_User->HipFire.fov, 1, m_User->Network.size, 1, "%dpx");

				custom::Separator_line();

				custom::SliderInt("Deadzone", &m_User->HipFire.deadzone, 0, 10, 1, "%dpx");
			}
			custom::EndChild();
		}
		custom::EndGroup();

		ImGui::SameLine();

		custom::BeginGroup();
		{
			custom::Child(SMenu::AdsTab.get().c_str(), ImVec2((GetContentRegionMax().x - spacing.x * 3) / 2, 200.f), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
			{
				float f = m_User->Ads.strength * 100.0f;
				custom::SliderFloat(SMenu::Strength.get().c_str(), &f, 1.0f, 100.0f, 1.0f, "%.0f%%");
				m_User->Ads.strength = f / 100.0f;

				custom::Separator_line();

				if (custom::Combo(SMenu::AimHeight.get().c_str(), reinterpret_cast<int*>(&m_User->Ads.aimHeightChoice), Combobox::aimHeightList, Combobox::aimHeightList.size()))
					m_User->Ads.aimHeight = AIMH2FLOAT.at(m_User->Ads.aimHeightChoice);

				custom::Separator_line();

				custom::SliderInt(SMenu::FOV.get().c_str(), &m_User->Ads.fov, 1, m_User->Network.size, 1, "%dpx");

				custom::Separator_line();

				custom::SliderInt("Deadzone", &m_User->Ads.deadzone, 0, 10, 1, "%dpx");
			}
			custom::EndChild();
		}
		custom::EndGroup();

		ImGui::SetCursorPosX(spacing.x);
		custom::BeginGroup();
		{
			custom::Child(SMenu::TriggerTab.get().c_str(), ImVec2((GetContentRegionMax().x - spacing.x * 3) / 2, ((GetContentRegionMax().y - (spacing.y * 2)) + 10) / 2 - 10), true);
			{
				custom::Checkbox(SMenu::Trigger.get().c_str(), &m_User->Trigger.enabled);

				custom::Separator_line();

				custom::KnobInt(SMenu::ProximityControl.get().c_str(), &m_User->Trigger.proximity, 0, 50, 1, "%dpx");
			}
			custom::EndChild();
		}
		custom::EndGroup();

		ImGui::SameLine();

		custom::BeginGroup();
		{
			custom::Child("Prediction", ImVec2((GetContentRegionMax().x - spacing.x * 3) / 2, ((GetContentRegionMax().y - (spacing.y * 2)) + 10) / 2 - 10), true);
			{
				custom::Checkbox("Prediction", &m_User->Prediction.enabled);

				custom::Separator_line();

				custom::SliderFloat("Strength", &m_User->Prediction.strength, 0.5f, 5.0f, 0.1f, "%.1f");
			}
			custom::EndChild();
		}
		custom::EndGroup();

		break;
	}
	case 1:
	{
		ImGui::SetCursorPosX(spacing.x);
		custom::BeginGroup();
		{
			custom::Child(SMenu::AITab.get().c_str(), ImVec2((GetContentRegionMax().x - spacing.x * 3) / 2, ((GetContentRegionMax().y - (spacing.y * 2)) + 10) / 2 - 10), true);
			{
				float f = m_User->Network.confidence * 100.0f;
				if (custom::SliderFloat(SMenu::ConfidenceThreshold.get().c_str(), &f, 0.01f, 99.0f, 0.01f, "%.2f%%"))
					m_User->Network.confidence = f / 100.0f;

				custom::Separator_line();
			}
			custom::EndChild();
		}
		custom::EndGroup();

		ImGui::SameLine();

		custom::BeginGroup();
		{
			custom::Child("Overlay", ImVec2((GetContentRegionMax().x - spacing.x * 3) / 2, ((GetContentRegionMax().y - (spacing.y * 2)) + 10) / 2 - 10), true);
			{
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Shows a white FOV circle overlay on screen. Switches between HipFire and ADS FOV based on right-click.");

				{
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Circle = radial FOV, Square = box FOV (same size).");

				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Shows detection boxes on screen.");

				custom::Separator_line();

			custom::SliderFloat("Left Mult (x)", &m_User->Network.thirdPersonMultiplier, 1.0f, 20.0f, 1.0f, "%.1fx");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Multiplies left-side distance to ignore local player (third-person). Higher = more ignore.");

				custom::Separator_line();

				custom::Checkbox("Anti Recoil", &m_User->Recoil.enabled);
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Moves mouse down while holding the bind key to compensate for recoil.");

				custom::SliderFloat("Recoil Strength", &m_User->Recoil.strength, 0.01f, 10.0f, 0.01f, "%.2fpx");
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Pixels per frame to pull down. Higher = faster compensation.");

				custom::Keybind("Recoil Key", &m_User->Recoil.bind);
			}
			custom::EndChild();
		}
		custom::EndGroup();
		break;
	}
	case 2:
	{
		ImGui::SetCursorPosX(spacing.x);
		custom::BeginGroup();
		{
			custom::Child(SMenu::KeybindsTab.get().c_str(), ImVec2((GetContentRegionMax().x - spacing.x * 3) / 2, (GetContentRegionMax().y - (spacing.y * 2)) + 10), true);
			{
				custom::Keybind(SMenu::HoldAim.get().c_str(), &m_User->Binds.aim);

				custom::Separator_line();

				if (custom::Keybind(SMenu::ToggleAim.get().c_str(), &m_User->Binds.toggleAim))
					m_User->Internal.toggledAim = false;

				custom::Separator_line();

				custom::Keybind(SMenu::HideMenu.get().c_str(), &m_User->Binds.toggleMenu);

				custom::Separator_line();

				custom::Keybind(SMenu::AdsBind.get().c_str(), &m_User->Binds.ads);

				custom::Separator_line();

				custom::Keybind("Trigger Key", &m_User->Trigger.bind);
			}
			custom::EndChild();
		}
		custom::EndGroup();

		ImGui::SameLine();

		custom::BeginGroup();
		{
			custom::Child(SMenu::SensitivityTab.get().c_str(), ImVec2((GetContentRegionMax().x - spacing.x * 3) / 2, ((GetContentRegionMax().y - (spacing.y * 2)) + 10) / 2 - 10), true);
			{
				if (custom::SliderFloat(SMenu::XAndY.get().c_str(), &m_User->HipFire.sens, 1.0f, 100.0f, 0.1f, "%.1f%%"))
				{
					m_User->Internal.HF360 = Game::GetHF360(m_User->HipFire.sens);
					m_User->Internal.ADS360 = Game::GetADS360(m_User->HipFire.sens, m_User->Ads.sens);
				}

				custom::Separator_line();
				custom::Separator_line();

				if (custom::SliderFloat(SMenu::AdsSens.get().c_str(), &m_User->Ads.sens, 1.0f, 100.0f, 0.1f, "%.1f%%"))
					m_User->Internal.ADS360 = Game::GetADS360(m_User->HipFire.sens, m_User->Ads.sens);
			}
			custom::EndChild();

			custom::Child("Appearance", ImVec2((GetContentRegionMax().x - spacing.x * 3) / 2, ((GetContentRegionMax().y - (spacing.y * 2)) + 10) / 2 - 10), true);
			{
				ImGui::PushItemWidth(100);
				if (ImGui::InputText("Accent #", m_User->Appearance.accentHex, 7, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase))
				{
					unsigned int hex = 0;
					if (sscanf_s(m_User->Appearance.accentHex, "%X", &hex) == 1)
					{
						float r = ((hex >> 16) & 0xFF) / 255.0f;
						float g = ((hex >> 8) & 0xFF) / 255.0f;
						float b = (hex & 0xFF) / 255.0f;
						m_Accent = ImColor(r, g, b);
					}
				}
				ImGui::PopItemWidth();
			}
			custom::EndChild();
		}
		custom::EndGroup();
		break;
	}
	case 3:
	{
		ImGui::SetCursorPosX(spacing.x);
		custom::BeginGroup();
		{
			custom::Child(SMenu::InformationTab.get().c_str(), ImVec2((GetContentRegionMax().x - spacing.x * 2), (GetContentRegionMax().y - (spacing.y * 2)) + 10), true);
			{
				custom::Text(SMenu::Screenshot.get().c_str(), m_User->Internal.screenshotMs);
				custom::Text(SMenu::Inference.get().c_str(), m_User->Internal.inferenceMs);
				custom::Text(SMenu::Processing.get().c_str(), m_User->Internal.processingMs);
				custom::Text(SMenu::Resolution.get().c_str(), m_User->Internal.ResolutionW, m_User->Internal.ResolutionH);
				custom::Text(SGame::Version.get().c_str());
				custom::Text(SMenu::Version.get().c_str());
			}
			custom::EndChild();
		}
		custom::EndGroup();
		break;
	}
	}

	ImGui::PopStyleVar();

	ImGui::EndChild();
	custom::EndGroup();

	SetCursorPos(ImVec2(spacing.x, region.y - (60 + spacing.y)));

	custom::BeginGroup();
	{
		float child_w = 52.f;
		float tab_w = 210.f;
		float left_x = spacing.x;
		float center_x = (region.x - tab_w) / 2.f;
		float right_x = region.x - spacing.x - child_w;

#ifndef CHINA
		ImGui::SetCursorPosX(left_x);
		custom::Child("1", ImVec2(child_w, 60), false);
		{
			if (custom::Button("i", ImVec2((ImGui::GetContentRegionMax().x - spacing.x * 1) / 1, ImGui::GetContentRegionMax().y - spacing.y)))
				OpenDiscord();
		}
		custom::EndChild();
#else
		ImGui::SetCursorPosX(left_x);
		ImGui::Dummy(ImVec2(child_w, 0));
#endif
		ImGui::SameLine();
		ImGui::SetCursorPosX(center_x - spacing.x - 2.f);
		custom::SeparatorEx(ImGuiSeparatorFlags_Vertical, 2.f);
		ImGui::SameLine();
		ImGui::SetCursorPosX(center_x);

		custom::Child("2", ImVec2(tab_w, 60), false);
		{
			if (custom::Page(0 == m_Page, "a", ImVec2((ImGui::GetContentRegionMax().x - spacing.x * 4) / 4, ImGui::GetContentRegionMax().y - spacing.y))) m_Page = 0;
			SameLine();
			if (custom::Page(1 == m_Page, "b", ImVec2((ImGui::GetContentRegionMax().x - spacing.x * 4) / 4, ImGui::GetContentRegionMax().y - spacing.y))) m_Page = 1;
			SameLine();
			if (custom::Page(2 == m_Page, "c", ImVec2((ImGui::GetContentRegionMax().x - spacing.x * 4) / 4, ImGui::GetContentRegionMax().y - spacing.y))) m_Page = 2;
			SameLine();
			if (custom::Page(3 == m_Page, "d", ImVec2((ImGui::GetContentRegionMax().x - spacing.x * 4) / 4, ImGui::GetContentRegionMax().y - spacing.y))) m_Page = 3;
		}
		custom::EndChild();

		ImGui::SameLine();
		ImGui::SetCursorPosX(right_x - spacing.x - 2.f);
		custom::SeparatorEx(ImGuiSeparatorFlags_Vertical, 2.f);
		ImGui::SameLine();
		ImGui::SetCursorPosX(right_x);

		custom::Child("3", ImVec2(child_w, 60), false);
		{
			if (custom::Button("h", ImVec2((ImGui::GetContentRegionMax().x - spacing.x * 1) / 1, ImGui::GetContentRegionMax().y - spacing.y)))
				*m_Running = false;
		}
		custom::EndChild();
	}
	custom::EndGroup();

	ImGui::End();
}

void Menu::DrawStartMenu()
{
	ImGui::Begin("1", m_Running,
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoResize);

	ImGuiStyle* style = &ImGui::GetStyle();

	const ImVec2& pos = ImGui::GetWindowPos();
	const ImVec2& region = ImGui::GetContentRegionMax();
	const ImVec2& spacing = style->ItemSpacing;

	style->WindowPadding = ImVec2(0, 0);
	style->ItemSpacing = ImVec2(20, 20);
	style->WindowBorderSize = 0;
	style->WindowRounding = 12.f;
	style->FrameRounding = 6.f;
	style->GrabRounding = 6.f;
	style->ScrollbarSize = 10.f;
	style->ScrollbarRounding = 6.f;

	float hue = fmodf(ImGui::GetTime() * 0.1f, 1.0f);
	ImColor borderColor = ImColor::HSV(hue, 0.8f, 1.0f, 1.0f);
	GetWindowDrawList()->AddRect(pos + ImVec2(4, 4), pos + ImVec2(region.x - 4, region.y - 4), borderColor, c::bg::rounding, ImDrawFlags_RoundCornersAll, 4.0f);

	GetWindowDrawList()->AddRectFilled(pos, pos + ImVec2(region), ImGui::GetColorU32(c::bg::background), c::bg::rounding);

	if (m_CharacterResource && m_CharHeight > 0)
	{
		float img_h = region.y * 0.85f;
		float img_w = img_h * ((float)m_CharWidth / m_CharHeight);
		ImVec2 img_pos = pos + ImVec2(region.x - img_w - 110, pos.y + region.y - img_h - 90);
		GetWindowDrawList()->AddImage(m_CharacterResource, img_pos, img_pos + ImVec2(img_w, img_h));
	}

	SetCursorPos(ImVec2(spacing.x, 10));
	
	custom::BeginGroup();
	ImGui::BeginChild("LeftContent", ImVec2(547.f, region.y - 80.f), false);
	
	ImGui::SetCursorPosX(spacing.x);
	custom::BeginGroup();
	{
		custom::Child(SMenu::StartMenu.get().c_str(), ImVec2((GetContentRegionMax().x - spacing.x * 2), (GetContentRegionMax().y - (spacing.y * 2)) + 10), true);
		{
			int gameIdx = static_cast<int>(m_User->Input.game);
			if (custom::Combo("Game", &gameIdx, Combobox::gameList, static_cast<int>(Combobox::gameList.size())))
			{
				m_User->SwitchGame(static_cast<GameChoice>(gameIdx));
			}
			if (m_User->Input.game == GameChoice::Custom)
			{
				char buf[128] = {};
				strcpy_s(buf, m_User->Input.customGameName.c_str());
				ImGui::SetNextItemWidth(200);
				if (ImGui::InputText("Window Name", buf, sizeof(buf)))
					m_User->Input.customGameName = buf;
			}
			custom::Separator_line();

			int inputMethodIdx = static_cast<int>(m_User->Input.method);
			if (custom::Combo("Input Method", &inputMethodIdx, Combobox::inputMethodList, static_cast<int>(Combobox::inputMethodList.size())))
			{
				m_User->Input.method = static_cast<InputMethod>(inputMethodIdx);
			}
			custom::Separator_line();

			int captureMethodIdx = static_cast<int>(m_User->Input.capture);
			if (custom::Combo("Capture Method", &captureMethodIdx, Combobox::captureMethodList, static_cast<int>(Combobox::captureMethodList.size())))
			{
				m_User->Input.capture = static_cast<CaptureMethod>(captureMethodIdx);
			}

				{
					{
					}
					else
					{
					}
				}
			}

			if (m_User->Input.capture == CaptureMethod::CaptureCard)
			{
				static std::vector<std::string> captureDevices = EnumerateCaptureDevices();
				static int captureDeviceIdx = m_User->Input.captureCardDeviceIndex;
				if (captureDeviceIdx >= static_cast<int>(captureDevices.size()))
					captureDeviceIdx = 0;
				if (custom::Combo("Capture Device", &captureDeviceIdx, captureDevices, static_cast<int>(captureDevices.size())))
					m_User->Input.captureCardDeviceIndex = captureDeviceIdx;
			}

			custom::Separator_line();

			if (!m_User->Network.availableModels.empty())
			{
				if (custom::Combo(SMenu::ModelChoice.get().c_str(), &m_User->Network.modelChoice, m_User->Network.availableModels, static_cast<int>(m_User->Network.availableModels.size())))
				{
					std::filesystem::path runtimeDir = ResourceExtract::GetRuntimeDir();
					std::filesystem::path exeDir = std::filesystem::current_path();
					std::string fname = m_User->Network.availableModels[m_User->Network.modelChoice];
					std::filesystem::path fp = runtimeDir / fname;
					if (!std::filesystem::exists(fp))
						fp = exeDir / fname;
					m_User->Network.modelPath = fp.string();
				}
			}
			else
			{
				custom::Text("No .mdl models found in directory");
			}
			custom::Separator();
		}
		custom::EndChild();
	}
	custom::EndGroup();

	ImGui::EndChild();
	custom::EndGroup();

	SetCursorPos(ImVec2(spacing.x, region.y - (60 + spacing.y)));

	custom::BeginGroup();
	{

#ifndef CHINA
		custom::Child("1", ImVec2((GetContentRegionMax().x - (spacing.x * 6)) / 8, 60), false);
		{
			if (custom::Button("i", ImVec2((GetContentRegionMax().x - spacing.x * 1) / 1, GetContentRegionMax().y - spacing.y)))
				OpenDiscord();
		}
		custom::EndChild();
#else
		ImGui::Dummy(ImVec2((GetContentRegionMax().x - (spacing.x * 6)) / 8, 0));
#endif

		ImGui::SameLine();
		ImGui::Dummy(ImVec2((GetContentRegionMax().x - spacing.x * 6) / 12, 0));
		ImGui::SameLine();
		custom::SeparatorEx(ImGuiSeparatorFlags_Vertical, 2.f);
		ImGui::SameLine();

		custom::Child("2", ImVec2((GetContentRegionMax().x - (spacing.x * 6)) / 2, 60), false);
		{
			ImGui::Dummy(ImVec2((GetContentRegionMax().x - spacing.x * 4) / 4, 0));
			SameLine();
			if (custom::Button("e", ImVec2((GetContentRegionMax().x - (spacing.x * 2)) / 2, GetContentRegionMax().y - spacing.y)))
			{
				m_User->Internal.appState = AppState::Loading;
			}
		}
		custom::EndChild();

		ImGui::SameLine();
		custom::SeparatorEx(ImGuiSeparatorFlags_Vertical, 2.f);
		ImGui::SameLine();
		ImGui::Dummy(ImVec2((GetContentRegionMax().x - spacing.x * 6) / 12, 0));
		SameLine();

		custom::Child("3", ImVec2((GetContentRegionMax().x - (spacing.x * 6)) / 8, 60), false);
		{
			if (custom::Button("h", ImVec2((GetContentRegionMax().x - spacing.x * 1) / 1, GetContentRegionMax().y - spacing.y)))
			{
				m_User->Internal.appState = AppState::Loading;
				*m_Running = false;
			}
		}
		custom::EndChild();
	}
	custom::EndGroup();

	ImGui::End();
}

void Menu::DrawLoading()
{
	ImGui::Begin("0", m_Running,
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse);

	ImGuiStyle* style = &ImGui::GetStyle();
	style->WindowPadding = ImVec2(0, 0);
	style->ItemSpacing = ImVec2(20, 20);
	style->WindowBorderSize = 0;
	style->WindowRounding = 12.f;
	style->FrameRounding = 6.f;
	style->GrabRounding = 6.f;
	style->ScrollbarSize = 10.f;
	style->ScrollbarRounding = 6.f;

	ImVec2 pos = ImGui::GetWindowPos();
	ImVec2 size = ImGui::GetWindowSize();
	ImVec2 center = ImVec2(size.x * 0.5f, size.y * 0.5f);

	float hue = fmodf(ImGui::GetTime() * 0.1f, 1.0f);
	ImColor borderColor = ImColor::HSV(hue, 0.8f, 1.0f, 1.0f);
	ImGui::GetWindowDrawList()->AddRect(pos + ImVec2(4, 4), pos + size - ImVec2(4, 4), borderColor, c::bg::rounding, ImDrawFlags_RoundCornersAll, 4.0f);

	ImGui::GetWindowDrawList()->AddRectFilled(
		pos, pos + size, ImGui::GetColorU32(c::bg::background), c::bg::rounding);

	ImGui::SetCursorPos(ImVec2(center.x - 50, center.y - 50));
	custom::Spinner("", 50, 10, ImGui::GetColorU32(m_Accent));

	ImGui::End();
}

void Menu::OpenDiscord()
{
	ShellExecuteA(nullptr, "open", SMenu::Discord.get().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

bool Menu::LoadTextureFromMemory(const void* data, size_t data_size, ID3D11ShaderResourceView** out_srv, int* out_width, int* out_height)
{
VIRTUALIZER_MUTATE_ONLY_START
	bool success = false;

	int image_width = 0;
	int image_height = 0;
	unsigned char* image_data = stbi_load_from_memory((const unsigned char*)data, (int)data_size, &image_width, &image_height, NULL, 4);
	
	if (out_width) *out_width = image_width;
	if (out_height) *out_height = image_height;

	if (image_data != NULL)
	{
		D3D11_TEXTURE2D_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.Width = image_width;
		desc.Height = image_height;
		desc.MipLevels = 0;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		desc.CPUAccessFlags = 0;
		desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

		ID3D11Texture2D* pTexture = NULL;
		HRESULT hr = m_pd3dDevice->CreateTexture2D(&desc, NULL, &pTexture);
		
		if (SUCCEEDED(hr))
		{
			m_pd3dDeviceContext->UpdateSubresource(pTexture, 0, NULL, image_data, desc.Width * 4, 0);
			
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
			ZeroMemory(&srvDesc, sizeof(srvDesc));
			srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = -1;
			srvDesc.Texture2D.MostDetailedMip = 0;

			hr = m_pd3dDevice->CreateShaderResourceView(pTexture, &srvDesc, out_srv);
			
			if (SUCCEEDED(hr)) 
			{
				m_pd3dDeviceContext->GenerateMips(*out_srv);
				success = true;
			}
			
			pTexture->Release();
		}
		stbi_image_free(image_data);
	}

VIRTUALIZER_MUTATE_ONLY_END
	return success;
}

bool Menu::CreateDeviceD3D(HWND hWnd)
{
VIRTUALIZER_MUTATE_ONLY_START

	DXGI_SWAP_CHAIN_DESC sd;
	ZeroMemory(&sd, sizeof(sd));
	sd.BufferCount = 2;
	sd.BufferDesc.Width = 0;
	sd.BufferDesc.Height = 0;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | D3D11_CREATE_DEVICE_DEBUG;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = hWnd;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	UINT createDeviceFlags = 0;
	D3D_FEATURE_LEVEL featureLevel;
	const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
	HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &m_pSwapChain, &m_pd3dDevice, &featureLevel, &m_pd3dDeviceContext);
	if (res == DXGI_ERROR_UNSUPPORTED)
		res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &m_pSwapChain, &m_pd3dDevice, &featureLevel, &m_pd3dDeviceContext);

VIRTUALIZER_MUTATE_ONLY_END
	
	if (res != S_OK)
			return false;

	CreateRenderTarget();
	return true;

}

void Menu::CleanupDeviceD3D()
{
	CleanupRenderTarget();
	if (m_pSwapChain) { m_pSwapChain->Release(); m_pSwapChain = nullptr; }
	if (m_pd3dDeviceContext) { m_pd3dDeviceContext->Release(); m_pd3dDeviceContext = nullptr; }
	if (m_pd3dDevice) { m_pd3dDevice->Release(); m_pd3dDevice = nullptr; }
}

void Menu::CreateRenderTarget()
{
	ID3D11Texture2D* pBackBuffer;
	m_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
	m_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &m_mainRenderTargetView);
	pBackBuffer->Release();
}

void Menu::CleanupRenderTarget()
{
	if (m_mainRenderTargetView) { m_mainRenderTargetView->Release(); m_mainRenderTargetView = nullptr; }
}

static int FilterNumbers(ImGuiInputTextCallbackData* data)
{
	ImWchar c = data->EventChar;

	if (!(c >= '0' && c <= '9'))
		return 1;

	return 0;
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return true;

	switch (msg)
	{
		case WM_SYSCOMMAND:
			if ((wParam & 0xfff0) == SC_KEYMENU)
				return 0;
			break;
		case WM_DESTROY:
			::PostQuitMessage(0);
			return 0;
		case WM_NCHITTEST:
		{
			constexpr int TITLEBAR = 75;
			constexpr int BUTTONS = 0;
			constexpr int BORDER = 4;

			RECT clientRect;
			GetClientRect(hWnd, &clientRect);
			POINT clientPoint{ LOWORD(lParam), HIWORD(lParam) };
			ScreenToClient(hWnd, &clientPoint);

			if (clientPoint.y <= TITLEBAR && clientPoint.x < (clientRect.right - BUTTONS)) return HTCAPTION;
			return HTCLIENT;
		}
	}

	return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
