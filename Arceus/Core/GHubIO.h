#pragma once
#include "IOManagement.h"

class GHubIO : public IOHandler
{
public:
	GHubIO(User* user);
	~GHubIO();
private:
	void Move(const int x, const int y) override;
	void Shoot() override;
	class Impl;
	std::unique_ptr<Impl> m_impl;
};
