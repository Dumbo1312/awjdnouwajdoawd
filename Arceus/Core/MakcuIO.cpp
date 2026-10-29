#include "MakcuIO.h"
#include "MakcuController.h"
#include "User.h"
#include "Target.h"
#include <windows.h>
#include <algorithm>

MakcuIO::MakcuIO(User* user)
{
	m_User = user;
	m_tracking = std::make_unique<Tracking>(m_User->Network.size);
	ResetSmooth();

	m_makcu = std::make_unique<MakcuController>();
	if (!m_makcu->initialize())
		throw std::runtime_error("Failed to initialize Makcu controller");

	if (!SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS))
		SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
}

MakcuIO::~MakcuIO() = default;

void MakcuIO::Reset()
{
	if (m_makcu) m_makcu->mouseMove(0, 0);
}

void MakcuIO::Move(const int x, const int y)
{
	float sens = (GetPlayerState() == PlayerState::ADS) ? m_User->Ads.sens : m_User->HipFire.sens;
	if (sens <= 0.0f) sens = 1.0f;
	int sx = static_cast<int>(x * 1.8f * (sens / 50.0f));
	int sy = static_cast<int>(y * 1.8f * (sens / 50.0f));
	int clampedX = std::clamp(sx, -128, 128);
	int clampedY = std::clamp(sy, -128, 128);
	m_makcu->mouseMove(clampedX, clampedY);
}

void MakcuIO::Shoot()
{
	m_makcu->mouseClick();
}
