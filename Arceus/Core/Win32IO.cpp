#include "Win32IO.h"
#include <windows.h>
#include "User.h"
#include "Target.h"

Win32IO::Win32IO(User* user)
{
	m_User = user;
	m_tracking = std::make_unique<Tracking>(m_User->Network.size);
	ResetSmooth();
}

Win32IO::~Win32IO() = default;

void Win32IO::Move(const int x, const int y)
{
	mouse_event(MOUSEEVENTF_MOVE, static_cast<int>(x * 0.34f * 1.8f), static_cast<int>(y * 0.34f * 1.8f), 0, 0);
}

void Win32IO::Shoot()
{
	if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0)
		return;

	mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
	mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
}
