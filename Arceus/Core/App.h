#pragma once
#include <memory>

class User;
class ScreenCapture;
class IOHandler;
class OnnxRT;

class App
{
public:
	App();

	void Run();

	~App();

private:
	bool m_Running = true;
	std::unique_ptr<User> m_User;
	std::unique_ptr<ScreenCapture> m_Screen;
	std::unique_ptr<OnnxRT> m_Detector;
	std::unique_ptr<IOHandler> m_Player;
};
