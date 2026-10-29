#pragma once
#include "IOManagement.h"
#include <memory>

class MakcuController;

class MakcuIO : public IOHandler
{
public:
	MakcuIO(User* user);
	~MakcuIO();
	void Reset() override;
private:
	void Move(const int x, const int y) override;
	void Shoot() override;
	std::unique_ptr<MakcuController> m_makcu;
};
