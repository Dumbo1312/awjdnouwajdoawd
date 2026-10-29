#pragma once
#include <cstdint>

#define RPBOX_MAGIC_0       0x52  // 'R'
#define RPBOX_MAGIC_1       0x50  // 'P'
#define RPBOX_VERSION       0x01

#define CMD_PING            0x00
#define CMD_MOVE            0x01
#define CMD_LEFT_CLICK      0x02
#define CMD_RIGHT_CLICK     0x03
#define CMD_KEYBOARD        0x04
#define CMD_WHEEL           0x05
#define CMD_BEEP            0x06
#define CMD_RESET           0x07
#define CMD_MIDDLE_CLICK    0x08

#define REPORT_ID_CMD       0x01
#define REPORT_ID_RESPONSE  0x02

class RPBoxController {
public:
    RPBoxController();
    ~RPBoxController();

    bool initialize();
    void shutdown();
    bool isConnected() const;

    // Mouse control
    bool mouseMove(int16_t x, int16_t y);
    bool mouseClick(int button = 0);  // 0=left, 1=right, 2=middle
    bool mouseDown(int button = 0);
    bool mouseUp(int button = 0);

private:
    void* m_device;
    uint16_t m_vid;
    uint16_t m_pid;

    bool sendCommand(uint8_t cmd, const uint8_t* data = nullptr, size_t dataLen = 0);
    bool getResponse(uint8_t* buffer, size_t bufLen);
};
