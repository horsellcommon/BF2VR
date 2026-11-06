#pragma once
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <unordered_map>
#include <string>
#include <cmath>
namespace BF2VR {
// Experimental native-UI placement pass. No CPU buffer/image readbacks.
class UiAnchorTransform {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    Ptr<ID3D11Device> device;Ptr<ID3D11DeviceContext1> context;
    Ptr<ID3DDeviceContextState> state;Ptr<ID3D11ComputeShader> shader;
    Ptr<ID3D11Buffer> parameters,output,constant;
    Ptr<ID3D11UnorderedAccessView> outputView;
    struct Cached { Ptr<ID3D11Buffer> source,copy;Ptr<ID3D11ShaderResourceView> view;UINT bytes=0;uint64_t frame=~0ull; };
    std::unordered_map<ID3D11Buffer*,Cached> cache;
    uint64_t frame=0;
    HRESULT Source(ID3D11Buffer* input,ID3D11ShaderResourceView** result,UINT& bytes) {
        if(!input)return E_INVALIDARG;
        auto& saved=cache[input];D3D11_BUFFER_DESC desc{};input->GetDesc(&desc);bytes=desc.ByteWidth;
        if(!desc.ByteWidth || desc.ByteWidth%4 || desc.ByteWidth>8*1024*1024)return E_INVALIDARG;
        if(!saved.copy || !saved.view || saved.bytes!=desc.ByteWidth) {
            saved={};saved.source=input;saved.bytes=desc.ByteWidth;
            D3D11_BUFFER_DESC copy{};copy.ByteWidth=desc.ByteWidth;copy.BindFlags=D3D11_BIND_SHADER_RESOURCE;copy.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
            HRESULT hr=device->CreateBuffer(&copy,nullptr,&saved.copy);if(FAILED(hr))return hr;
            D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_R32_TYPELESS;view.ViewDimension=D3D11_SRV_DIMENSION_BUFFEREX;
            view.BufferEx.NumElements=desc.ByteWidth/4;view.BufferEx.Flags=D3D11_BUFFEREX_SRV_FLAG_RAW;
            hr=device->CreateShaderResourceView(saved.copy.Get(),&view,&saved.view);if(FAILED(hr))return hr;
        }
        if(saved.frame!=frame) { context->CopyResource(saved.copy.Get(),input);saved.frame=frame; }
        *result=saved.view.Get();return S_OK;
    }
public:
    // The inspected world overlay stage retains the two scene constant ranges
    // and auxiliary vertex streams. The later map/status stage binds only slot0.
    static bool WorldLayer(UINT auxiliaryConstants,UINT sceneConstants,UINT auxiliaryStride,UINT transformStride) {
        return auxiliaryConstants==16 && sceneConstants==80 && auxiliaryStride==64 && transformStride==48;
    }
    static bool AimLayer(bool worldLayer,UINT constants,UINT stride) {
        return !worldLayer && constants==64 && (stride==40 || stride==56);
    }
    struct alignas(16) Parameters {
        UINT vertexOffset=0,stride=0,indexOffset=0;int baseVertex=0;
        UINT vertexBytes=0,indexBytes=0,indexBits=0,indexed=0;
        float scale=.25f,aimX=0,aimY=0,worldEnabled=1;
        float aimEnabled=1,worldReproject=0,splitLayers=0,pad2=0;
        float worldX[4]{1,0,0,0},worldY[4]{0,1,0,0},worldW[4]{0,0,1,0};
    };
    HRESULT Initialize(ID3D11Device* input,ID3D11DeviceContext* immediate,std::string& errors) {
        if(!input || !immediate || immediate->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return E_INVALIDARG;
        device=input;HRESULT hr=immediate->QueryInterface(IID_PPV_ARGS(&context));if(FAILED(hr))return hr;
        Ptr<ID3D11Device1> device1;hr=device.As(&device1);if(FAILED(hr))return hr;
        const auto level=device->GetFeatureLevel();D3D_FEATURE_LEVEL chosen{};
        hr=device1->CreateDeviceContextState(0,&level,1,D3D11_SDK_VERSION,__uuidof(ID3D11Device),&chosen,&state);if(FAILED(hr))return hr;
        const char* code=R"(
cbuffer NativeUI:register(b0){float4 ui[64];};
cbuffer Placement:register(b1){uint vertexOffset,stride,indexOffset;int baseVertex;uint vertexBytes,indexBytes,indexBits,indexed;float scale,aimX,aimY,worldEnabled;float aimEnabled,worldReproject,splitLayers,pad2;float4 worldX,worldY,worldW;};
ByteAddressBuffer vertices:register(t0);ByteAddressBuffer indices:register(t1);
RWStructuredBuffer<float4> changed:register(u0);
[numthreads(64,1,1)] void CS(uint3 tid:SV_DispatchThreadID) {
    float4 value=ui[tid.x];float2 shift=0;int vertex=baseVertex;bool anchored=false;
    bool valid=stride>=12 && vertexOffset<vertexBytes && isfinite(scale) && scale>=.25 && scale<=1;
    if(indexed!=0) {
        if((indexOffset&~3u)+4>indexBytes)valid=false;
        else { uint word=indices.Load(indexOffset&~3u);uint index=indexBits==16?((word>>((indexOffset&2u)*8))&65535u):word;vertex+=int(index); }
    }
    if(vertex<0 || uint(vertex)>vertexBytes/ max(stride,1u))valid=false;
    uint location=vertexOffset+uint(max(vertex,0))*stride;
    if(location+12>vertexBytes)valid=false;
    if(valid) {
        float3 position=asfloat(vertices.Load3(location));
        float4 x=ui[1],y=ui[2],w=ui[4];
        bool plane=all(isfinite(position)) && all(isfinite(x)) && all(isfinite(y)) && all(isfinite(w)) && abs(w.w)>1 && abs(w.x)+abs(w.y)<1e-5;
        bool diagonal=abs(x.y)+abs(y.x)<1e-5 && x.x>0 && y.y<0;
        // Local geometry belongs to markers only in the observed world overlay stage.
        // Preserve the native rotation of arrow geometry when moving its root.
        bool marker=plane && all(abs(position.xy)<192) && worldEnabled!=0;
        float2 center=float2(w.w/max(x.x,1e-6),-w.w/min(y.y,-1e-6));
        bool aim=plane && diagonal && abs(x.w/w.w+1)<.02 && y.w/w.w>.85 && y.w/w.w<1.05 && all(abs(position.xy-center)<float2(140,100)) && aimEnabled!=0;
        anchored=marker || aim;
        if(marker) {
            float2 anchor=float2(x.w,y.w)/w.w;
            if(worldReproject!=0) {
                float3 ray=float3(anchor,1);float3 mapped=float3(dot(worldX.xyz,ray),dot(worldY.xyz,ray),dot(worldW.xyz,ray));
                anchor=mapped.z>1e-5?mapped.xy/mapped.z:float2(8,8);
            }
            shift=anchor-scale*float2(x.w,y.w)/w.w;
            // In the captured objective and unit icon layouts, the visible
            // center is displaced from the draw origin. Keep that center at
            // its native offset while shrinking the graphic around it.
            float2 pivot=0;
            if(diagonal) {
                if(stride==40 && all(abs(position.xy-float2(57,18))<.02))
                    pivot=float2(75,36);
                else if((stride==56 && all(abs(position.xy-float2(56,45.5))<.05)) ||
                        (stride==40 && abs(position.y-20.5)<.02 &&
                         any(abs(position.x-float3(23.5,25.5,26.5))<.02)))
                    pivot=float2(32,32);
            }
            shift+=(1-scale)*float2(dot(x.xy,pivot),dot(y.xy,pivot))/w.w;
        }
        else if(aim)shift=float2(aimX,aimY);
    }
    // Rasterize at the final HUD size before clipping to the source texture.
    if(tid.x==1)value=scale*value+shift.x*ui[4];
    if(tid.x==2)value=scale*value+shift.y*ui[4];
    if(splitLayers!=0) {
        float4 nativeValue=ui[tid.x];
        if(tid.x==1) {
            if(anchored)nativeValue=8*ui[4];
            else value=8*ui[4];
        }
        changed[tid.x+64]=nativeValue;
    }
    changed[tid.x]=value;
})";
        Ptr<ID3DBlob> blob,error;hr=D3DCompile(code,strlen(code),nullptr,nullptr,nullptr,"CS","cs_5_0",0,0,&blob,&error);
        if(error)errors.assign(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize());if(FAILED(hr))return hr;
        hr=device->CreateComputeShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&shader);if(FAILED(hr))return hr;
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=2048;desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;desc.StructureByteStride=16;
        hr=device->CreateBuffer(&desc,nullptr,&output);if(FAILED(hr))return hr;
        hr=device->CreateUnorderedAccessView(output.Get(),nullptr,&outputView);if(FAILED(hr))return hr;
        desc={};desc.ByteWidth=2048;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;hr=device->CreateBuffer(&desc,nullptr,&constant);if(FAILED(hr))return hr;
        desc={};desc.ByteWidth=sizeof(Parameters);desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;desc.Usage=D3D11_USAGE_DYNAMIC;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        return device->CreateBuffer(&desc,nullptr,&parameters);
    }
    void NextFrame() { ++frame;if(cache.size()>32)cache.clear(); }
    ID3D11Buffer* Constant() const {return constant.Get();}
    HRESULT Prepare(ID3D11Buffer* source,UINT first,UINT count,ID3D11Buffer* vertices,ID3D11Buffer* indices,Parameters p) {
        if(!source || !vertices || !context || count<5 || count>64 || !std::isfinite(p.scale) || p.scale<.25f || p.scale>1.f || !std::isfinite(p.aimX) || !std::isfinite(p.aimY))return E_INVALIDARG;
        D3D11_BUFFER_DESC native{};source->GetDesc(&native);if((uint64_t(first)+count)*16>native.ByteWidth)return E_INVALIDARG;
        Ptr<ID3DDeviceContextState> previous;context->SwapDeviceContextState(state.Get(),&previous);
        struct Restore { ID3D11DeviceContext1* context;ID3DDeviceContextState* state;~Restore(){context->SwapDeviceContextState(state,nullptr);} } restore{context.Get(),previous.Get()};
        ID3D11ShaderResourceView* inputs[2]{};HRESULT hr=Source(vertices,&inputs[0],p.vertexBytes);if(FAILED(hr))return hr;
        if(p.indexed) { hr=Source(indices,&inputs[1],p.indexBytes);if(FAILED(hr))return hr; }
        D3D11_MAPPED_SUBRESOURCE mapped{};hr=context->Map(parameters.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped);if(FAILED(hr))return hr;
        memcpy(mapped.pData,&p,sizeof(p));context->Unmap(parameters.Get(),0);
        context->CSSetShader(shader.Get(),nullptr,0);context->CSSetConstantBuffers1(0,1,&source,&first,&count);
        auto* param=parameters.Get();context->CSSetConstantBuffers(1,1,&param);context->CSSetShaderResources(0,2,inputs);
        auto* target=outputView.Get();context->CSSetUnorderedAccessViews(0,1,&target,nullptr);context->Dispatch(1,1,1);
        ID3D11UnorderedAccessView* none=nullptr;context->CSSetUnorderedAccessViews(0,1,&none,nullptr);
        context->CopyResource(constant.Get(),output.Get());
        ID3D11ShaderResourceView* empty[2]{};context->CSSetShaderResources(0,2,empty);ID3D11Buffer* noBuffer=nullptr;context->CSSetConstantBuffers(0,1,&noBuffer);
        return S_OK;
    }
};
}
