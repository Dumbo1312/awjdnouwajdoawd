#include "MouseDeltaTracker.h"
#include "CrashLog.h"

static MouseDeltaTracker* g_instance = nullptr;

MouseDeltaTracker::MouseDeltaTracker()
{
	g_instance = this;
	m_thread = std::thread(&MouseDeltaTracker::ThreadFunc, this);

	while (!m_ready.load())
		Sleep(1);
}

MouseDeltaTracker::~MouseDeltaTracker()
{
	m_running.store(false);
	if (m_hwnd)
		PostMessage(m_hwnd, WM_QUIT, 0, 0);
	if (m_thread.joinable())
		m_thread.join();
	g_instance = nullptr;
}

void MouseDeltaTracker::GetDelta(int& dx, int& dy)
{
	dx = static_cast<int>(m_dx.exchange(0));
	dy = static_cast<int>(m_dy.exchange(0));
}

LRESULT CALLBACK MouseDeltaTracker::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_INPUT && g_instance)
	{
		UINT size = 0;
		GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));

		if (size <= sizeof(RAWINPUT))
		{
			RAWINPUT raw{};
			UINT copied = size;
			if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &raw, &copied, sizeof(RAWINPUTHEADER)) == size)
			{
				if (raw.header.dwType == RIM_TYPEMOUSE &&
					(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0)
				{
					g_instance->m_dx.fetch_add(raw.data.mouse.lLastX);
					g_instance->m_dy.fetch_add(raw.data.mouse.lLastY);
				}
			}
		}
		return 0;
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}

void MouseDeltaTracker::ThreadFunc()
{
	CrashLog::Log(("THREAD MouseDeltaTracker::ThreadFunc tid=" + std::to_string(GetCurrentThreadId())).c_str());
	const wchar_t CLASS_NAME[] = L"SenyMouseDeltaTracker";

	WNDCLASSW wc{};
	wc.lpfnWndProc = WndProc;
	wc.hInstance = GetModuleHandle(nullptr);
	wc.lpszClassName = CLASS_NAME;
	RegisterClassW(&wc);

	m_hwnd = CreateWindowExW(0, CLASS_NAME, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);

	RAWINPUTDEVICE rid{};
	rid.usUsagePage = 0x01;
	rid.usUsage = 0x02;
	rid.dwFlags = RIDEV_INPUTSINK;
	rid.hwndTarget = m_hwnd;
	RegisterRawInputDevices(&rid, 1, sizeof(rid));

	m_ready.store(true);

	MSG msg;
	while (m_running.load())
	{
		BOOL ret = GetMessage(&msg, m_hwnd, 0, 0);
		if (ret <= 0) break;
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	DestroyWindow(m_hwnd);
	UnregisterClassW(CLASS_NAME, wc.hInstance);
}
