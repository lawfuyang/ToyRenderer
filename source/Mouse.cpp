#include "Mouse.h"

#include "Engine.h"

void Mouse::ProcessMouseMove(::LPARAM lParam, ::RECT rect)
{
    const int32_t x = GET_X_LPARAM(lParam) - rect.left;
    const int32_t y = GET_Y_LPARAM(lParam) - rect.top;

    ms_Pos[0] = (float)x;
    ms_Pos[1] = (float)y;
}

void Mouse::UpdateButton(Button button, bool pressed)
{
    const uint32_t buttonIdx = (int32_t)button;

    if (!pressed && ms_Pressed[buttonIdx])
        ms_WasReleased[buttonIdx] = true;
    if (pressed && !ms_Pressed[buttonIdx])
        ms_WasPressed[buttonIdx] = true;

    ms_Pressed[buttonIdx] = pressed;
}

void Mouse::Tick()
{
    memset(ms_WasPressed, 0, sizeof(ms_WasPressed));
    memset(ms_WasReleased, 0, sizeof(ms_WasReleased));

    ms_Wheel = 0.0f;

    for (uint32_t i = 0; i < _countof(ms_PressedTime); ++i)
    {
        if (ms_Pressed[i])
            ms_PressedTime[i] += (float)g_Engine.m_CPUFrameTimeMs;
        else
            ms_PressedTime[i] = 0;
    }
}
