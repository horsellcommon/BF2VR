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
#include <d3d11.h>
#include <atomic>
#include <FrameBlitter.h>
#include <memory>
#include <filesystem>

#include <safetyhook.hpp>

namespace BF2VR {

class D3DService {
 public:
    static int Initialize();
    static void ConfigureStartupHud(const std::string& shader);
    static int CaptureResources();
    static void RequestHudPreview(const std::filesystem::path& file);
    static void SelectHudSurface(int index);
    static void QueueHudShader(const std::string& code);
    static void EnableHudTransform(bool enable);
    static void RequestHudAudit(const std::filesystem::path& file);
    static void RequestStereoCapture(const std::filesystem::path& directory);
    static int Uninitialize();

    static inline ID3D11Device* pDevice;
    static inline ID3D11DeviceContext* pContext;
    static inline IDXGISwapChain* pSwapChain;
    static inline void* presentAddress = nullptr;
    static inline HWND gameWindow = nullptr;
    static inline ID3D11RenderTargetView* uiRTV;
    static inline ID3D11Texture2D* uiBuffer;

 private:
    static inline std::unique_ptr<FrameBlitter> frameBlitter;
    static inline SafetyHookInline presentHook;
    static HRESULT PresentDetour(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags);

    static inline SafetyHookInline resourcesHook;
    static void ResourcesDetour(ID3D11DeviceContext*,UINT,UINT,ID3D11ShaderResourceView* const*);
    static inline SafetyHookInline indexedHook, indexedInstancedHook, instancedHook;
    static void IndexedDetour(ID3D11DeviceContext*,UINT,UINT,INT);
    static void IndexedInstancedDetour(ID3D11DeviceContext*,UINT,UINT,UINT,INT,UINT);
    static void InstancedDetour(ID3D11DeviceContext*,UINT,UINT,UINT,UINT);
    static inline SafetyHookInline drawHook;
    static void DrawDetour(ID3D11DeviceContext* pContext, UINT VertexCount, UINT StartVertexLocation);

    static inline SafetyHookMid uiHook;
    static void OnUIDraw();
    static void RestoreUITargets();
    static inline bool uiTargetsSaved = false;
    static inline ID3D11RenderTargetView* savedUI[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    static inline ID3D11DepthStencilView* savedDepth = nullptr;

    static inline ID3D11VertexShader* pVertexShader = nullptr;
    static inline ID3D11PixelShader* pPixelShader = nullptr;

    enum DrawStage {
       WAITED,
       DRAWING_L,
       DRAWN_L,
       DRAWING_R,
       DRAWN_R,
       COMPLETE
    };

    static inline DrawStage drawStage = COMPLETE;
};

}  // namespace BF2VR

