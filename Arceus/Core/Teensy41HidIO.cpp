#include "Teensy41HidIO.h"
#include "User.h"
#include "Target.h"
#include "hidapi.h"
#include <windows.h>
#include <cstring>
#include <thread>
#include <atomic>
#include <mutex>
#include <array>
#include <algorithm>
#include <limits>

class Teensy41HidIO::Impl
{
public:
	bool connected = false;

	Impl()
	{
		hid_init();
		hid_device_info* devs = hid_enumerate(0, 0);
		for (hid_device_info* cur = devs; cur; cur = cur->next)
		{
			if (cur->usage_page == 0xFFAB && cur->usage == 0x0200)
			{
				m_device = hid_open_path(cur->path);
				if (m_device)
				{
					hid_set_nonblocking(m_device, 1);
					connected = true;
					hid_free_enumeration(devs);
					return;
				}
			}
		}
		hid_free_enumeration(devs);
	}

	~Impl()
	{
		connected = false;
		if (m_device)
		{
			hid_close(m_device);
			m_device = nullptr;
		}
		hid_exit();
	}

	bool move(int dx, int dy)
	{
		if (!m_device) return false;
		std::array<unsigned char, 65> report{};
		report[0] = 0;
		report[1] = 1;
		report[2] = 0;
		report[3] = static_cast<unsigned char>(std::clamp(dx, -32768, 32767) & 0xFF);
		report[4] = static_cast<unsigned char>((std::clamp(dx, -32768, 32767) >> 8) & 0xFF);
		report[5] = static_cast<unsigned char>(std::clamp(dy, -32768, 32767) & 0xFF);
		report[6] = static_cast<unsigned char>((std::clamp(dy, -32768, 32767) >> 8) & 0xFF);
		report[7] = m_buttonMask;
		int written = hid_write(m_device, report.data(), 65);
		return written == 65;
	}

	void press()
	{
		m_buttonMask |= 0x01;
		move(0, 0);
	}

	void release()
	{
		m_buttonMask &= ~0x01;
		move(0, 0);
	}

private:
	hid_device* m_device = nullptr;
	uint8_t m_buttonMask = 0;
};

Teensy41HidIO::Teensy41HidIO(User* user)
{
	m_User = user;
	m_tracking = std::make_unique<Tracking>(m_User->Network.size);
	ResetSmooth();
	m_impl = std::make_unique<Impl>();
	if (!m_impl->connected)
		throw std::runtime_error("Failed to connect to Teensy 4.1 HID device");
}

Teensy41HidIO::~Teensy41HidIO() = default;

void Teensy41HidIO::Reset()
{
}

void Teensy41HidIO::Move(const int x, const int y)
{
	float sens = (GetPlayerState() == PlayerState::ADS) ? m_User->Ads.sens : m_User->HipFire.sens;
	if (sens <= 0.0f) sens = 1.0f;
	int sx = static_cast<int>(x * (sens / 50.0f));
	int sy = static_cast<int>(y * (sens / 50.0f));
	m_impl->move(sx, sy);
}

void Teensy41HidIO::Shoot()
{
	m_impl->press();
	Sleep(10);
	m_impl->release();
}
