#pragma once
#include "IOManagement.h"

class Teensy41IO : public IOHandler
{
public:
	Teensy41IO(User* user);
	~Teensy41IO();
	void Reset() override;
private:
	void Move(const int x, const int y) override;
	void Shoot() override;
	class Impl;
	std::unique_ptr<Impl> m_impl;
};
