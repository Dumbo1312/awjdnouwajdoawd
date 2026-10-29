#define WIN32_LEAN_AND_MEAN
#define _WINSOCKAPI_
#include "KmboxNetIO.h"
#include "User.h"
#include "Target.h"
#include "kmboxNet.h"
#include <windows.h>
#include <string>

class KmboxNetIO::Impl
{
public:
	bool is_open = false;

	Impl(const std::string& ip, const std::string& port, const std::string& uuid)
	{
		char ip_buf[64], port_buf[16], uuid_buf[64];
		strcpy_s(ip_buf, ip.c_str());
		strcpy_s(port_buf, port.c_str());
		strcpy_s(uuid_buf, uuid.c_str());
		is_open = (kmNet_init(ip_buf, port_buf, uuid_buf) == 0);
	}

	~Impl()
	{
		if (is_open) kmNet_close();
	}

	void move(int x, int y)
	{
		if (!is_open) return;
		kmNet_mouse_move(static_cast<short>(x), static_cast<short>(y));
	}

	void leftDown()
	{
		if (!is_open) return;
		kmNet_mouse_left(1);
	}

	void leftUp()
	{
		if (!is_open) return;
		kmNet_mouse_left(0);
	}
};

KmboxNetIO::KmboxNetIO(User* user)
{
	m_User = user;
	m_tracking = std::make_unique<Tracking>(m_User->Network.size);
	ResetSmooth();
	m_impl = std::make_unique<Impl>(user->Input.kmboxNetIp, user->Input.kmboxNetPort, user->Input.kmboxNetUuid);
	if (!m_impl->is_open)
		throw std::runtime_error("Failed to connect to KmBox Net");
}

KmboxNetIO::~KmboxNetIO() = default;

void KmboxNetIO::Move(const int x, const int y)
{
	float sens = (GetPlayerState() == PlayerState::ADS) ? m_User->Ads.sens : m_User->HipFire.sens;
	if (sens <= 0.0f) sens = 1.0f;
	int sx = static_cast<int>(x * (sens / 50.0f));
	int sy = static_cast<int>(y * (sens / 50.0f));
	m_impl->move(sx, sy);
}

void KmboxNetIO::Shoot()
{
	m_impl->leftDown();
	Sleep(10);
	m_impl->leftUp();
}
