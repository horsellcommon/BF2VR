#pragma once
#include <Windows.h>
namespace BF2VR {
struct MenuInput {
    static void Initialize(HWND window);
    static void Shutdown();
    static void Configure(bool enabled,float scale);
    static bool CanScaleMenu();
    static float Scale();
    static bool GameCursorVisible();
    static void UpdateQuestPointer();
    static bool PhysicalCursor(float* values);
    static void SetVisualActive(bool value);
    static void Report();
};
}
