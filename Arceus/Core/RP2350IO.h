#pragma once
#include "IOManagement.h"

class RP2350IO : public IOHandler
{
public:
	RP2350IO(User* user);
	~RP2350IO();
	void Reset() override;
private:
	void Move(const int x, const int y) override;
	void Shoot() override;
	class Impl;
	std::unique_ptr<Impl> m_impl;
};
