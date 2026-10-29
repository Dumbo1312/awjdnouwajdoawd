#pragma once
#include "IOManagement.h"

class KmboxAIO : public IOHandler
{
public:
	KmboxAIO(User* user);
	~KmboxAIO();
private:
	void Move(const int x, const int y) override;
	void Shoot() override;
	class Impl;
	std::unique_ptr<Impl> m_impl;
};
