#include "RPBoxIO.h"
#include "RPBoxController.h"
#include "User.h"
#include "Target.h"
#include <windows.h>
#include <algorithm>

RPBoxIO::RPBoxIO(User* user)
{
	m_User = user;
	m_tracking = std::make_unique<Tracking>(m_User->Network.size);
	ResetSmooth();

	m_rpbox = std::make_unique<RPBoxController>();
	if (!m_rpbox->initialize())
		throw std::runtime_error("Failed to initialize RPBox controller");

	if (!SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS))
		SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
}

RPBoxIO::~RPBoxIO() = default;

void RPBoxIO::Reset()
{
	if (m_rpbox) m_rpbox->mouseMove(0, 0);
}

void RPBoxIO::Move(const int x, const int y)
{
	float sens = (GetPlayerState() == PlayerState::ADS) ? m_User->Ads.sens : m_User->HipFire.sens;
	if (sens <= 0.0f) sens = 1.0f;
	int sx = static_cast<int>(x * 1.8f * (sens / 50.0f));
	int sy = static_cast<int>(y * 1.8f * (sens / 50.0f));
	m_rpbox->mouseMove(static_cast<int16_t>(sx), static_cast<int16_t>(sy));
}

void RPBoxIO::Shoot()
{
	m_rpbox->mouseClick();
}
