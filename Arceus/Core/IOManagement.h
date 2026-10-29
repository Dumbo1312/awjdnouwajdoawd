#pragma once
#include <memory>
#include "AimSmoother.h"

class User;
class Target;
class Tracking;
class TargetVector;

enum class PlayerState
{
	Idle = 0, HipFire, ADS, Scope
};

class IOHandler
{
public:
	virtual ~IOHandler() = default;

	PlayerState GetPlayerState();
	bool Act(PlayerState state, TargetVector& detections);
	virtual void Reset() {}
	virtual void RecoilMove(int y) {}

protected:
	bool ActHipFire(Target& target);
	bool ActAds(Target& target);
	bool ActScope(Target& target) { return false; }
	virtual void Move(const int x, const int y) = 0;
	virtual void Shoot() = 0;

	User* m_User;
	std::unique_ptr<Tracking> m_tracking;

	AimSmoother m_hfSmoother;
	AimSmoother m_adsSmoother;

	float m_speedScale = 0.5f;

	void ResetSmooth()
	{
		m_hfSmoother.Reset();
		m_adsSmoother.Reset();
	}
};

class ViGEmController;
class GHubIO;
class RazerIO;
class RPBoxIO;
class MakcuIO;

class ViGEmIO : public IOHandler
{
public:
	ViGEmIO(User* user);
	~ViGEmIO();
	void Reset() override;
	void RecoilMove(int y);

private:
	void Move(const int x, const int y) override;
	void Shoot() override;

	std::unique_ptr<ViGEmController> m_vigem;
};
