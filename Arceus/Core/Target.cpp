#include "Target.h"
#include "../Utilities/Utilities.h"
#include "ByteTrack/BYTETracker.h"
#include "VirtualizerSDK.h"


Tracking::Tracking(const int netSize) : m_netSize(netSize)
{
	m_tracker = std::make_unique<byte_track::BYTETracker>(60, 60, 0.2f, 0.35f, 0.8f);
}

Tracking::~Tracking() = default;

void Tracking::Reset()
{
	m_tracker = std::make_unique<byte_track::BYTETracker>(60, 60, 0.2f, 0.35f, 0.8f);
	m_activeTrack = nullptr;
	m_activeType = Type::Ignore;
	m_lostFrames = 0;
	m_lastPos = { 0, 0 };
}

bool Tracking::GetTarget(const TargetVector& detections, Target& outTarget, const Type preferredType, bool brutalMode)
{

	const int maxLostFrames = brutalMode ? 120 : 60;

	if (detections.empty())
	{
		m_lostFrames++;
		if (m_lostFrames > maxLostFrames) {
			m_activeTrack = nullptr;
			m_activeType = Type::Ignore;
		}
		return false;
	}

	std::vector<byte_track::Object> objects;
	for (const Target& det : detections)
	{
		byte_track::Rect<float> rect(
			static_cast<float>(det.BBox[X]),
			static_cast<float>(det.BBox[Y]),
			static_cast<float>(det.BBox[W]),
			static_cast<float>(det.BBox[H]));
		objects.emplace_back(rect, static_cast<int>(det.TType), det.Confidence);
	}

	auto tracks = m_tracker->update(objects);

	outTarget = SelectBestTrack(tracks, detections, preferredType, brutalMode);

	if (outTarget.TType == Type::Ignore && !detections.empty())
	{
		const int center = m_netSize / 2;
		double bestDist = brutalMode ? (m_netSize * m_netSize) : 400.0;
		int bestIdx = -1;

		for (int i = 0; i < detections.size(); ++i)
		{
			if (detections[i].TType == Type::Ignore) continue;

			double dx = detections[i].Point[X] - center;
			double dy = detections[i].Point[Y] - center;
			double dist = dx * dx + dy * dy;

			if (dist < bestDist)
			{
				if (detections[i].TType == preferredType) {
					bestDist = dist;
					bestIdx = i;
				}
				else if (bestIdx == -1 || detections[bestIdx].TType != preferredType) {
					bestDist = dist;
					bestIdx = i;
				}
			}
		}

		if (bestIdx != -1)
		{
			outTarget = detections[bestIdx];
			return true;
		}
	}

	return outTarget.TType != Type::Ignore;
}

