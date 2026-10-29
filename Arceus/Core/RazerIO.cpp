#include "RazerIO.h"
#include "User.h"
#include "Target.h"
#include <windows.h>
#include <filesystem>
#include <vector>

class RazerIO::Impl
{
public:
	using InitFn = bool(*)();
	using MouseMoveFn = void(*)(int, int, bool);
	using MouseClickFn = void(*)(int);

	Impl()
	{
		wchar_t buffer[MAX_PATH]{};
		std::filesystem::path exeDir;
		if (GetModuleFileNameW(nullptr, buffer, MAX_PATH) > 0)
			exeDir = std::filesystem::path(buffer).parent_path();

		std::vector<std::filesystem::path> candidates;
		if (!exeDir.empty())
		{
			candidates.push_back(exeDir / L"rzctl.dll");
			candidates.push_back(exeDir / L"controls" / L"rzctl.dll");
			candidates.push_back(exeDir / L"config" / L"Razer" / L"rzctl.dll");
		}

		for (const auto& c : candidates)
		{
			if (std::filesystem::exists(c))
			{
				m_rzctl = LoadLibraryW(c.wstring().c_str());
				if (m_rzctl) break;
			}
		}

		if (!m_rzctl) return;

		m_init = reinterpret_cast<InitFn>(GetProcAddress(m_rzctl, "init"));
		m_mouseMove = reinterpret_cast<MouseMoveFn>(GetProcAddress(m_rzctl, "mouse_move"));
		m_mouseClick = reinterpret_cast<MouseClickFn>(GetProcAddress(m_rzctl, "mouse_click"));

		if (!m_init || !m_mouseMove) return;
		m_ok = m_init();
	}

	~Impl()
	{
		if (m_rzctl) FreeLibrary(m_rzctl);
	}

	bool move(int x, int y)
	{
		if (!m_ok) return false;
		m_mouseMove(x, y, true);
		return true;
	}

	void press()
	{
		if (!m_ok) return;
		m_mouseClick(1);
	}

	void release()
	{
		if (!m_ok) return;
		m_mouseClick(2);
	}

private:
	HMODULE m_rzctl = nullptr;
	bool m_ok = false;
	InitFn m_init = nullptr;
	MouseMoveFn m_mouseMove = nullptr;
	MouseClickFn m_mouseClick = nullptr;
};

RazerIO::RazerIO(User* user)
{
	m_User = user;
	m_tracking = std::make_unique<Tracking>(m_User->Network.size);
	ResetSmooth();
	m_impl = std::make_unique<Impl>();
}

RazerIO::~RazerIO() = default;

void RazerIO::Move(const int x, const int y)
{
	float sens = (GetPlayerState() == PlayerState::ADS) ? m_User->Ads.sens : m_User->HipFire.sens;
	if (sens <= 0.0f) sens = 1.0f;
	int sx = static_cast<int>(x * 1.8f * (sens / 50.0f));
	int sy = static_cast<int>(y * 1.8f * (sens / 50.0f));
	m_impl->move(sx, sy);
}

void RazerIO::Shoot()
{
	m_impl->press();
	Sleep(10);
	m_impl->release();
}
