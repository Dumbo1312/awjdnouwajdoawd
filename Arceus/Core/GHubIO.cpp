#include "GHubIO.h"
#include "User.h"
#include "Target.h"
#include <windows.h>
#include <filesystem>

class GHubIO::Impl
{
public:
	using MoveFn = bool(*)(int, int);
	using PressFn = bool(*)(int);
	using ReleaseFn = bool(*)();

	Impl()
	{
		char buffer[MAX_PATH];
		GetModuleFileNameA(NULL, buffer, MAX_PATH);
		auto basedir = std::filesystem::path(buffer).parent_path();
		auto dlldir = basedir / "ghub_mouse.dll";
		m_gm = LoadLibraryA(dlldir.string().c_str());
		if (m_gm == NULL)
		{
			auto alt = basedir / "config" / "Ghub" / "ghub_mouse.dll";
			m_gm = LoadLibraryA(alt.string().c_str());
		}
		if (m_gm == NULL) return;

		m_moveR = reinterpret_cast<MoveFn>(GetProcAddress(m_gm, "moveR"));
		m_press = reinterpret_cast<PressFn>(GetProcAddress(m_gm, "press"));
		m_release = reinterpret_cast<ReleaseFn>(GetProcAddress(m_gm, "release"));

		auto mouse_open = reinterpret_cast<ReleaseFn>(GetProcAddress(m_gm, "mouse_open"));
		if (mouse_open) m_ok = mouse_open();
	}

	~Impl()
	{
		if (m_gm != NULL)
		{
			auto mouse_close = reinterpret_cast<ReleaseFn>(GetProcAddress(m_gm, "mouse_close"));
			if (mouse_close) mouse_close();
			FreeLibrary(m_gm);
		}
	}

	bool move(int x, int y)
	{
		if (m_ok && m_moveR) return m_moveR(x, y);
		return false;
	}

	void press()
	{
		if (m_ok && m_press) m_press(1);
	}

	void release()
	{
		if (m_ok && m_release) m_release();
	}

private:
	HMODULE m_gm = NULL;
	bool m_ok = false;
	MoveFn m_moveR = nullptr;
	PressFn m_press = nullptr;
	ReleaseFn m_release = nullptr;
};

GHubIO::GHubIO(User* user)
{
	m_User = user;
	m_tracking = std::make_unique<Tracking>(m_User->Network.size);
	ResetSmooth();
	m_impl = std::make_unique<Impl>();
}

GHubIO::~GHubIO() = default;

void GHubIO::Move(const int x, const int y)
{
	float sens = (GetPlayerState() == PlayerState::ADS) ? m_User->Ads.sens : m_User->HipFire.sens;
	if (sens <= 0.0f) sens = 1.0f;
	int sx = static_cast<int>(x * 1.8f * (sens / 50.0f));
	int sy = static_cast<int>(y * 1.8f * (sens / 50.0f));
	m_impl->move(sx, sy);
}

void GHubIO::Shoot()
{
	m_impl->press();
	Sleep(10);
	m_impl->release();
}
