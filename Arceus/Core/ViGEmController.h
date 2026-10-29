#pragma once
#include <windows.h>
#include <ViGEmClient.h>
#include <memory>

class ViGEmController {
public:
    ViGEmController();
    ~ViGEmController();

    bool initialize();
    void controllerMove(int x, int y, float sensitivity);
    void controllerTrigger(bool pressed);
    bool isConnected() const;

private:
    PVIGEM_CLIENT m_client;
    PVIGEM_TARGET m_pad;
    DS4_REPORT m_report;
    bool m_connected;
};
