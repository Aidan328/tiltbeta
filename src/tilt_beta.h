
#pragma once
#include "Gfx.h"

class tilt_beta: public Cubios::Application 
{
public:
    virtual void on_PhysicsTick(const std::array<Cubios::Screen, 3>&) override;
    virtual void on_Twist(const Cubios::TOPOLOGY_twistInfo_t&) override;
    virtual void on_Message(uint32_t, uint8_t*, u32_t) override;
    virtual void on_ExternalMessage(uint8_t*, u32_t) override;
    virtual void on_Pat(uint32_t) override;
    virtual void on_Render(std::array<Cubios::Screen, 3>& screens) override;
    virtual void on_Tick(uint32_t, uint32_t) override;
    virtual void on_Timer(uint8_t) override;
    virtual void on_Close() override;

private:
    const int SCREEN_SIZE  = 240;
    const int RECT_WIDTH  = 100;
    const int RECT_HEIGHT = 100;
    const int RECT_X = (SCREEN_SIZE - RECT_WIDTH) / 2;
    const int RECT_Y = (SCREEN_SIZE - RECT_HEIGHT) / 2;
};