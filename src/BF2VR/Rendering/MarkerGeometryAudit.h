#pragma once
#include <d3d11_1.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <array>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <sstream>
namespace BF2VR {
// Desktop-only, one complete frame. Copies bounded UI buffers; never redirects draws.
class MarkerGeometryAudit {
    using Buffer=Microsoft::WRL::ComPtr<ID3D11Buffer>;
    static inline std::mutex mutex;
    static inline std::atomic_bool active=false;
    static inline bool pending=false,waiting=false;
    static inline unsigned capturedFrames=0,waitingFrames=0;
    static inline ID3D11Device* observedDevice=nullptr;
    static inline std::filesystem::path folder;
    static inline ID3D11DeviceContext* observedContext=nullptr;
    struct Snapshot { Buffer copy;Microsoft::WRL::ComPtr<ID3D11Query> ready;D3D11_BUFFER_DESC source{};uintptr_t address=0; };
    static inline std::vector<Snapshot> buffers;
    struct ContextCount { unsigned type=0,draws=0,matches=0;bool sameDevice=false; };
    static inline std::unordered_map<ID3D11DeviceContext*,ContextCount> contexts;
    static inline std::vector<std::string> rows;
    static inline size_t bytes=0;
    static inline bool truncated=false;
    static int Keep(ID3D11DeviceContext* context,ID3D11Buffer* buffer) {
        if(!buffer)return -1;
        for(size_t i=0;i<buffers.size();++i)if(buffers[i].address==reinterpret_cast<uintptr_t>(buffer))return static_cast<int>(i);
        D3D11_BUFFER_DESC desc{};buffer->GetDesc(&desc);
        if(buffers.size()>=64 || desc.ByteWidth>8*1024*1024 || bytes+desc.ByteWidth>16*1024*1024) { truncated=true;return -2; }
        Snapshot snapshot;snapshot.source=desc;snapshot.address=reinterpret_cast<uintptr_t>(buffer);
        D3D11_BUFFER_DESC copy{};copy.ByteWidth=desc.ByteWidth;copy.Usage=D3D11_USAGE_STAGING;copy.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        if(FAILED(observedDevice->CreateBuffer(&copy,nullptr,&snapshot.copy)))return -3;
        D3D11_QUERY_DESC query{};query.Query=D3D11_QUERY_EVENT;
        if(FAILED(observedDevice->CreateQuery(&query,&snapshot.ready)))return -3;
        // Place copies on the very context recording this draw, before the source can be reused.
        context->CopyResource(snapshot.copy.Get(),buffer);context->End(snapshot.ready.Get());
        bytes+=desc.ByteWidth;buffers.push_back(std::move(snapshot));return static_cast<int>(buffers.size()-1);
    }
    static HRESULT ReadSnapshot(ID3D11DeviceContext* context,ID3D11Buffer* copy,std::vector<unsigned char>& output) {
        D3D11_BUFFER_DESC desc{};copy->GetDesc(&desc);D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT hr=context->Map(copy,0,D3D11_MAP_READ,0,&mapped);if(FAILED(hr))return hr;
        try { output.assign(static_cast<const unsigned char*>(mapped.pData),static_cast<const unsigned char*>(mapped.pData)+desc.ByteWidth); }
        catch(...) { context->Unmap(copy,0);throw; }
        context->Unmap(copy,0);return S_OK;
    }
public:
    static HRESULT ReadBuffer(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Buffer* source,std::vector<unsigned char>& output) {
        output.clear();if(!device || !context || !source)return E_INVALIDARG;
        D3D11_BUFFER_DESC original{};source->GetDesc(&original);
        if(!original.ByteWidth || original.ByteWidth>8*1024*1024)return E_INVALIDARG;
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=original.ByteWidth;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Buffer staging;HRESULT hr=device->CreateBuffer(&desc,nullptr,&staging);if(FAILED(hr))return hr;
        context->CopyResource(staging.Get(),source);
        D3D11_MAPPED_SUBRESOURCE mapped{};hr=context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped);
        if(FAILED(hr))return hr;
        try { output.assign(static_cast<const unsigned char*>(mapped.pData),static_cast<const unsigned char*>(mapped.pData)+desc.ByteWidth); }
        catch(...) { context->Unmap(staging.Get(),0);throw; }
        context->Unmap(staging.Get(),0);return S_OK;
    }
    static void Request(const std::filesystem::path& destination) {
        std::lock_guard lock(mutex);if(active || pending || waiting)return;folder=destination;pending=true;
    }
    static void Present(ID3D11Device* device,ID3D11DeviceContext* context) {
        std::lock_guard lock(mutex);
        if(active && (++capturedFrames>=3 || (!rows.empty() && capturedFrames>=1))) { active=false;waiting=true;waitingFrames=0; }
        bool completed=waiting;
        if(waiting)for(const auto& saved:buffers) {
            const HRESULT status=context->GetData(saved.ready.Get(),nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if(status!=S_OK) { completed=false;break; }
        }
        const bool timedOut=waiting && ++waitingFrames>=120;
        if(waiting && (completed || timedOut)) {
            waiting=false;
            try {
                std::filesystem::create_directories(folder);
                std::ofstream draws(folder/"draws.csv");
                draws<<"order,kind,count,instances,start,base,startInstance,ps,vs,ib,ibFormat,ibOffset,vb0,stride0,offset0,vb1,stride1,offset1,vb2,stride2,offset2,vb3,stride3,offset3,vsCBs,psTextures,stencil,stencilRef,stencilReadMask,stencilWriteMask,frontFunc,backFunc,scissorEnabled,sx0,sy0,sx1,sy1\n";
                for(size_t i=0;i<rows.size();++i)draws<<i<<','<<rows[i]<<'\n';
                std::ofstream index(folder/"buffers.csv");index<<"id,bytes,usage,bind,stride,result\n";
                for(size_t i=0;i<buffers.size();++i) {
                    const auto desc=buffers[i].source;std::vector<unsigned char> data;
                    const HRESULT hr=completed ? ReadSnapshot(context,buffers[i].copy.Get(),data) : E_PENDING;
                    index<<i<<','<<desc.ByteWidth<<','<<desc.Usage<<','<<desc.BindFlags<<','<<desc.StructureByteStride<<','<<hr<<'\n';
                    if(SUCCEEDED(hr)) { std::ofstream binary(folder/("buffer-"+std::to_string(i)+".bin"),std::ios::binary);binary.write(reinterpret_cast<const char*>(data.data()),data.size()); }
                }
                std::ofstream counts(folder/"contexts.csv");counts<<"context,type,draws,matches,sameDevice\n";
                for(const auto& [pointer,entry]:contexts)counts<<reinterpret_cast<uintptr_t>(pointer)<<','<<entry.type<<','<<entry.draws<<','<<entry.matches<<','<<entry.sameDevice<<'\n';
                std::ofstream note(folder/"capture.txt");note<<"Desktop-only native UI geometry diagnostic. No draws or shader bindings changed. Raw game data: private recon only, do not distribute. Truncated="<<truncated<<"; bytes="<<bytes<<"; draws="<<rows.size()<<"; gpuReady="<<completed<<"; first-use resource snapshots"<<"\n";
            } catch(...) { /* A diagnostic failure must not interrupt the native Present. */ }
            buffers.clear();rows.clear();observedContext=nullptr;
        }
        if(pending) { pending=false;buffers.clear();rows.clear();contexts.clear();bytes=0;truncated=false;capturedFrames=0;observedContext=context;observedDevice=device;active=true; }
    }
    static void Cancel() { std::lock_guard lock(mutex);active=false;pending=waiting=false;buffers.clear();rows.clear();contexts.clear(); }
    static void Draw(ID3D11DeviceContext* context,unsigned kind,unsigned count,unsigned instances,unsigned start,int base,unsigned startInstance) {
        if(!active)return;
        std::lock_guard lock(mutex);if(!active || rows.size()>=2048) { truncated=true;return; }
        auto [entry,inserted]=contexts.try_emplace(context);
        if(inserted) { Microsoft::WRL::ComPtr<ID3D11Device> device;context->GetDevice(&device);entry->second.type=context->GetType();entry->second.sameDevice=device.Get()==observedDevice; }
        ++entry->second.draws;if(!entry->second.sameDevice)return;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;context->OMGetRenderTargets(1,&target,nullptr);if(!target)return;
        D3D11_RENDER_TARGET_VIEW_DESC view{};target->GetDesc(&view);if(view.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)return;
        Microsoft::WRL::ComPtr<ID3D11Resource> resource;target->GetResource(&resource);Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        if(FAILED(resource.As(&texture)))return;D3D11_TEXTURE2D_DESC screen{};texture->GetDesc(&screen);if(screen.Width<640 || screen.Height<480)return;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depth;UINT stencil=0;context->OMGetDepthStencilState(&depth,&stencil);
        D3D11_DEPTH_STENCIL_DESC dd{};if(!depth)return;depth->GetDesc(&dd);if(dd.DepthEnable)return;
        Microsoft::WRL::ComPtr<ID3D11BlendState> blend;FLOAT factors[4]{};UINT mask=0;context->OMGetBlendState(&blend,factors,&mask);
        D3D11_BLEND_DESC bd{};if(!blend)return;blend->GetDesc(&bd);if(!bd.RenderTarget[0].BlendEnable)return;
        ++entry->second.matches;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> ps;Microsoft::WRL::ComPtr<ID3D11VertexShader> vs;
        context->PSGetShader(&ps,nullptr,nullptr);context->VSGetShader(&vs,nullptr,nullptr);
        Buffer ib;DXGI_FORMAT format{};UINT ibOffset=0;context->IAGetIndexBuffer(&ib,&format,&ibOffset);
        std::ostringstream row;row<<kind<<','<<count<<','<<instances<<','<<start<<','<<base<<','<<startInstance<<','<<reinterpret_cast<uintptr_t>(ps.Get())<<','<<reinterpret_cast<uintptr_t>(vs.Get())<<','<<Keep(context,ib.Get())<<','<<format<<','<<ibOffset;
        ID3D11Buffer* vertices[4]{};UINT strides[4]{},offsets[4]{};context->IAGetVertexBuffers(0,4,vertices,strides,offsets);
        for(unsigned i=0;i<4;++i) { row<<','<<Keep(context,vertices[i])<<','<<strides[i]<<','<<offsets[i];if(vertices[i])vertices[i]->Release(); }
        ID3D11Buffer* constants[14]{};UINT first[14]{},length[14]{};
        Microsoft::WRL::ComPtr<ID3D11DeviceContext1> context1;
        if(SUCCEEDED(context->QueryInterface(IID_PPV_ARGS(&context1))))context1->VSGetConstantBuffers1(0,14,constants,first,length);
        else context->VSGetConstantBuffers(0,14,constants);
        row<<",\"";for(unsigned i=0;i<14;++i)if(constants[i]) { row<<i<<':'<<Keep(context,constants[i])<<':'<<first[i]<<':'<<length[i]<<' ';constants[i]->Release(); }row<<'"';
        ID3D11ShaderResourceView* resources[8]{};context->PSGetShaderResources(0,8,resources);row<<",\"";
        for(unsigned i=0;i<8;++i)if(resources[i]) { Microsoft::WRL::ComPtr<ID3D11Resource> source;resources[i]->GetResource(&source);Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
            if(SUCCEEDED(source.As(&tex))) { D3D11_TEXTURE2D_DESC td{};tex->GetDesc(&td);row<<i<<':'<<reinterpret_cast<uintptr_t>(source.Get())<<':'<<td.Width<<'x'<<td.Height<<':'<<td.Format<<' '; }resources[i]->Release(); }row<<'"';
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> raster;context->RSGetState(&raster);D3D11_RASTERIZER_DESC rd{};if(raster)raster->GetDesc(&rd);
        D3D11_RECT scissor{};UINT scissorCount=1;context->RSGetScissorRects(&scissorCount,&scissor);
        row<<','<<dd.StencilEnable<<','<<stencil<<','<<static_cast<unsigned>(dd.StencilReadMask)<<','<<static_cast<unsigned>(dd.StencilWriteMask)<<','<<dd.FrontFace.StencilFunc<<','<<dd.BackFace.StencilFunc<<','<<rd.ScissorEnable<<','<<scissor.left<<','<<scissor.top<<','<<scissor.right<<','<<scissor.bottom;
        rows.push_back(row.str());
    }
};
}
