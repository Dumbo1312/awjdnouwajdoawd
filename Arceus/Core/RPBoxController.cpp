#include "RPBoxController.h"
#include "hidapi.h"
#include <cstring>
#include <thread>
#include <chrono>

RPBoxController::RPBoxController() 
    : m_device(nullptr), m_vid(0), m_pid(0) 
{
    hid_init();
}

RPBoxController::~RPBoxController() 
{
    shutdown();
    hid_exit();
}

bool RPBoxController::sendCommand(uint8_t cmd, const uint8_t* data, size_t dataLen)
{
    if (!m_device) return false;

    uint8_t packet[9] = {0};
    packet[0] = REPORT_ID_CMD;
    packet[1] = RPBOX_MAGIC_0;
    packet[2] = RPBOX_MAGIC_1;
    packet[3] = cmd;

    if (data && dataLen > 0) {
        size_t copyLen = (dataLen > 5) ? 5 : dataLen;
        memcpy(&packet[4], data, copyLen);
    }

    int res = hid_send_feature_report((hid_device*)m_device, packet, 9);
    return res >= 0;
}

bool RPBoxController::getResponse(uint8_t* buffer, size_t bufLen) 
{
    if (!m_device || bufLen < 9) return false;

    int res = hid_get_feature_report((hid_device*)m_device, buffer, bufLen);
    return res >= 0;
}

bool RPBoxController::initialize() 
{
    struct hid_device_info* devs = hid_enumerate(0, 0);
    struct hid_device_info* cur = devs;

    while (cur) {
        if (cur->usage_page >= 0xFF00) {
            hid_device* dev = hid_open_path(cur->path);
            if (dev) {
                hid_set_nonblocking(dev, 1);

                uint8_t packet[9] = {REPORT_ID_CMD, RPBOX_MAGIC_0, RPBOX_MAGIC_1, CMD_PING, 0, 0, 0, 0, 0};
                hid_send_feature_report(dev, packet, 9);

                std::this_thread::sleep_for(std::chrono::milliseconds(50));

                uint8_t response[9] = {0};
                response[0] = REPORT_ID_RESPONSE;
                if (hid_get_feature_report(dev, response, 9) >= 0) {
                    if (response[1] == RPBOX_MAGIC_0 && response[2] == RPBOX_MAGIC_1) {
                        m_device = dev;
                        m_vid = cur->vendor_id;
                        m_pid = cur->product_id;
                        hid_free_enumeration(devs);
                        return true;
                    }
                }
                hid_close(dev);
            }
        }
        cur = cur->next;
    }

    hid_free_enumeration(devs);
    return false;
}

void RPBoxController::shutdown() 
{
    if (m_device) {
        sendCommand(CMD_RESET);
        hid_close((hid_device*)m_device);
        m_device = nullptr;
    }
    m_vid = 0;
    m_pid = 0;
}

bool RPBoxController::isConnected() const 
{
    return m_device != nullptr;
}

bool RPBoxController::mouseMove(int16_t x, int16_t y) 
{
    uint8_t data[4];
    data[0] = x & 0xFF;
    data[1] = (x >> 8) & 0xFF;
    data[2] = y & 0xFF;
    data[3] = (y >> 8) & 0xFF;
    return sendCommand(CMD_MOVE, data, 4);
}

bool RPBoxController::mouseClick(int button) 
{
    mouseDown(button);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return mouseUp(button);
}

bool RPBoxController::mouseDown(int button) 
{
    uint8_t data[1] = {1};
    switch (button) {
        case 0: return sendCommand(CMD_LEFT_CLICK, data, 1);
        case 1: return sendCommand(CMD_RIGHT_CLICK, data, 1);
        case 2: return sendCommand(CMD_MIDDLE_CLICK, data, 1);
    }
    return false;
}

bool RPBoxController::mouseUp(int button) 
{
    uint8_t data[1] = {0};
    switch (button) {
        case 0: return sendCommand(CMD_LEFT_CLICK, data, 1);
        case 1: return sendCommand(CMD_RIGHT_CLICK, data, 1);
        case 2: return sendCommand(CMD_MIDDLE_CLICK, data, 1);
    }
    return false;
}
