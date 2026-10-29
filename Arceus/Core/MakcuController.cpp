#include "MakcuController.h"
#include <cstdio>

MakcuController::MakcuController() : m_device(std::make_unique<makcu::Device>()), m_connected(false), m_m4Pressed(false), m_m5Pressed(false) {}

MakcuController::~MakcuController() {
    if (m_connected) {
        m_device->disconnect();
    }
}

bool MakcuController::initialize() {
    try {
        if (m_device->connect()) {
            m_device->setBaudRate(4000000);
            m_device->enableButtonMonitoring(true);
            m_device->setMouseButtonCallback([this](makcu::MouseButton button, bool pressed) {
                onButtonChange(button, pressed);
            });
            m_connected = true;
            return true;
        }
    } catch (const makcu::MakcuException&) {
        m_connected = false;
    }
    return false;
}

void MakcuController::mouseMove(int x, int y) {
    if (m_connected) {
        m_device->mouseMove(x, y);
    }
}

void MakcuController::mouseMoveForce(int x, int y) {
    if (m_connected) {
        m_device->mouseMove(x, y);
    }
}

void MakcuController::mouseClick() {
    if (m_connected) {
        m_device->click(makcu::MouseButton::LEFT);
    }
}

bool MakcuController::isConnected() const {
    return m_connected;
}

void MakcuController::onButtonChange(makcu::MouseButton button, bool pressed) {
    if (button == makcu::MouseButton::SIDE2) {
        m_m5Pressed = pressed;
    } else if (button == makcu::MouseButton::SIDE1) {
        m_m4Pressed = pressed;
    }
}
