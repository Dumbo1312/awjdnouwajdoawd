#include "Teensy41IO.h"
#include "User.h"
#include "Target.h"
#include <windows.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

class Teensy41IO::Impl
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

	void move(int x, int y)
	{
		if (!is_open) return;
		write("move " + std::to_string(x) + " " + std::to_string(y) + " 0 0\n");
	}

	void press()
	{
		write("buttons 1\n");
	}

	void release()
	{
		write("buttons 0\n");
	}

private:
	HANDLE m_handle = INVALID_HANDLE_VALUE;
	std::mutex m_mutex;
};

Teensy41IO::Teensy41IO(User* user)
{
	m_User = user;
	m_tracking = std::make_unique<Tracking>(m_User->Network.size);
	ResetSmooth();
	m_impl = std::make_unique<Impl>(user->Input.teensy41Port, user->Input.teensy41Baudrate);
	if (!m_impl->is_open)
		throw std::runtime_error("Failed to open Teensy 4.1 serial port");
}

Teensy41IO::~Teensy41IO() = default;

void Teensy41IO::Reset()
{
}

void Teensy41IO::Move(const int x, const int y)
{
	float sens = (GetPlayerState() == PlayerState::ADS) ? m_User->Ads.sens : m_User->HipFire.sens;
	if (sens <= 0.0f) sens = 1.0f;
	int sx = static_cast<int>(x * (sens / 50.0f));
	int sy = static_cast<int>(y * (sens / 50.0f));
	m_impl->move(sx, sy);
}

void Teensy41IO::Shoot()
{
	m_impl->press();
	Sleep(10);
	m_impl->release();
}
