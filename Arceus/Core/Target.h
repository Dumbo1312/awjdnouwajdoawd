#pragma once
#include "User.h"
#include <array>
#include <chrono>
#include <memory>
#include <vector>

inline constexpr float RADTODEG = 180.0 / 3.1415;

inline constexpr int X = 0;
inline constexpr int Y = 1;
inline constexpr int W = 2;
inline constexpr int H = 3;

enum class Type
{
	Ignore = 0, Player, Head
};

class Target
{
public:
	Target() : BBox{ 0, 0, 0, 0 }, Point{ 0, 0 }, Confidence(0.0f), TType(Type::Ignore) {}

	Target(int x, int y, int w, int h, int relX, int relY, float confidence, Type type)
		: BBox{ x, y, w, h }, Point{ relX, relY }, Confidence(confidence), TType(type) {}

	std::array<int, 4> BBox;
	std::array<int, 2> Point;
	float Confidence;
	Type TType;
};

class TargetVector;

namespace byte_track { class BYTETracker; class STrack; }

class Tracking
{
public:
	explicit Tracking(const int netSize);
	~Tracking();

	bool GetTarget(const TargetVector& detections, Target& outTarget, const Type preferredType, bool brutalMode = false);
	void Reset();

	void GetPredictionOffset(int& offsetX, int& offsetY, float predictionStrength) const;

private:
	int m_netSize;
	std::unique_ptr<byte_track::BYTETracker> m_tracker;

	std::shared_ptr<byte_track::STrack> m_activeTrack;
	Type m_activeType = Type::Ignore;
	int m_lostFrames = 0;
	std::array<int, 2> m_lastPos = { 0, 0 };

	Target SelectBestTrack(const std::vector<std::shared_ptr<byte_track::STrack>>& tracks,
		const TargetVector& detections, const Type preferredType, bool brutalMode);
};

namespace TargetHelper
{
	int ScreenToWorld(int relPos, const int degreeTurn, const int res, const float fovFactor);

	bool IsInFov(const Target& target, const int fov, const FovShape shape);

	bool IsInsideExclusion(const Target& target, const std::array<int, 4>& exclusion, const int resW, const int resH, const int netSize);

	bool IsInHead(const Target& target, const int networkSize, const int proximity);

	bool IsInCreatedHead(const Target& target, const int networkSize, const int proximity);
}
