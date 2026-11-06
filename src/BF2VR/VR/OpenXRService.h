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
#define XR_USE_GRAPHICS_API_D3D11
#include <d3d11.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <array>
#include <vector>
#include <FrameCycle.h>
#include <atomic>
namespace BF2VR {
class OpenXRService {
 public:
    static int Initialize();
    static void HandleStates();
    static void UpdatePose();
    static void RecordNativeFov(const XrFovf& fov);
    static int RenderEye() { return cycle.eye; }
    static inline uint64_t poseRevision = 0;
    static bool PrepareDraw();
    static bool FinishEye();
    static int Uninitialize();
    static ID3D11RenderTargetView* EyeTarget();
    static ID3D11RenderTargetView* UITarget();
    static inline XrSession xrSession = XR_NULL_HANDLE;
    static inline XrInstance xrInstance = XR_NULL_HANDLE;
    static inline XrFrameState xrFrameState{XR_TYPE_FRAME_STATE};
    static inline XrSpace xrSpace = XR_NULL_HANDLE;
    static inline bool xrRunning = false, xrFocused = false, xrExiting = false;
    static inline bool poseValid = false;
    static inline bool uiLayerEnabled = true;
    static inline std::atomic_bool renderReady = false;
    static inline int swapchainWidth = 0, swapchainHeight = 0;
 private:
    static bool DestroySwapchains();
    static bool DestroyChain(XrSwapchain& chain,std::vector<ID3D11RenderTargetView*>& views);
    static bool CreateChain(int width,int height,XrSwapchain& chain,std::vector<ID3D11RenderTargetView*>& views);
    static void ApplyProjectionScale();
    static inline double maxProjectionScale=1.0;
    static inline float attemptedScale=1.f;
    static inline int64_t swapchainFormat=0;
    static inline XrSpace optionsSpace=XR_NULL_HANDLE;
    static inline bool optionsDrawn=false;
    static inline int hudWidth=0,hudHeight=0;
    static bool Check(XrResult result, const char* operation);
    static bool Acquire(int chain);
    static bool Release(int chain);
    static bool EndFrame(bool submit);
    static inline FrameCycle cycle;
    static inline std::array<XrFovf,2> nativeFovs{};
    static inline std::array<bool,2> nativeFovValid{};
    static inline bool shouldRender = false;
    static inline std::array<XrView, 2> views;
    static inline std::array<XrCompositionLayerProjectionView, 2> projectionViews;
    static inline std::array<XrSwapchain, 3> swapchains{};
    static inline std::array<std::vector<ID3D11RenderTargetView*>, 3> targets;
    static inline std::array<uint32_t, 3> imageIndices{};
    static inline std::array<bool, 3> acquired{}, writable{};
    static inline XrEnvironmentBlendMode blendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    static inline XrCompositionLayerQuad hud{XR_TYPE_COMPOSITION_LAYER_QUAD};
};
}
