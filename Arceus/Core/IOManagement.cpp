#include "IOManagement.h"
#include "User.h"
#include "Target.h"
#include "SecurePayload.h"
#include "ServerSecureClient.h"
#include "../Utilities/Utilities.h"
#include "../Utilities/Strings.hpp"
#include "../Utilities/Logger.hpp"
#include "../Utilities/HardenedCore.h"
#include "../Obfusk8/Instrumentation/materialization/state/Obfusk8Core.hpp"

#include "../Utilities/Games.hpp"

#include <stdexcept>

#include "VirtualizerSDK.h"


static void CalculateAimPoints(TargetVector& targets, const float aimHeight, bool isHipFire)
{
	for (auto& t : targets)
	{
		const int centerX = t.BBox[0] + (t.BBox[2] >> 1);
		t.Point[0] = centerX;

		if (t.TType == Type::Head)
		{
			t.Point[1] = t.BBox[1] + (t.BBox[3] >> 1);
		}
		else
		{
			float effectiveHeight = aimHeight;
			if (isHipFire) {
				effectiveHeight = (aimHeight > 0.08f) ? 0.08f : aimHeight;
			}

			t.Point[1] = t.BBox[1] + static_cast<int>(t.BBox[3] * effectiveHeight);
		}
	}
}

__declspec(noinline) PlayerState IOHandler::GetPlayerState()
{
VIRTUALIZER_MUTATE_ONLY_START

	PlayerState result;
	const bool aiming = GetAsyncKeyState(m_User->Binds.aim) & 0x8000;
	// ADS activates ONLY while the configured ADS bind is held; a stored
	// VK_RBUTTON bind means "right click", anything else is its own key.
	const bool adsing = (m_User->Binds.ads != 0) && (GetAsyncKeyState(m_User->Binds.ads) & 0x8000);

	if (GetAsyncKeyState(m_User->Binds.toggleAim) & 0x01)
	{
		m_User->Internal.toggledAim = !m_User->Internal.toggledAim;
		m_User->Internal.toggledAim ? Beep(1000, 100) : Beep(500, 100);
	}

	const bool isAiming = aiming || m_User->Internal.toggledAim;

	if (!isAiming)
	{
		ResetSmooth();
		m_tracking->Reset();
		result = PlayerState::Idle;
	}

	else if (adsing)
		result = PlayerState::ADS;

	else
		result = PlayerState::HipFire;

VIRTUALIZER_MUTATE_ONLY_END

	return result;
}

bool IOHandler::Act(PlayerState state, TargetVector& detections)
{
	float currentAimHeight = (state == PlayerState::ADS) ? m_User->Ads.aimHeight : m_User->HipFire.aimHeight;
	if (!detections.empty()) {
		CalculateAimPoints(detections, currentAimHeight, (state == PlayerState::HipFire));
	}

	Target target;
	bool found = false;

	switch (state)
	{
	case PlayerState::HipFire:
		found = m_tracking->GetTarget(detections, target, Type::Head, m_User->Network.brutalMode);
		if (found) return ActHipFire(target);
		break;

	case PlayerState::ADS:
		found = m_tracking->GetTarget(detections, target, Type::Head, m_User->Network.brutalMode);
		if (found) return ActAds(target);
		break;
	}

	return false;
}

bool IOHandler::ActHipFire(Target& target)
{
	if (Game2::THIRD_PERSON() &&
		TargetHelper::IsInsideExclusion(target, m_User->Internal.Exclusion,
			m_User->Internal.ResolutionW, m_User->Internal.ResolutionH,
			m_User->Network.size))
	{
		ResetSmooth();
		return false;
	}

	target.Point[X] -= m_User->Network.size / 2;
	target.Point[Y] -= m_User->Network.size / 2;

	{
		if (!TargetHelper::IsInFov(target, m_User->HipFire.fov, m_User->Network.fovShape))
		{
			ResetSmooth();
			return false;
		}
	}

	// Left-side (third person) rejection: multiply left distance by multiplier
	if (Game2::THIRD_PERSON() && m_User->Network.thirdPersonMultiplier > 1.0f)
	{
		float leftDist = std::abs(static_cast<float>(target.Point[X]));
		if (target.Point[X] < 0 && leftDist * m_User->Network.thirdPersonMultiplier > m_User->HipFire.fov)
		{
			ResetSmooth();
			return false;
		}
	}

	int aimX, aimY;
	aimX = target.Point[X];
	aimY = target.Point[Y];

	OBF_CALL_ANY_LOCAL_JUNK();
	{
		double calX = static_cast<double>(aimX);
		double calY = static_cast<double>(aimY);
		SecurePayload::ApplyCalibrationOffset(calX, calY, calX, calY);
		aimX = static_cast<int>(calX);
		aimY = static_cast<int>(calY);
	}

	{
		int shapedX = 0, shapedY = 0;
		m_hfSmoother.Smooth(aimX, aimY, m_User->HipFire.strength,
			m_User->HipFire.fov, m_User->HipFire.deadzone, shapedX, shapedY);
		aimX = shapedX;
		aimY = shapedY;
	}

	OBF_CALL_ANY_LOCAL_JUNK();
	{
		const auto& session = ServerSecureClient::GetSession();
		uint64_t noise = SecurePayload::ComputeAimNoise(session.noiseSeed,
			m_User->Internal.inferenceMs + m_User->Internal.processingMs);
		// HardenedCore canary folds into the aim math: killing the protection
		// watchdogs flips this digest and silently corrupts the jitter pattern.
		noise ^= HardenedCore::FrameCanary();
		aimX += static_cast<int>(noise & 0x3) - 1;
		aimY += static_cast<int>((noise >> 4) & 0x3) - 1;
	}

	Move(static_cast<int>(aimX * m_speedScale), static_cast<int>(aimY * m_speedScale));

	if (!m_User->Trigger.enabled || !Game2::HAS_TRIGGERBOT())
		return true;

	if (m_User->Trigger.bind != 0x00 && !(GetAsyncKeyState(m_User->Trigger.bind) & 0x8000))
		return true;

	if (target.TType == Type::Head)
	{
		if (TargetHelper::IsInHead(target, m_User->Network.size, m_User->Trigger.proximity))
			Shoot();
	}
	else if (Game2::FORCE_TRIGGER())
	{
		if (TargetHelper::IsInCreatedHead(target, m_User->Network.size, m_User->Trigger.proximity))
			Shoot();
	}

	return true;
}

