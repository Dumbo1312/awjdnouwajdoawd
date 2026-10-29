#pragma once
#include <atomic>
#include <thread>
#include <windows.h>

class MouseDeltaTracker
{
public:
	MouseDeltaTracker();
	~MouseDeltaTracker();

	void GetDelta(int& dx, int& dy);

private:
	static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	void ThreadFunc();

	std::atomic<long> m_dx{ 0 };
	std::atomic<long> m_dy{ 0 };
	std::thread m_thread;
	std::atomic<bool> m_running{ true };
	std::atomic<bool> m_ready{ false };
	HWND m_hwnd = nullptr;
};
