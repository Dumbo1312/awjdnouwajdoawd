#include "App.h"
#include "CrashLog.h"
#include "../Obfusk8/Instrumentation/materialization/state/Obfusk8Core.hpp"

#include "IOManagement.h"
#include "Win32IO.h"
#include "GHubIO.h"
#include "RazerIO.h"
#include "RPBoxIO.h"
#include "MakcuIO.h"
#include "../Utilities/Utilities.h"
#include "../Utilities/Games.hpp"
#include "User.h"
#include "ScreenCapture.h"
#include "WinRTCapture.h"
#include "OnnxRT.h"
#include "Target.h"
#include "ServerSecureClient.h"
#include "SecurePayload.h"
#include "ResourceExtract.h"
#include "WebConfigServer.h"
#include "../KeyAux/storage.hpp"
#include "../Utilities/LicenseGuard.h"

#include <timeapi.h>
#include <exception>
#include <shellapi.h>
#pragma comment(lib, "winmm.lib")

// ----------------------------------------------------------------------------
// Authentication disabled. The license / KeyAuth login flow is preserved in
// this file but compiled out, so no authentication is required to run the app.
// Set kAuthEnabled to `true` to restore the original login behavior.
// ----------------------------------------------------------------------------
namespace
{
    constexpr bool kAuthEnabled = true;
}

App::App()
{
	timeBeginPeriod(1);

	Security::SetSecurity();

	// Single instance: if the app is already running, a second launch just
	// brings the panel to the foreground in the browser and exits quietly.
	std::wstring mutexName = OBFUSCATE_WSTRING(L"ARCEUSSingleInstance");
	static HANDLE s_instanceMutex = CreateMutexW(nullptr, TRUE, mutexName.c_str());
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		ShellExecuteA(nullptr, "open", "http://127.0.0.1:4890/", nullptr, nullptr, SW_SHOWNORMAL);
		m_Running = false;
		return;
	}

	WebConfigServer::Start(4890);

	m_User = std::make_unique<User>();
	WebConfigServer::SetUser(m_User.get());

	ShellExecuteA(nullptr, "open", "http://127.0.0.1:4890/", nullptr, nullptr, SW_SHOWNORMAL);

	if constexpr (kAuthEnabled)
	{
		// KeyAux license gate. The app waits in the Login state until the web
		// panel submits a valid license (POST /action {"license": ...,
		// "remember": ...}). A saved key ("Remember me") skips login.
		LicenseServer::Authenticate();
		m_User->Internal.appState = AppState::Login;

		std::string dummyUser, dummyPass, savedKey;
		if (LicenseServer::TryAutoLogin(dummyUser, dummyPass, savedKey))
			m_User->Internal.appState = AppState::StartMenu;
	}
	else
	{
		// Seed a local (non-secret) crypto state so payload calibration
		// passes through unchanged. No license / server session is used.
		constexpr uint8_t kLocalKey[32] = {
			0x50, 0x45, 0x4B, 0x49, 0x20, 0x4D, 0x41, 0x53,
			0x54, 0x45, 0x52, 0x20, 0x44, 0x45, 0x56, 0x20,
			0x42, 0x55, 0x49, 0x4C, 0x44, 0x20, 0x4B, 0x45,
			0x59, 0x20, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35 };
		constexpr uint8_t kLocalIv[16] = {
			0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
			0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };
		SecurePayload::SetSessionKey(kLocalKey, kLocalIv);

		m_User->Internal.appState = AppState::StartMenu;
	}

	while (m_User->Internal.appState == AppState::Login || m_User->Internal.appState == AppState::StartMenu)
	{
		if (WebConfigServer::IsExitRequested()) { m_Running = false; break; }
		Sleep(100);
	}
	if (m_Running == false) return; 

	try
	{
		switch (m_User->Input.game)
		{
		case GameChoice::Fortnite:		SetActiveGame(GameType::Fortnite);		break;
		case GameChoice::Valorant:		SetActiveGame(GameType::Valorant);		break;
		case GameChoice::CS2:			SetActiveGame(GameType::CS2);			break;
		case GameChoice::RainbowSix:	SetActiveGame(GameType::RainbowSix);	break;
		case GameChoice::COD:			SetActiveGame(GameType::COD);			break;
		case GameChoice::Warzone:		SetActiveGame(GameType::Warzone);		break;
		case GameChoice::Rust:			SetActiveGame(GameType::Rust);			break;
		case GameChoice::Custom:
		default:						SetActiveGame(GameType::Custom);		break;
		}
	}
	catch (const std::exception& e)
	{
		MessageBoxA(NULL, e.what(), "Game init error", MB_ICONERROR | MB_SETFOREGROUND | MB_OK);
		m_Running = false;
		return;
	}

	try
	{
		CrashLog::Log("App ctor: before OnnxRT init");
		m_Detector = std::make_unique<OnnxRT>(m_User.get());
		CrashLog::Log("App ctor: after OnnxRT init");
	}
	catch (const std::exception& e)
	{
		MessageBoxA(NULL, e.what(), "Detector init error", MB_ICONERROR | MB_SETFOREGROUND | MB_OK);
		m_Running = false;
		return;
	}
	catch (...)
	{
		MessageBoxA(NULL, "Unknown error during detector initialization.", "Detector init error", MB_ICONERROR | MB_SETFOREGROUND | MB_OK);
		m_Running = false;
		return;
	}
	try
	{
		switch (m_User->Input.capture)
		{
		case CaptureMethod::DesktopDuplication:
		default:
			m_Screen = std::make_unique<DesktopDuplicationGPU>(m_User.get());
			break;
		case CaptureMethod::WinRT:
			m_Screen = std::make_unique<WinRTCapture>(m_User.get());
			break;
		case CaptureMethod::CaptureCard:
			m_Screen = std::make_unique<CaptureCardCapture>(m_User.get(), m_User->Input.captureCardDeviceIndex);
			break;
		}
	}
	catch (const std::exception& e)
	{
		MessageBoxA(NULL, e.what(), "", MB_ICONERROR | MB_SETFOREGROUND | MB_OK);
		m_Running = false;
		return;
	}

	try
	{
		switch (m_User->Input.method)
		{
		case InputMethod::Win32:
			m_Player = std::make_unique<Win32IO>(m_User.get());
			break;
		case InputMethod::Default:
		default:
			m_Player = std::make_unique<ViGEmIO>(m_User.get());
			break;
		case InputMethod::GHub:
			m_Player = std::make_unique<GHubIO>(m_User.get());
			break;
		case InputMethod::Razer:
			m_Player = std::make_unique<RazerIO>(m_User.get());
			break;
		case InputMethod::RPBox:
			m_Player = std::make_unique<RPBoxIO>(m_User.get());
			break;
		case InputMethod::Makcu:
			m_Player = std::make_unique<MakcuIO>(m_User.get());
			break;
		}
	}
	catch (const std::exception& e)
	{
		MessageBoxA(NULL, e.what(), "", MB_ICONERROR | MB_SETFOREGROUND | MB_OK);
		m_Running = false;
		return;
	}

	m_User->UseSettings();
	m_User->Internal.appState = AppState::Running;
	CrashLog::Log("App ctor: complete, appState=Running");
}