bool IOHandler::ActAds(Target& target)
{
	target.Point[X] -= m_User->Network.size / 2;
	target.Point[Y] -= m_User->Network.size / 2;

	{
		if (!TargetHelper::IsInFov(target, m_User->Ads.fov, m_User->Network.fovShape))
		{
			ResetSmooth();
			return false;
		}
	}

	int aimX, aimY;
	aimX = target.Point[X];
	aimY = target.Point[Y];

	OBF_CALL_ANY_LOCAL_JUNK();
	{
		double calX = static_cast<double>(aimX);
		double calY = static_cast<double>(aimY);
		SecurePayload::ApplyCalibrationOffset(calX, calY, calX, calY);
		aimX = static_cast<int>(calX);
		aimY = static_cast<int>(calY);
	}

	{
		int shapedX = 0, shapedY = 0;
		m_adsSmoother.Smooth(aimX, aimY, m_User->Ads.strength,
			m_User->Ads.fov, m_User->Ads.deadzone, shapedX, shapedY);
		aimX = shapedX;
		aimY = shapedY;
	}

	OBF_CALL_ANY_LOCAL_JUNK();
	{
		const auto& session = ServerSecureClient::GetSession();
		uint64_t noise = SecurePayload::ComputeAimNoise(session.noiseSeed,
			m_User->Internal.inferenceMs + m_User->Internal.processingMs);
		// HardenedCore canary folds into the aim math: killing the protection
		// watchdogs flips this digest and silently corrupts the jitter pattern.
		noise ^= HardenedCore::FrameCanary();
		aimX += static_cast<int>(noise & 0x3) - 1;
		aimY += static_cast<int>((noise >> 4) & 0x3) - 1;
	}

	Move(static_cast<int>(aimX * m_speedScale), static_cast<int>(aimY * m_speedScale));

	if (!m_User->Trigger.enabled || !Game2::HAS_TRIGGERBOT())
		return true;

	if (m_User->Trigger.bind != 0x00 && !(GetAsyncKeyState(m_User->Trigger.bind) & 0x8000))
		return true;

	if (target.TType == Type::Head)
	{
		if (TargetHelper::IsInHead(target, m_User->Network.size, m_User->Trigger.proximity))
			Shoot();
	}
	else if (Game2::FORCE_TRIGGER())
	{
		if (TargetHelper::IsInCreatedHead(target, m_User->Network.size, m_User->Trigger.proximity))
			Shoot();
	}

	return true;
}

#include "ViGEmController.h"

ViGEmIO::ViGEmIO(User* user)
{
	m_speedScale = 1.0f;
	m_User = user;
	m_tracking = std::make_unique<Tracking>(m_User->Network.size);
	ResetSmooth();

	m_vigem = std::make_unique<ViGEmController>();
	if (!m_vigem->initialize())
		throw std::runtime_error("Failed to initialize ViGEm controller");

	if (!SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS))
		SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
}

ViGEmIO::~ViGEmIO() = default;

void ViGEmIO::Reset()
{
	m_vigem->controllerMove(0, 0, 0.0f);
	m_vigem->controllerTrigger(false);
}

void ViGEmIO::Move(const int x, const int y)
{
	float sens = (GetPlayerState() == PlayerState::ADS) ? m_User->Ads.sens : m_User->HipFire.sens;
	int recoilY = 0;
	if (m_User->Recoil.enabled && (GetAsyncKeyState(m_User->Recoil.bind) & 0x8000))
	{
		m_User->Internal.recoilAccum += m_User->Recoil.strength;
		recoilY = static_cast<int>(m_User->Internal.recoilAccum);
		if (recoilY)
			m_User->Internal.recoilAccum -= static_cast<float>(recoilY);
	}
	m_vigem->controllerMove(x, y + recoilY, sens);
}

void ViGEmIO::RecoilMove(int y)
{
	float sens = (GetPlayerState() == PlayerState::ADS) ? m_User->Ads.sens : m_User->HipFire.sens;
	m_vigem->controllerMove(0, y, sens);
}

void ViGEmIO::Shoot()
{
	m_vigem->controllerTrigger(false);
	m_vigem->controllerTrigger(true);
}
