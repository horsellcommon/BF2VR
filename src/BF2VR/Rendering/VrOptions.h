#pragma once
#include <d3d11.h>
#include <filesystem>

namespace BF2VR {
struct VrOptions {
    static void Initialize(const std::filesystem::path& directory);
    static bool Key(UINT message,WPARAM key,LPARAM flags);
    static bool Update();
    static bool IsVisible();
    static constexpr UINT Width=640,Height=520;
    static bool Draw(ID3D11Device* device,ID3D11DeviceContext* context,
        ID3D11RenderTargetView* target,UINT width,UINT height);
    static void Shutdown();
};
}