Target Tracking::SelectBestTrack(
	const std::vector<std::shared_ptr<byte_track::STrack>>& tracks,
	const TargetVector& detections,
	const Type preferredType,
	bool brutalMode)
{
	const int center = m_netSize / 2;
	const float searchRadius = brutalMode ? 250.0f : 120.0f;
	const int maxLostFrames = brutalMode ? 120 : 60;

	if (m_activeTrack)
	{
		bool currentTrackFound = false;
		for (const auto& track : tracks)
		{
			if (track->getTrackId() == m_activeTrack->getTrackId())
			{
				const auto& rect = track->getRect();
				float cx = rect.x() + rect.width() / 2.0f;
				float cy = rect.y() + rect.height() / 2.0f;

 				float dx = cx - m_lastPos[0];
 				float dy = cy - m_lastPos[1];
 				float dist = std::sqrt(dx * dx + dy * dy);
 
 				if (dist < searchRadius)
 				{
					if (brutalMode)
					{
						float currentDistToCrosshair = std::sqrtf((cx - center) * (cx - center) + (cy - center) * (cy - center));
						
						for (const auto& other : tracks)
						{
							if (other->getTrackId() == m_activeTrack->getTrackId()) continue;
							
							const auto& oRect = other->getRect();
							float ocx = oRect.x() + oRect.width() / 2.0f;
							float ocy = oRect.y() + oRect.height() / 2.0f;
							float otherDistToCrosshair = std::sqrtf((ocx - center) * (ocx - center) + (ocy - center) * (ocy - center));
							
							if (otherDistToCrosshair < (currentDistToCrosshair / 2.0f))
							{
								m_activeTrack = nullptr;
								goto reselection;
							}
						}
					}

 					m_lostFrames = 0;
 					m_lastPos = { static_cast<int>(cx), static_cast<int>(cy) };
 					
 					Target result;
 					result.BBox[X] = static_cast<int>(rect.x());
 					result.BBox[Y] = static_cast<int>(rect.y());
 					result.BBox[W] = static_cast<int>(rect.width());
 					result.BBox[H] = static_cast<int>(rect.height());
 					result.Confidence = track->getScore();
 					result.TType = m_activeType;
					result.Point[X] = result.BBox[X] + (result.BBox[W] >> 1);
					result.Point[Y] = result.BBox[Y] + (result.BBox[H] >> 1);
 
 					float bestMatch = 0.0f;
 					for (const Target& det : detections)
 					{
 						float ddx = cx - (det.BBox[X] + det.BBox[W] / 2.0f);
 						float ddy = cy - (det.BBox[Y] + det.BBox[H] / 2.0f);
 						float matchScore = 1.0f / (1.0f + ddx * ddx + ddy * ddy);
 						if (matchScore > bestMatch)
 						{
 							bestMatch = matchScore;
 							result.Point = det.Point;
 							result.TType = det.TType;
 						}
 					}
 					m_activeType = result.TType;
 					return result;
 				}
 			}
 		}
 
reselection:
 		m_lostFrames++;
 		if (m_lostFrames > maxLostFrames)
 		{
 			m_activeTrack = nullptr;
 			m_activeType = Type::Ignore;
 		}
 	}

	double bestDistPreferred = m_netSize * m_netSize;
	double bestDistAny = m_netSize * m_netSize;
	std::shared_ptr<byte_track::STrack> bestPreferred = nullptr;
	std::shared_ptr<byte_track::STrack> bestAny = nullptr;
	Type bestPreferredType = Type::Ignore;
	Type bestAnyType = Type::Ignore;
	std::array<int, 2> bestPreferredPoint = { 0, 0 };
	std::array<int, 2> bestAnyPoint = { 0, 0 };

	for (const auto& track : tracks)
	{
		const auto& rect = track->getRect();
		float cx = rect.x() + rect.width() / 2.0f;
		float cy = rect.y() + rect.height() / 2.0f;
		double dist = (cx - center) * (cx - center) + (cy - center) * (cy - center);

		Type trackType = Type::Player; 
		std::array<int, 2> trackPoint = { static_cast<int>(cx), static_cast<int>(cy) };
		float bestMatch = 0.0f;

		for (const Target& det : detections)
		{
			float dx = cx - (det.BBox[X] + det.BBox[W] / 2.0f);
			float dy = cy - (det.BBox[Y] + det.BBox[H] / 2.0f);
			float d2 = dx * dx + dy * dy;
			float matchScore = 1.0f / (1.0f + d2); 

			if (matchScore > bestMatch)
			{
				bestMatch = matchScore;
				trackType = det.TType;
				trackPoint = det.Point;
			}
		}

		if (trackType == preferredType && dist < bestDistPreferred)
		{
			bestDistPreferred = dist;
			bestPreferred = track;
			bestPreferredType = trackType;
			bestPreferredPoint = trackPoint;
		}
		if (dist < bestDistAny)
		{
			bestDistAny = dist;
			bestAny = track;
			bestAnyType = trackType;
			bestAnyPoint = trackPoint;
		}
	}

	auto chosen = bestPreferred ? bestPreferred : bestAny;
	if (!chosen) return Target();

	m_activeTrack = chosen;
	m_activeType = bestPreferred ? bestPreferredType : bestAnyType;
	m_lastPos = { static_cast<int>(chosen->getRect().x() + chosen->getRect().width() / 2.0f),
				  static_cast<int>(chosen->getRect().y() + chosen->getRect().height() / 2.0f) };
	m_lostFrames = 0;

	const auto& rect = chosen->getRect();
	Target result;
	result.BBox[X] = static_cast<int>(rect.x());
	result.BBox[Y] = static_cast<int>(rect.y());
	result.BBox[W] = static_cast<int>(rect.width());
	result.BBox[H] = static_cast<int>(rect.height());
	result.Point = bestPreferred ? bestPreferredPoint : bestAnyPoint;
	result.Confidence = chosen->getScore();
	result.TType = m_activeType;

	return result;
}

