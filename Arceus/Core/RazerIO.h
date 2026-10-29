#pragma once
#include "IOManagement.h"

class RazerIO : public IOHandler
{
public:
	RazerIO(User* user);
	~RazerIO();
private:
	void Move(const int x, const int y) override;
	void Shoot() override;
	class Impl;
	std::unique_ptr<Impl> m_impl;
};
