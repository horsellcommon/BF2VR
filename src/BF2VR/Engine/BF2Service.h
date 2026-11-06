// Copyright Ethan Porcaro

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.

#pragma once
#include <Classes.h>
#include <safetyhook.hpp>
#include <Xinput.h>


namespace BF2VR {

class BF2Service {
 public:
    static int Initialize();
    static bool HudAim(Vec2& output);
    static bool HudReprojection(float* rows);
    static int Uninitialize();
    static void RestoreVisualSettings();
    static void RefreshMainView();
    static void ResetCameraAnchor(bool recenterOnly=false);
    static inline std::atomic<RenderView*> cachedMainView{nullptr};
    static inline uint64_t projectionRevision = 0;
    static inline int projectionEye = -1;
    static inline Matrix4 viewMatrix;
    static inline Matrix4 headCenterMatrix;
    static inline Matrix4 projMatrix;
    static inline XINPUT_GAMEPAD gamepad;
    static inline bool inGame = false;

 private:
    static inline GlobalPostProcessSettings* postSettings;

    static inline SafetyHookMid earlyViewHook;
    static inline SafetyHookInline projectionHook;
    static RenderView* PrepareCamera(RenderView* view);
    static Matrix4* ProjectionOverride(RenderView* view, Matrix4* output, void* camera);

    static inline SafetyHookMid buildViewsHook;
    static void BuildViews(RenderView* view);

    static inline SafetyHookMid resizeScreenHook;

    static inline SafetyHookMid postHook;

    static inline SafetyHookMid gamepadHook;
    static void GamepadUpdate(GamepadState* state);
};

}  // namespace BF2VR
