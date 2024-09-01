#pragma once

class Mouse
{
public:
    enum Button
    {
        Left = 1,
        Right = 2,
        ButtonCount = 3,
    };

    static bool IsButtonPressed(Button key) { return ms_Pressed[key]; }
    static bool WasButtonPressed(Button key) { return ms_WasPressed[key]; }
    static bool WasButtonReleased(Button key) { return ms_WasReleased[key]; }
    static bool WasHeldFor(Button key, float time) { return (ms_PressedTime[key] >= time) && !ms_WasReleased[key]; }
    static bool WasClicked(Button key, float time) { return ms_WasReleased[key] && (ms_PressedTime[key] < time); }

    // From [0, Resolution.X]
    static float GetX() { return ms_Pos[0]; }

    // From [0, Resolution.Y]
    static float GetY() { return ms_Pos[1]; }

    // positive = wheel up
    static float GetWheel() { return ms_Wheel; }

private:
    static void ProcessMouseMove(::LPARAM lParam, ::RECT rect);
    static void ProcessMouseWheel(::WPARAM wParam) { ms_Wheel += (float)GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA; }
    static void UpdateButton(Button button, bool pressed);
    static void Tick();

    inline static bool ms_Pressed[ButtonCount];
    inline static bool ms_WasPressed[ButtonCount];
    inline static bool ms_WasReleased[ButtonCount];
    inline static float ms_PressedTime[ButtonCount];
    inline static float ms_Pos[2];
    inline static float ms_Wheel;

    friend class Engine;
};
