#pragma once
#include "IOManagement.h"

class ArduinoIO : public IOHandler
{
public:
	ArduinoIO(User* user);
	~ArduinoIO();
	void Reset() override;
private:
	void Move(const int x, const int y) override;
	void Shoot() override;
	class Impl;
	std::unique_ptr<Impl> m_impl;
};
