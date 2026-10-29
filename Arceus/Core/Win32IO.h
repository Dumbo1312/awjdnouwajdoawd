#pragma once
#include "IOManagement.h"

class Win32IO : public IOHandler
{
public:
	Win32IO(User* user);
	~Win32IO();

private:
	void Move(const int x, const int y) override;
	void Shoot() override;
};