void App::Run()
{
	PlayerState state = PlayerState::Idle;
	Frame frame;
	TargetVector detections;
	bool wasActive = true;

	int heartbeatCounter = 0;
	while (m_Running) 
	{
		try
		{
		if (WebConfigServer::IsExitRequested())
		{
			m_Running = false;
			break;
		}
		if constexpr (kAuthEnabled)
		{
			LicenseServer::CheckHeartBeat();
			OBF_CALL_ANY_LOCAL_JUNK();
			if (!LicenseGuard::CheckTokenAtPoint1())
			{
				CrashLog::Log("App::Run: token check failed, graceful shutdown");
				m_Running = false;
				break;
			}

			OBF_CALL_ANY_LOCAL_JUNK();
			heartbeatCounter++;
			if (heartbeatCounter >= 300)
			{
				heartbeatCounter = 0;
				if (!ServerSecureClient::SendHeartbeat())
				{
					ServerSecureClient::InvalidateSession();
					SecurePayload::InvalidateSessionKey();
					m_Running = false;
					break;
				}
			}
		}

		Security::CheckSecurity();

		state = m_Player->GetPlayerState();
		if (state == PlayerState::Idle) {
			if (wasActive) { m_Player->Reset(); wasActive = false; }
			Sleep(1);
			continue;
		}
		Timer::Start();

		if (!m_Screen->GetScreen(frame)) continue;
		Timer::Measure(m_User->Internal.screenshotMs);

		OBF_CALL_ANY_LOCAL_JUNK();
		if constexpr (kAuthEnabled)
		{
			if (!LicenseGuard::CheckTokenAtPoint5())
			{
				Security::SetSecurity();
				m_Running = false;
				break;
			}
		}

		detections.clear();
		m_Detector->Detect(frame, detections);
		Timer::Measure(m_User->Internal.inferenceMs);




		if (state != PlayerState::Idle)
		{
			if (!m_Player->Act(state, detections))
			{
				m_Player->Reset();
			}
			wasActive = true;
		}
		else
		{
			if (wasActive)
			{
				m_Player->Reset();
				wasActive = false;
			}
		}
		if (state == PlayerState::Idle && m_User->Recoil.enabled && (GetAsyncKeyState(m_User->Recoil.bind) & 0x8000))
		{
			m_User->Internal.recoilAccum += m_User->Recoil.strength;
			int pixels = static_cast<int>(m_User->Internal.recoilAccum);
			if (pixels)
			{
				m_User->Internal.recoilAccum -= static_cast<float>(pixels);
				m_Player->RecoilMove(pixels);
			}
		}
		else if (!(GetAsyncKeyState(m_User->Recoil.bind) & 0x8000))
		{
			m_User->Internal.recoilAccum = 0.0f;
		}

		Timer::Measure(m_User->Internal.processingMs);

		OBF_CALL_ANY_LOCAL_JUNK();
		if constexpr (kAuthEnabled)
		{
			if (!LicenseGuard::CrossValidateAll())
			{
				CrashLog::Log("App::Run: cross-validation failed, graceful shutdown");
				m_Running = false;
				break;
			}
		}
		}
		catch (const std::exception& e)
		{
			// Never let the run loop die: a throw inside Obfusk8-VM code does NOT
			// reach RunRiven's catch (terminate directly, 0xC0000409). Swallow,
			// log and continue.
			CrashLog::Log("App::Run loop caught std::exception (survived)");
			CrashLog::Log(e.what());
			Sleep(1);
		}
		catch (...)
		{
			CrashLog::Log("App::Run loop caught unknown exception (survived)");
			Sleep(1);
		}
	}
}

App::~App()
{
	WebConfigServer::Stop();
	timeEndPeriod(1);
	ResourceExtract::CleanupDir();
}
