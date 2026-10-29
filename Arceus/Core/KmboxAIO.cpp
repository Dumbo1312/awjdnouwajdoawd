#include "KmboxAIO.h"
#include "User.h"
#include "Target.h"
#include "kmboxA.h"
#include <windows.h>
#include <string>
#include <algorithm>
#include <cctype>

class KmboxAIO::Impl
{
public:
	bool is_open = false;

	Impl(const std::string& pidvid)
	{
		unsigned short pid = 0, vid = 0;
		std::string hex;
		for (unsigned char c : pidvid)
			if (std::isxdigit(c))
				hex.push_back(static_cast<char>(std::toupper(c)));

		if (hex.size() == 8)
		{
			pid = static_cast<unsigned short>(std::stoul(hex.substr(0, 4), nullptr, 16));
			vid = static_cast<unsigned short>(std::stoul(hex.substr(4, 4), nullptr, 16));
			is_open = (KM_init(vid, pid) == 0);
		}
	}

	~Impl()
	{
		if (is_open) KM_close();
	}

	void move(int x, int y)
	{
		if (!is_open) return;
		KM_move(static_cast<short>(x), static_cast<short>(y));
	}

	void leftDown()
	{
		if (!is_open) return;
		KM_left(1);
	}

	void leftUp()
	{
		if (!is_open) return;
		KM_left(0);
	}
};

KmboxAIO::KmboxAIO(User* user)
{
	m_User = user;
	m_tracking = std::make_unique<Tracking>(m_User->Network.size);
	ResetSmooth();
	m_impl = std::make_unique<Impl>(user->Input.kmboxAPidVid);
	if (!m_impl->is_open)
		throw std::runtime_error("Failed to connect to KmBox A");
}

KmboxAIO::~KmboxAIO() = default;

void KmboxAIO::Move(const int x, const int y)
{
	float sens = (GetPlayerState() == PlayerState::ADS) ? m_User->Ads.sens : m_User->HipFire.sens;
	if (sens <= 0.0f) sens = 1.0f;
	int sx = static_cast<int>(x * (sens / 50.0f));
	int sy = static_cast<int>(y * (sens / 50.0f));
	m_impl->move(sx, sy);
}

void KmboxAIO::Shoot()
{
	m_impl->leftDown();
	Sleep(10);
	m_impl->leftUp();
}
