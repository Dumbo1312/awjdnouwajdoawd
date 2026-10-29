#pragma once
#include "IOManagement.h"
#include <memory>

class RPBoxController;

class RPBoxIO : public IOHandler
{
public:
	RPBoxIO(User* user);
	~RPBoxIO();
	void Reset() override;
private:
	void Move(const int x, const int y) override;
	void Shoot() override;
	std::unique_ptr<RPBoxController> m_rpbox;
};
