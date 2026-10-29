#pragma once
#include "IOManagement.h"
#include <cstdint>

class Teensy41HidIO : public IOHandler
{
public:
	Teensy41HidIO(User* user);
	~Teensy41HidIO();
	void Reset() override;
private:
	void Move(const int x, const int y) override;
	void Shoot() override;
	class Impl;
	std::unique_ptr<Impl> m_impl;
};
