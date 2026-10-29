#pragma once
#include "IOManagement.h"

class KmboxNetIO : public IOHandler
{
public:
	KmboxNetIO(User* user);
	~KmboxNetIO();
private:
	void Move(const int x, const int y) override;
	void Shoot() override;
	class Impl;
	std::unique_ptr<Impl> m_impl;
};