void Tracking::GetPredictionOffset(int& offsetX, int& offsetY, float predictionStrength) const
{
	if (!m_activeTrack || predictionStrength <= 0.0f)
	{
		offsetX = 0;
		offsetY = 0;
		return;
	}

	float vx = m_activeTrack->getVelocityX();
	float vy = m_activeTrack->getVelocityY();

	offsetX = static_cast<int>(vx * predictionStrength);
	offsetY = static_cast<int>(vy * predictionStrength);
}


namespace TargetHelper
{
	int ScreenToWorld(int relPos, const int degreeTurn, const int res, const float fovFactor)
	{
		double lookAt = relPos * 2.0 / res;
		double degrees = atan(lookAt * fovFactor) * RADTODEG;
		return static_cast<int>(degreeTurn * degrees);
	}

	bool IsInFov(const Target& target, const int fov, const FovShape shape)
	{
		float dx = static_cast<float>(target.Point[X]);
		float dy = static_cast<float>(target.Point[Y]);
		float half = fov / 2.0f;

		if (shape == FovShape::Square)
			return std::abs(dx) <= half && std::abs(dy) <= half;

		float dist = std::sqrtf(dx * dx + dy * dy);
		return dist <= half;
	}

	bool IsInsideExclusion(const Target& target, const std::array<int, 4>& exclusion, const int resW, const int resH, const int netSize)
	{
VIRTUALIZER_MUTATE_ONLY_START
		int left = exclusion[X];
		int right = exclusion[X] + exclusion[W];
		int top = exclusion[Y];
		int bottom = exclusion[Y] + exclusion[H];

		int pointX = (resW / 2) - (netSize / 2) + target.Point[X];
		int pointY = (resH / 2) - (netSize / 2) + target.Point[Y];

VIRTUALIZER_MUTATE_ONLY_END

		if (left <= pointX && pointX <= right)
			if (top <= pointY && pointY <= bottom)
				return true;

		return false;
	}

	bool IsInHead(const Target& target, const int networkSize, const int proximity)
	{
VIRTUALIZER_MUTATE_ONLY_START
		const int crossHairCenter = networkSize / 2;

		const int adjustedLeft = target.BBox[X] - proximity;
		const int adjustedRight = target.BBox[X] + target.BBox[W] + proximity;
		const int adjustedTop = target.BBox[Y] - proximity;
		const int adjustedBottom = target.BBox[Y] + target.BBox[H] + proximity;

VIRTUALIZER_MUTATE_ONLY_END

		if (adjustedLeft < crossHairCenter && crossHairCenter < adjustedRight)
			if (adjustedTop < crossHairCenter && crossHairCenter < adjustedBottom)
				return true;

		return false;
	}

	bool IsInCreatedHead(const Target& target, const int networkSize, const int proximity)
	{
VIRTUALIZER_MUTATE_ONLY_START

		const int crossHairCenter = networkSize / 2;

		const int halfSize = 4;

		const int adjustedLeft = target.Point[X] - halfSize - proximity;
		const int adjustedRight = target.Point[X] + halfSize + proximity;
		const int adjustedTop = target.Point[Y] - halfSize - proximity;
		const int adjustedBottom = target.Point[Y] + halfSize + proximity;

VIRTUALIZER_MUTATE_ONLY_END

		if (adjustedLeft < crossHairCenter && crossHairCenter < adjustedRight)
			if (adjustedTop < crossHairCenter && crossHairCenter < adjustedBottom)
				return true;

		return false;
	}
}
