#pragma once
#include <FrameBlitter.h>
namespace BF2VR {
// A depth view may stay bound while the UI pass disables both tests.
inline bool HudOverlayDepthIndependent(bool viewBound, const D3D11_DEPTH_STENCIL_DESC* state) {
    return !viewBound || (state && !state->DepthEnable && !state->StencilEnable);
}

// GPU-only transform of a selected UI texture. The producer texture is never modified.
class HudTransform {
    FrameBlitter blitter;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> output;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    UINT width=0,height=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
public:
    HRESULT Initialize(ID3D11Device* d,ID3D11DeviceContext* c) { device=d;return blitter.Initialize(d,c); }
    ID3D11RenderTargetView* Target() const { return target.Get(); }
    HRESULT SetCursor(const float* values) { return blitter.SetPixelConstants(values,16); }
    HRESULT SetShader(const std::string& code,std::string& errors) { return blitter.SetPixelShaderSource(code,errors); }
    HRESULT Apply(ID3D11ShaderResourceView* source,ID3D11ShaderResourceView** result,ID3D11ShaderResourceView* overlay=nullptr) {
        *result=nullptr;if(!source || !device)return E_INVALIDARG;
        D3D11_SHADER_RESOURCE_VIEW_DESC srv{};source->GetDesc(&srv);
        if(srv.ViewDimension!=D3D11_SRV_DIMENSION_TEXTURE2D)return E_NOTIMPL;
        Microsoft::WRL::ComPtr<ID3D11Resource> resource;source->GetResource(&resource);
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;HRESULT hr=resource.As(&texture);if(FAILED(hr))return hr;
        D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
        if(desc.SampleDesc.Count!=1 || desc.ArraySize!=1 || desc.MipLevels!=1)return E_NOTIMPL;
        if(!output || width!=desc.Width || height!=desc.Height || format!=srv.Format) {
            output.Reset();target.Reset();view.Reset();
            desc.Format=srv.Format;desc.Usage=D3D11_USAGE_DEFAULT;desc.CPUAccessFlags=desc.MiscFlags=0;
            desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
            hr=device->CreateTexture2D(&desc,nullptr,&output);if(FAILED(hr))return hr;
            hr=device->CreateRenderTargetView(output.Get(),nullptr,&target);if(FAILED(hr))return hr;
            hr=device->CreateShaderResourceView(output.Get(),nullptr,&view);if(FAILED(hr))return hr;
            width=desc.Width;height=desc.Height;format=srv.Format;
        }
        hr=blitter.Copy(texture.Get(),srv.Format,target.Get(),width,height,0.f,overlay);
        if(SUCCEEDED(hr))*result=view.Get();return hr;
    }
};
// Prepare at the consuming draw, never at the earlier resource-binding call.
// Restore exactly the binding the game expects after this draw.
class HudDrawBinding {
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> original;
    UINT slot=0;bool changed=false;
public:
    HudDrawBinding()=default;
    HudDrawBinding(const HudDrawBinding&)=delete;
    HudDrawBinding& operator=(const HudDrawBinding&)=delete;
    ~HudDrawBinding() { if(changed) { auto* value=original.Get();context->PSSetShaderResources(slot,1,&value); } }
    HRESULT Prepare(ID3D11DeviceContext* input,HudTransform& transform,ID3D11Resource* expected,UINT bindingSlot,ID3D11ShaderResourceView* overlay=nullptr) {
        if(changed || !input || !expected || bindingSlot>=D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT)return E_INVALIDARG;
        context=input;slot=bindingSlot;input->PSGetShaderResources(slot,1,&original);
        if(!original)return S_FALSE;
        Microsoft::WRL::ComPtr<ID3D11Resource> resource;original->GetResource(&resource);
        if(resource.Get()!=expected)return S_FALSE;
        ID3D11RenderTargetView* targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
        input->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,targets,nullptr);
        bool producer=false;
        for(auto* target:targets) if(target) {
            Microsoft::WRL::ComPtr<ID3D11Resource> output;target->GetResource(&output);
            producer |= output.Get()==expected;target->Release();
        }
        if(producer)return S_FALSE;
        ID3D11ShaderResourceView* replacement=nullptr;
        const HRESULT result=transform.Apply(original.Get(),&replacement,overlay);
        if(FAILED(result))return result;
        if(!replacement)return E_FAIL;
        input->PSSetShaderResources(slot,1,&replacement);changed=true;return S_OK;
    }
};
}
