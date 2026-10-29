#include "ViGEmController.h"
#include <iostream>
#include <algorithm>
#include "../Utilities/Logger.hpp"

ViGEmController::ViGEmController() : m_client(nullptr), m_pad(nullptr), m_connected(false) {
    DS4_REPORT_INIT(&m_report);
}

ViGEmController::~ViGEmController() {
    if (m_pad) {
        vigem_target_remove(m_client, m_pad);
        vigem_target_free(m_pad);
    }
    if (m_client) {
        vigem_disconnect(m_client);
        vigem_free(m_client);
    }
}

bool ViGEmController::initialize() {
    m_client = vigem_alloc();
    if (!m_client) {
        Logger::Warning(L"Input", L"ViGEm: Failed to allocate client");
        return false;
    }

    const auto conn = vigem_connect(m_client);
    if (!VIGEM_SUCCESS(conn)) {
        Logger::Warning(L"Input", L"ViGEm: Connection failed");
        return false;
    }

    m_pad = vigem_target_ds4_alloc();
    const auto add_res = vigem_target_add(m_client, m_pad);
    if (!VIGEM_SUCCESS(add_res)) {
        Logger::Warning(L"Input", L"ViGEm: Target add failed");
        return false;
    }

    m_connected = true;
    Logger::Progress(L"Input", L"ViGEm: Initialized successfully");
    return true;
}

void ViGEmController::controllerMove(int x, int y, float sensitivity) {
    if (!m_connected) return;

    if (x == 0 && y == 0) {
        m_report.RX = 128;
        m_report.RY = 128;
    } else {
        float multiplier = 100.0f * (sensitivity / 5.0f);

        int moveX = static_cast<int>(x * multiplier);
        int moveY = static_cast<int>(y * multiplier);

        m_report.RX = (BYTE)std::clamp(128 + moveX, 0, 255);
        m_report.RY = (BYTE)std::clamp(128 + moveY, 0, 255);
    }

    vigem_target_ds4_update(m_client, m_pad, m_report);
}

void ViGEmController::controllerTrigger(bool pressed) {
    if (!m_connected) return;

    m_report.RTrigger = pressed ? 255 : 0;
    vigem_target_ds4_update(m_client, m_pad, m_report);
}

bool ViGEmController::isConnected() const {
    return m_connected;
}
