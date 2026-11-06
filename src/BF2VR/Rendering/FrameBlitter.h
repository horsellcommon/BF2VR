#pragma once
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <GpuCopyTimer.h>
#include <string>
#include <cmath>

namespace BF2VR {
// Copy the finished per-eye game frame, rather than replaying intermediate
// engine draws. SwapDeviceContextState isolates every graphics-pipeline change.
class FrameBlitter {
    template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> device;
    Ptr<ID3D11DeviceContext1> context;
    Ptr<ID3DDeviceContextState> state;
    Ptr<ID3D11VertexShader> vertex;
    Ptr<ID3D11PixelShader> pixel,identityPixel,eyeSharpenPixel;
    Ptr<ID3D11BlendState> overlayBlend;
    Ptr<ID3D11SamplerState> sampler;
    Ptr<ID3D11Texture2D> copy;
    Ptr<ID3D11ShaderResourceView> source;
    Ptr<ID3D11Buffer> pixelConstants;
    UINT constantBytes=0;
    float eyeSharpness=0;
    GpuCopyTimer gpuTimer;
    UINT width = 0, height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
public:
    HRESULT Initialize(ID3D11Device* input, ID3D11DeviceContext* immediate) {
        device = input;
        gpuTimer.Initialize(input);
        HRESULT hr = immediate->QueryInterface(IID_PPV_ARGS(&context)); if (FAILED(hr)) return hr;
        Ptr<ID3D11Device1> device1;
        hr = device.As(&device1); if (FAILED(hr)) return hr;
        D3D_FEATURE_LEVEL level = device->GetFeatureLevel(), chosen{};
        hr = device1->CreateDeviceContextState(0, &level, 1, D3D11_SDK_VERSION, __uuidof(ID3D11Device), &chosen, &state);
        if (FAILED(hr)) return hr;
        const char* shader = R"(
struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Vertex VS(uint id : SV_VertexID) {
    Vertex v; v.uv = float2((id << 1) & 2, id & 2);
    v.position = float4(v.uv * float2(2,-2) + float2(-1,1), 0, 1); return v;
}
Texture2D image : register(t0); SamplerState linearClamp : register(s0);
float4 PS(Vertex v) : SV_Target { return image.Sample(linearClamp, v.uv); }
)";
        Ptr<ID3DBlob> code, error;
        hr = D3DCompile(shader, strlen(shader), nullptr, nullptr, nullptr, "VS", "vs_5_0", 0, 0, &code, &error);
        if (FAILED(hr)) return hr;
        hr = device->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &vertex); if (FAILED(hr)) return hr;
        code.Reset(); error.Reset();
        hr = D3DCompile(shader, strlen(shader), nullptr, nullptr, nullptr, "PS", "ps_5_0", 0, 0, &code, &error);
        if (FAILED(hr)) return hr;
        hr = device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &pixel); if (FAILED(hr)) return hr;
        identityPixel=pixel;
        D3D11_SAMPLER_DESC desc{};
        desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.MaxLOD = D3D11_FLOAT32_MAX;
        return device->CreateSamplerState(&desc, &sampler);
    }
    HRESULT SetPixelConstants(const void* data,UINT bytes) {
        if(!device || !context || !data || !bytes || bytes%16 || bytes>65536)return E_INVALIDARG;
        if(!pixelConstants || bytes!=constantBytes) {
            pixelConstants.Reset();D3D11_BUFFER_DESC desc{};desc.ByteWidth=bytes;
            desc.Usage=D3D11_USAGE_DYNAMIC;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
            HRESULT hr=device->CreateBuffer(&desc,nullptr,&pixelConstants);if(FAILED(hr))return hr;constantBytes=bytes;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr=context->Map(pixelConstants.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped);if(FAILED(hr))return hr;
        memcpy(mapped.pData,data,bytes);context->Unmap(pixelConstants.Get(),0);return S_OK;
    }
    HRESULT SetPixelShaderSource(const std::string& text,std::string& errors) {
        if(!device)return E_UNEXPECTED;
        Ptr<ID3DBlob> code,error;
        HRESULT hr=D3DCompile(text.data(),text.size(),nullptr,nullptr,nullptr,"PS","ps_5_0",0,0,&code,&error);
        if(error)errors.assign(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize());
        if(FAILED(hr))return hr;
        Ptr<ID3D11PixelShader> next;
        hr=device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&next);
        if(SUCCEEDED(hr))pixel=next;return hr;
    }
    HRESULT SetEyeSharpness(float amount) {
        if(!std::isfinite(amount) || amount<0.f || amount>.5f)return E_INVALIDARG;
        if(amount==eyeSharpness)return S_OK;
        if(amount==0.f) {
            pixel=identityPixel;
            eyeSharpness=0.f;
            return S_OK;
        }
        if(!eyeSharpenPixel) {
            const char* shader=R"(
Texture2D image:register(t0);
SamplerState linearClamp:register(s0);
cbuffer Sharpness:register(b0){float amount;};
struct Vertex {float4 position:SV_Position;float2 uv:TEXCOORD0;};
float4 PS(Vertex input):SV_Target {
    uint width,height;
    image.GetDimensions(width,height);
    float2 texel=1.0/float2(width,height);
    float4 center=image.Sample(linearClamp,input.uv);
    float3 left=image.Sample(linearClamp,input.uv-float2(texel.x,0)).rgb;
    float3 right=image.Sample(linearClamp,input.uv+float2(texel.x,0)).rgb;
    float3 up=image.Sample(linearClamp,input.uv-float2(0,texel.y)).rgb;
    float3 down=image.Sample(linearClamp,input.uv+float2(0,texel.y)).rgb;
    float3 low=min(center.rgb,min(min(left,right),min(up,down)));
    float3 high=max(center.rgb,max(max(left,right),max(up,down)));
    float3 detail=center.rgb-.25*(left+right+up+down);
    return float4(clamp(center.rgb+amount*detail,low,high),center.a);
})";
            Ptr<ID3DBlob> code;
            HRESULT hr=D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,"PS","ps_5_0",0,0,&code,nullptr);
            if(FAILED(hr))return hr;
            hr=device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&eyeSharpenPixel);
            if(FAILED(hr))return hr;
        }
        const float constants[4]{amount,0,0,0};
        const HRESULT hr=SetPixelConstants(constants,sizeof(constants));
        if(FAILED(hr))return hr;
        pixel=eyeSharpenPixel;
        eyeSharpness=amount;
        return S_OK;
    }
    HRESULT Copy(ID3D11Texture2D* backbuffer, DXGI_FORMAT typedFormat, ID3D11RenderTargetView* output, UINT outputWidth, UINT outputHeight,float clearAlpha=1.f,ID3D11ShaderResourceView* overlay=nullptr) {
        if (!context || !output) return E_INVALIDARG;
        if(overlay && !overlayBlend) {
            D3D11_BLEND_DESC blend{};
            auto& target=blend.RenderTarget[0];
            target.BlendEnable=TRUE;
            target.SrcBlend=target.SrcBlendAlpha=D3D11_BLEND_ONE;
            target.DestBlend=target.DestBlendAlpha=D3D11_BLEND_INV_SRC_ALPHA;
            target.BlendOp=target.BlendOpAlpha=D3D11_BLEND_OP_ADD;
            target.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
            const HRESULT hr=device->CreateBlendState(&blend,&overlayBlend);
            if(FAILED(hr))return hr;
        }
        D3D11_TEXTURE2D_DESC desc{}; backbuffer->GetDesc(&desc);
        if (!copy || width != desc.Width || height != desc.Height || format != typedFormat) {
            source.Reset(); copy.Reset();
            desc.Format = typedFormat; desc.SampleDesc = {1,0}; desc.MipLevels = desc.ArraySize = 1;
            desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.CPUAccessFlags = desc.MiscFlags = 0;
            HRESULT hr = device->CreateTexture2D(&desc, nullptr, &copy); if (FAILED(hr)) return hr;
            hr = device->CreateShaderResourceView(copy.Get(), nullptr, &source); if (FAILED(hr)) return hr;
            width = desc.Width; height = desc.Height; format = typedFormat;
        }
        int gpuSlot = gpuTimer.Begin(context.Get());
        Ptr<ID3DDeviceContextState> previous;
        context->SwapDeviceContextState(state.Get(), &previous);
        D3D11_TEXTURE2D_DESC inputDesc{}; backbuffer->GetDesc(&inputDesc);
        if (inputDesc.SampleDesc.Count > 1) context->ResolveSubresource(copy.Get(), 0, backbuffer, 0, typedFormat);
        else context->CopyResource(copy.Get(), backbuffer);
        const float clear[4] = {0,0,0,clearAlpha}; context->ClearRenderTargetView(output, clear);
        context->OMSetRenderTargets(1, &output, nullptr);
        context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        context->OMSetDepthStencilState(nullptr, 0);
        context->RSSetState(nullptr);
        D3D11_VIEWPORT viewport{0,0,static_cast<float>(outputWidth),static_cast<float>(outputHeight),0,1};
        context->RSSetViewports(1, &viewport);
        context->IASetInputLayout(nullptr); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertex.Get(), nullptr, 0); context->PSSetShader(pixel.Get(), nullptr, 0);
        context->GSSetShader(nullptr, nullptr, 0); context->HSSetShader(nullptr, nullptr, 0); context->DSSetShader(nullptr, nullptr, 0);
        ID3D11ShaderResourceView* srv = source.Get(); context->PSSetShaderResources(0, 1, &srv);
        ID3D11Buffer* constants=pixelConstants.Get();context->PSSetConstantBuffers(0,1,&constants);
        ID3D11SamplerState* sample = sampler.Get(); context->PSSetSamplers(0, 1, &sample);
        context->Draw(3, 0);
        if(overlay) {
            context->PSSetShader(identityPixel.Get(),nullptr,0);
            context->PSSetShaderResources(0,1,&overlay);
            context->OMSetBlendState(overlayBlend.Get(),nullptr,0xffffffff);
            context->Draw(3,0);
        }
        srv = nullptr; context->PSSetShaderResources(0, 1, &srv);
        context->OMSetRenderTargets(0, nullptr, nullptr);
        context->SwapDeviceContextState(previous.Get(), nullptr);
        gpuTimer.End(context.Get(), gpuSlot);
        return S_OK;
    }
};
}
