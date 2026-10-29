#include "RP2350IO.h"
#include "User.h"
#include "Target.h"
#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>

class RP2350IO::Impl
{
public:
	bool is_open = false;

	Impl(const std::string& port, unsigned int baudrate)
	{
		std::string path = "\\\\.\\" + port;
		m_handle = CreateFileA(path.c_str(),
			GENERIC_READ | GENERIC_WRITE,
			0, NULL, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL, NULL);

		if (m_handle == INVALID_HANDLE_VALUE)
			return;

		DCB dcb = { 0 };
		dcb.DCBlength = sizeof(DCB);
		GetCommState(m_handle, &dcb);
		dcb.BaudRate = baudrate;
		dcb.ByteSize = 8;
		dcb.Parity = NOPARITY;
		dcb.StopBits = ONESTOPBIT;
		SetCommState(m_handle, &dcb);

		COMMTIMEOUTS timeouts = { 0 };
		timeouts.ReadIntervalTimeout = 50;
		timeouts.ReadTotalTimeoutConstant = 50;
		timeouts.ReadTotalTimeoutMultiplier = 10;
		timeouts.WriteTotalTimeoutConstant = 50;
		timeouts.WriteTotalTimeoutMultiplier = 10;
		SetCommTimeouts(m_handle, &timeouts);

		is_open = true;
	}

	~Impl()
	{
		is_open = false;
		if (m_handle != INVALID_HANDLE_VALUE)
			CloseHandle(m_handle);
	}

	void write(const std::string& data)
	{
		if (!is_open || m_handle == INVALID_HANDLE_VALUE) return;
		std::lock_guard<std::mutex> lock(m_mutex);
		DWORD written = 0;
		WriteFile(m_handle, data.c_str(), (DWORD)data.size(), &written, NULL);
	}

	void sendCommand(const std::string& cmd)
	{
		write(cmd + "\n");
	}

	void move(int x, int y)
	{
		if (!is_open) return;
		sendCommand("m" + std::to_string(x) + "," + std::to_string(y));
	}

	void press()
	{
		sendCommand("p");
	}

	void release()
	{
		sendCommand("r");
	}

private:
	HANDLE m_handle = INVALID_HANDLE_VALUE;
	std::mutex m_mutex;
};

RP2350IO::RP2350IO(User* user)
{
	m_User = user;
	m_tracking = std::make_unique<Tracking>(m_User->Network.size);
	ResetSmooth();
	m_impl = std::make_unique<Impl>(user->Input.rp2350Port, user->Input.rp2350Baudrate);
	if (!m_impl->is_open)
		throw std::runtime_error("Failed to open RP2350 serial port");
}

RP2350IO::~RP2350IO() = default;

void RP2350IO::Reset()
{
}

void RP2350IO::Move(const int x, const int y)
{
	float sens = (GetPlayerState() == PlayerState::ADS) ? m_User->Ads.sens : m_User->HipFire.sens;
	if (sens <= 0.0f) sens = 1.0f;
	int sx = static_cast<int>(x * (sens / 50.0f));
	int sy = static_cast<int>(y * (sens / 50.0f));
	m_impl->move(sx, sy);
}

void RP2350IO::Shoot()
{
	m_impl->press();
	Sleep(10);
	m_impl->release();
}
