#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <cstdint>
namespace BF2VR {
struct EyePixels { UINT width=0, height=0; std::vector<uint8_t> rgb,alpha; };
// Explicit one-shot diagnostic: Map blocks for GPU readback. Never run per frame.
inline HRESULT ReadEyePixels(ID3D11Device* device, ID3D11DeviceContext* context,
                             ID3D11RenderTargetView* target, EyePixels& pixels) {
    pixels = {};
    if (!device || !context || !target) return E_INVALIDARG;
    D3D11_RENDER_TARGET_VIEW_DESC view{}; target->GetDesc(&view);
    const bool rgba = view.Format==DXGI_FORMAT_R8G8B8A8_UNORM || view.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    const bool bgra = view.Format==DXGI_FORMAT_B8G8R8A8_UNORM || view.Format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    if (!rgba && !bgra) return E_NOTIMPL;
    Microsoft::WRL::ComPtr<ID3D11Resource> resource; target->GetResource(&resource);
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    HRESULT hr=resource.As(&texture); if (FAILED(hr)) return hr;
    D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
    if (desc.SampleDesc.Count!=1 || desc.ArraySize!=1 || desc.MipLevels!=1 || !desc.Width || !desc.Height || desc.Width>16384 || desc.Height>16384) return E_NOTIMPL;
    desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=desc.MiscFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    hr=device->CreateTexture2D(&desc,nullptr,&staging); if (FAILED(hr)) return hr;
    pixels.rgb.resize(static_cast<size_t>(desc.Width)*desc.Height*3);
    pixels.alpha.resize(static_cast<size_t>(desc.Width)*desc.Height);
    context->CopyResource(staging.Get(),texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr=context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped); if (FAILED(hr)) { pixels={}; return hr; }
    for (UINT y=0;y<desc.Height;++y) {
        const auto* row=static_cast<const uint8_t*>(mapped.pData)+static_cast<size_t>(y)*mapped.RowPitch;
        auto* out=pixels.rgb.data()+static_cast<size_t>(y)*desc.Width*3;
        for (UINT x=0;x<desc.Width;++x) {
            pixels.alpha[static_cast<size_t>(y)*desc.Width+x]=row[x*4+3];
            out[x*3]=row[x*4+(rgba?0:2)]; out[x*3+1]=row[x*4+1]; out[x*3+2]=row[x*4+(rgba?2:0)];
        }
    }
    context->Unmap(staging.Get(),0); pixels.width=desc.Width; pixels.height=desc.Height;
    return S_OK;
}
}
