#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
namespace BF2VR {
struct DrawSnapshot {
    unsigned kind=0,count=0,instances=0;
    uintptr_t pixel=0,vertex=0,target=0;
    UINT width=0,height=0; bool depth=false,blend=false;
    D3D11_VIEWPORT viewport{};
};
inline DrawSnapshot SnapshotDraw(ID3D11DeviceContext* context,unsigned kind,unsigned count,unsigned instances) {
    DrawSnapshot out{};out.kind=kind;out.count=count;out.instances=instances;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> ps;context->PSGetShader(&ps,nullptr,nullptr);out.pixel=reinterpret_cast<uintptr_t>(ps.Get());
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;context->VSGetShader(&vs,nullptr,nullptr);out.vertex=reinterpret_cast<uintptr_t>(vs.Get());
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;context->OMGetRenderTargets(1,&target,nullptr);out.target=reinterpret_cast<uintptr_t>(target.Get());
    if(target) { Microsoft::WRL::ComPtr<ID3D11Resource> resource;target->GetResource(&resource);Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        if(SUCCEEDED(resource.As(&texture))) { D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);out.width=desc.Width;out.height=desc.Height; } }
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth;UINT stencil=0;context->OMGetDepthStencilState(&depth,&stencil);
    out.depth=true;if(depth) { D3D11_DEPTH_STENCIL_DESC desc{};depth->GetDesc(&desc);out.depth=desc.DepthEnable!=FALSE; }
    Microsoft::WRL::ComPtr<ID3D11BlendState> blend;FLOAT factors[4]{};UINT mask=0;context->OMGetBlendState(&blend,factors,&mask);
    if(blend) { D3D11_BLEND_DESC desc{};blend->GetDesc(&desc);out.blend=desc.RenderTarget[0].BlendEnable!=FALSE; }
    UINT n=1;context->RSGetViewports(&n,&out.viewport);return out;
}
}
