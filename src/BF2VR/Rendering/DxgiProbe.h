#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

namespace BF2VR {
// Obtain an actual DXGI method, without relying on a reverse-engineered engine
// pointer being an IDXGISwapChain. The hidden probe never renders game content.
struct DxgiProbe {
    HWND window = nullptr;
    Microsoft::WRL::ComPtr<IDXGISwapChain> chain;
    ~DxgiProbe() { chain.Reset(); if (window) DestroyWindow(window); }
    HRESULT Create(ID3D11Device* device) {
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        Microsoft::WRL::ComPtr<IDXGIFactory> factory;
        HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
        if (FAILED(hr)) return hr;
        hr = dxgiDevice->GetAdapter(&adapter); if (FAILED(hr)) return hr;
        hr = adapter->GetParent(IID_PPV_ARGS(&factory)); if (FAILED(hr)) return hr;
        window = CreateWindowExW(0, L"STATIC", L"BF2VR DXGI probe", WS_POPUP,
            0, 0, 16, 16, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!window) return HRESULT_FROM_WIN32(GetLastError());
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferDesc.Width = desc.BufferDesc.Height = 16;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 1; desc.OutputWindow = window; desc.Windowed = TRUE;
        return factory->CreateSwapChain(device, &desc, &chain);
    }
    void* PresentAddress() const { return (*reinterpret_cast<void***>(chain.Get()))[8]; }
};
inline bool MatchesGameSwapchain(IDXGISwapChain* chain, ID3D11Device* device, HWND window) {
    DXGI_SWAP_CHAIN_DESC desc{};
    Microsoft::WRL::ComPtr<ID3D11Device> owner;
    return window && SUCCEEDED(chain->GetDesc(&desc)) && desc.OutputWindow == window &&
        SUCCEEDED(chain->GetDevice(IID_PPV_ARGS(&owner))) && owner.Get() == device;
}
}
