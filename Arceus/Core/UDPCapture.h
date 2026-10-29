#pragma once
#include "ScreenCapture.h"
#include <d3d11.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wincodec.h>
#include <atomic>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

class User;

#pragma comment(lib, "ws2_32.lib")

class UDPCapture : public ScreenCapture
{
public:
	UDPCapture(User* user, const std::string& ip = "0.0.0.0", int port = 1234);
	~UDPCapture();
	bool GetScreen(Frame& frame) override;
private:
	User* m_User;
	int m_width = 0, m_height = 0;
	std::string m_ip;
	int m_port;
	SOCKET m_socket = INVALID_SOCKET;
	sockaddr_in m_serverAddr{};
	std::atomic<bool> m_connected{ false };
	std::atomic<bool> m_initialized{ false };
	std::atomic<bool> m_stop{ false };
	std::thread m_recvThread;
	std::mutex m_mutex;
	std::queue<std::vector<uint8_t>> m_queue;

	IWICImagingFactory* m_wicFactory = nullptr;
	ID3D11Device* m_device = nullptr;
	ID3D11DeviceContext* m_context = nullptr;
	ID3D11Texture2D* m_sharedTexture = nullptr;
	ID3D11Texture2D* m_stagingTexture = nullptr;
	HANDLE m_sharedHandle = nullptr;

	static const int MAX_FRAME_SIZE = 1024 * 1024;
	static const int MAX_QUEUE_SIZE = 5;

	void ReceiveThread();
	bool DecodeJPEG(const std::vector<uint8_t>& jpeg, std::vector<uint8_t>& bgra, int& w, int& h);
	void CleanupD3D();

	template<typename T>
	void SafeRelease(T*& ptr)
	{
		if (ptr) { ptr->Release(); ptr = nullptr; }
	}
};
