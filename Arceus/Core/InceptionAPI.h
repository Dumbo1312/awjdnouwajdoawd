#pragma once

enum class BUTTON { LEFT, RIGHT, MIDDLE };

struct MouseOffsets {
    int buttonOffset;
    int xOffset[2];
    int yOffset[2];
};

class Inception {
public:
    Inception() = default;
    ~Inception() = default;
    
    bool Initialize() { return false; }
    void Start(int port) {}
    void SetInputPacketSize(int size) {}
    void SetOffsets(const MouseOffsets& offsets) {}
    void PushEventMoveReplace(int x, int y, int z, int count) {}
    void PushEventPress(BUTTON btn, int duration, int count) {}
    void Move(int dx, int dy) {}
    void Click() {}
    void MouseDown() {}
    void MouseUp() {}
};
