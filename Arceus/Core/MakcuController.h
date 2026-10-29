#pragma once
#include <makcu/makcu.h>
#include <memory>
#include <atomic>

class MakcuController {
public:
    MakcuController();
    ~MakcuController();

    bool initialize();
    void mouseMove(int x, int y);
    void mouseMoveForce(int x, int y);
    void mouseClick();
    bool isConnected() const;
    bool isM4Pressed() const { return m_m4Pressed; }
    bool isM5Pressed() const { return m_m5Pressed; }

private:
    std::unique_ptr<makcu::Device> m_device;
    bool m_connected;
    std::atomic<bool> m_m4Pressed;
    std::atomic<bool> m_m5Pressed;
    
    void onButtonChange(makcu::MouseButton button, bool pressed);
};
