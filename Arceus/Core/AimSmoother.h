#pragma once
#include <cmath>
#include <algorithm>

class AimSmoother
{
public:
	AimSmoother() { Reset(); }

	void Reset()
	{
		m_prevErrorX = 0.0f;
		m_prevErrorY = 0.0f;
		m_lpfX = 0.0f;
		m_lpfY = 0.0f;
		m_initialized = false;
	}

	void Smooth(int rawX, int rawY, float baseStrength, int maxFov, int deadzoneRadius,
		int& outX, int& outY)
	{
		const float errorX = static_cast<float>(rawX);
		const float errorY = static_cast<float>(rawY);
		const float distance = std::sqrtf(errorX * errorX + errorY * errorY);

		if (deadzoneRadius > 0 && distance < static_cast<float>(deadzoneRadius))
		{
			Reset();
			outX = 0;
			outY = 0;
			return;
		}

		const float fovF = static_cast<float>(std::max(maxFov, 1));
		const float t = std::clamp(distance / fovF, 0.0f, 1.0f);
		const float smoothT = t * t * (3.0f - 2.0f * t); 
		
		float pGain = (baseStrength * 3.5f) * (0.3f + 0.7f * smoothT);
		pGain = std::clamp(pGain, 0.1f, 2.0f);

		float outputX = errorX * pGain;
		float outputY = errorY * pGain;

		if (m_initialized)
		{
			float velocityX = errorX - m_prevErrorX;
			float velocityY = errorY - m_prevErrorY;

			float dGain = baseStrength * 3.0f;
			
			if (baseStrength > 0.65f) {
				dGain *= (1.0f + (baseStrength - 0.65f) * 3.5f);
			}

			outputX += velocityX * dGain;
			outputY += velocityY * dGain;

			float jitterAlpha = std::clamp(0.25f + 0.5f * smoothT, 0.05f, 0.9f);
			
			if (baseStrength > 0.6f) {
				jitterAlpha *= (1.0f - (baseStrength - 0.6f) * 1.5f); 
			}
			jitterAlpha = std::max(jitterAlpha, 0.02f);

			m_lpfX = m_lpfX + jitterAlpha * (outputX - m_lpfX);
			m_lpfY = m_lpfY + jitterAlpha * (outputY - m_lpfY);
		}
		else
		{
			m_lpfX = outputX;
			m_lpfY = outputY;
			m_initialized = true;
		}

		m_prevErrorX = errorX;
		m_prevErrorY = errorY;

		if (std::abs(m_lpfX) < 0.4f) m_lpfX = 0.0f;
		if (std::abs(m_lpfY) < 0.4f) m_lpfY = 0.0f;

		outX = static_cast<int>(std::round(m_lpfX));
		outY = static_cast<int>(std::round(m_lpfY));
	}

private:
	float m_prevErrorX;
	float m_prevErrorY;
	float m_lpfX;
	float m_lpfY;
	bool  m_initialized;
};
