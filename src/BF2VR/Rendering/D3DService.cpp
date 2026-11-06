// Copyright Ethan Porcaro

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.

#include <D3DService.h>
#include <Logging.h>
#include <OpenXRService.h>
#include <Runtime.h>
#include <BF2Service.h>
#include <Classes.h>
#include <wrl/client.h>
#include <format>
#include <DxgiProbe.h>
#include <Timing.h>
#include <EyeReadback.h>
#include <fstream>
#include <DrawAudit.h>
#include <HudTransform.h>
#include <algorithm>
#include <HudMenuGuard.h>
#include <MenuInput.h>
#include <MenuShader.h>
#include <HudStartup.h>
#include <MarkerCameraAudit.h>
#include <MarkerGeometryAudit.h>
#include <UiAnchorTransform.h>
namespace BF2VR {
namespace {
SafetyHookInline deferredDrawHook,deferredIndexedHook,deferredIndexedInstancedHook,deferredInstancedHook,deferredResourcesHook;
void RecordDraw(ID3D11DeviceContext*,unsigned,unsigned,unsigned);
void TrackHudResources(ID3D11DeviceContext*,UINT,UINT,ID3D11ShaderResourceView* const*);
std::filesystem::path captureDirectory;
bool capturePending=false;
std::atomic_bool auditActive=false;
bool auditPending=false;
bool startupSelection=false;
bool autoHudDiscovery=false;
unsigned startupFrames=0;
HudStartupSelection startupConfirmation;
unsigned auditMethods=0;
std::filesystem::path auditFile;
std::vector<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>> selectableSurfaces;
Microsoft::WRL::ComPtr<ID3D11Resource> selectedHudResource;
Microsoft::WRL::ComPtr<ID3D11RenderTargetView> selectedHudTarget;
std::atomic<ID3D11RenderTargetView*> selectedHudTargetKey{nullptr};
Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> selectedHudView;
std::unique_ptr<HudTransform> hudTransform,menuTransform;
float compiledMenuScale=0;
std::string pendingHudShader;
std::atomic_bool hudEnabled=false;
bool hudHookReady=false;
thread_local bool insideHudTransform=false;
std::array<bool,D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT> hudSlots{};
std::atomic_bool hudMayBeBound=false;
std::filesystem::path hudPreviewFile;
struct HudProcessingGuard { HudProcessingGuard(){insideHudTransform=true;}~HudProcessingGuard(){insideHudTransform=false;} };
void SaveHudPreview() {
    if(hudPreviewFile.empty() || !Runtime::desktopAuditOnly)return;
    auto file=hudPreviewFile;hudPreviewFile.clear();EyePixels pixels;
    const HRESULT hr=ReadEyePixels(D3DService::pDevice,D3DService::pContext,hudTransform->Target(),pixels);
    if(FAILED(hr)) { Logging::Log("[HUD_TRANSFORM] Preview readback failed",static_cast<int>(hr));return; }
    auto rgbFile=file;rgbFile.replace_extension(".ppm");auto alphaFile=file;alphaFile.replace_extension(".pgm");
    std::ofstream rgb(rgbFile,std::ios::binary),alpha(alphaFile,std::ios::binary);
    rgb << "P6\n" << pixels.width << " " << pixels.height << "\n255\n";
    rgb.write(reinterpret_cast<const char*>(pixels.rgb.data()),static_cast<std::streamsize>(pixels.rgb.size()));
    alpha << "P5\n" << pixels.width << " " << pixels.height << "\n255\n";
    alpha.write(reinterpret_cast<const char*>(pixels.alpha.data()),static_cast<std::streamsize>(pixels.alpha.size()));
    if(!rgb || !alpha)throw std::runtime_error("HUD preview write failed");
    Logging::Log(std::format("[HUD_TRANSFORM] Preview saved {}; nonzero alpha pixels={}",rgbFile.string(),std::count_if(pixels.alpha.begin(),pixels.alpha.end(),[](auto x){return x!=0;})));
}
HudMenuGuard hudMenuGuard;
uint64_t uiPlacementFrame=0,uiCursorFrame=~0ull;
uint64_t lastHudProducer=0,lastHudBinding=0,nextHudDiscovery=0;
UINT knownHudSlot=D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
bool uiCursorAllowed=false;

Microsoft::WRL::ComPtr<ID3D11RenderTargetView> hudOverlayTarget;
Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> hudOverlayView;
uint64_t hudOverlayFrame=~0ull;

HRESULT PrepareHudOverlay(ID3D11DeviceContext* context,ID3D11RenderTargetView* native) {
    if(!hudOverlayTarget || !hudOverlayView) {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture,overlay;
        const HRESULT query=selectedHudResource.As(&texture);
        if(FAILED(query))return query;
        D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
        D3D11_RENDER_TARGET_VIEW_DESC targetDesc{};native->GetDesc(&targetDesc);
        if(desc.SampleDesc.Count!=1 || desc.ArraySize!=1 || desc.MipLevels!=1 ||
            targetDesc.ViewDimension!=D3D11_RTV_DIMENSION_TEXTURE2D)return E_NOTIMPL;
        desc.Format=targetDesc.Format;
        desc.Usage=D3D11_USAGE_DEFAULT;desc.CPUAccessFlags=desc.MiscFlags=0;
        desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        auto* device=D3DService::pDevice;
        HRESULT hr=device->CreateTexture2D(&desc,nullptr,&overlay);if(FAILED(hr))return hr;
        hr=device->CreateRenderTargetView(overlay.Get(),nullptr,&hudOverlayTarget);if(FAILED(hr))return hr;
        hr=device->CreateShaderResourceView(overlay.Get(),nullptr,&hudOverlayView);if(FAILED(hr))return hr;
    }
    if(hudOverlayFrame!=uiPlacementFrame) {
        const float transparent[4]{};
        context->ClearRenderTargetView(hudOverlayTarget.Get(),transparent);
        hudOverlayFrame=uiPlacementFrame;
    }
    return S_OK;
}

bool AllowGameplayHudTransform() {
    const bool focused=GetForegroundWindow()==D3DService::gameWindow;
    CURSORINFO cursor{};cursor.cbSize=sizeof(cursor);
    const bool valid=focused && GetCursorInfo(&cursor);
    const bool allowed=hudMenuGuard.Observe(focused,valid,(cursor.flags&CURSOR_SHOWING)!=0);
    static bool previous=false;
    if(allowed!=previous) {
        previous=allowed;Logging::Log(allowed ? "[HUD_MENU] Gameplay cursor hidden: HUD transform allowed" : "[HUD_MENU] Visible cursor or query failure: native interactive UI retained");
    }
    return allowed;
}
template<class Draw> void ExecuteHudDraw(ID3D11DeviceContext* context,Draw&& draw) {
    if(insideHudTransform || context!=D3DService::pContext || !Runtime::enabled || !hudEnabled || !hudMayBeBound) { draw();return; }
    std::lock_guard lock(Runtime::mutex);
    const bool overlayReady=hudOverlayFrame==uiPlacementFrame && hudOverlayView;
    const bool menuMode=!overlayReady && !AllowGameplayHudTransform();
    if(menuMode && !MenuInput::CanScaleMenu()) { MenuInput::SetVisualActive(false);draw();return; }
    if(!menuMode)MenuInput::SetVisualActive(false);
    bool issued=false;
    try {
        HudProcessingGuard guard;
        if(!selectedHudResource) { issued=true;draw();return; }
        if(!hudTransform) {
            auto made=std::make_unique<HudTransform>();
            if(FAILED(made->Initialize(D3DService::pDevice,context))) { issued=true;draw();return; }
            hudTransform=std::move(made);
        }
        if(!pendingHudShader.empty()) {
            std::string error;const HRESULT hr=hudTransform->SetShader(pendingHudShader,error);pendingHudShader.clear();
            Logging::Log(SUCCEEDED(hr)?"[HUD_TRANSFORM] Shader reloaded":"[HUD_TRANSFORM] Shader rejected; previous retained",static_cast<int>(hr));
            if(FAILED(hr))Logging::Log(error.substr(0,1024));
        }
        HudTransform* processor=hudTransform.get();
        if(menuMode) {
            if(!menuTransform) {
                auto made=std::make_unique<HudTransform>();
                if(FAILED(made->Initialize(D3DService::pDevice,context))) { MenuInput::SetVisualActive(false);issued=true;draw();return; }
                menuTransform=std::move(made);
            }
            const float factor=MenuInput::Scale();
            if(compiledMenuScale!=factor) {
                const std::string code=BuildMenuShader(factor);
                std::string errors;
                if(FAILED(menuTransform->SetShader(code,errors))) { MenuInput::SetVisualActive(false);Logging::Log("[MENU_INPUT] Menu shader failed; native menu retained");issued=true;draw();return; }
                compiledMenuScale=factor;
            }
            float cursor[4]={-1,-1,0,0};
            if(!Runtime::desktopAuditOnly)MenuInput::PhysicalCursor(cursor);
            if(FAILED(menuTransform->SetCursor(cursor))) { MenuInput::SetVisualActive(false);issued=true;draw();return; }
            processor=menuTransform.get();
        }
        for(UINT slot=0;slot<hudSlots.size();++slot)if(hudSlots[slot]) {
            HudDrawBinding binding;const HRESULT hr=binding.Prepare(context,*processor,selectedHudResource.Get(),slot,overlayReady?hudOverlayView.Get():nullptr);
            if(hr==S_FALSE)continue;
            if(FAILED(hr)) { hudEnabled=false;MenuInput::SetVisualActive(false);Logging::Log("[HUD_TRANSFORM] Draw preparation failed; native HUD retained",static_cast<int>(hr));break; }
            if(!menuMode)SaveHudPreview();
            static auto last=Timing::Clock::time_point{};static unsigned draws=0;++draws;
            if(Timing::Clock::now()-last>=std::chrono::seconds(5)) { last=Timing::Clock::now();Logging::Log(std::format("[HUD_TRANSFORM] Consumer draws processed={} since previous report",draws));draws=0; }
            issued=true;draw();MenuInput::SetVisualActive(menuMode);return;
        }
        issued=true;draw();
    } catch(const std::exception& error) {
        hudEnabled=false;MenuInput::SetVisualActive(false);Logging::Log(std::string("[HUD_TRANSFORM] Draw processing stopped; native retained: ")+error.what());
        if(!issued)draw();
    }
}

std::unordered_map<ID3D11DeviceContext*,std::unique_ptr<UiAnchorTransform>> uiAnchors;
bool uiAnchorFailed=false;
Vec2 uiFrameAim{};bool uiFrameAimValid=false,uiFrameReproject=false;float uiFrameMapping[12]{};
template<class Draw> void ExecuteAnchoredUiDraw(ID3D11DeviceContext* context,unsigned kind,unsigned start,int base,Draw&& draw) {
    if(!Runtime::enabled || !hudEnabled || context!=D3DService::pContext) {draw();return;}
    auto* key=selectedHudTargetKey.load();if(!key) {draw();return;}
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;context->OMGetRenderTargets(1,&target,nullptr);
    if(target.Get()!=key) {draw();return;}
    std::lock_guard lock(Runtime::mutex);
    if(!selectedHudResource || selectedHudTargetKey.load()!=key) {draw();return;}
    lastHudProducer=uiPlacementFrame;
    if(uiCursorFrame!=uiPlacementFrame) {
        uiCursorFrame=uiPlacementFrame;uiCursorAllowed=Runtime::hudAnchorsEnabled && AllowGameplayHudTransform();
        uiFrameAim={};uiFrameAimValid=uiFrameReproject=false;
    }
    if(!uiCursorAllowed) {draw();return;}
    if(uiAnchorFailed) {draw();return;}
    Microsoft::WRL::ComPtr<ID3D11DeviceContext1> context1;if(FAILED(context->QueryInterface(IID_PPV_ARGS(&context1)))) {draw();return;}
    ID3D11Buffer* rawConstants[3]{};UINT constantOffsets[3]{},constantCounts[3]{};
    context1->VSGetConstantBuffers1(0,3,rawConstants,constantOffsets,constantCounts);
    Microsoft::WRL::ComPtr<ID3D11Buffer> original,auxiliary,scene;
    original.Attach(rawConstants[0]);auxiliary.Attach(rawConstants[1]);scene.Attach(rawConstants[2]);
    const UINT first=constantOffsets[0],count=constantCounts[0];
    ID3D11Buffer* rawVertices[3]{};UINT strides[3]{},offsets[3]{};
    context->IAGetVertexBuffers(0,3,rawVertices,strides,offsets);
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertex,auxVertex,transformVertex,index;
    vertex.Attach(rawVertices[0]);auxVertex.Attach(rawVertices[1]);transformVertex.Attach(rawVertices[2]);
    const UINT stride=strides[0],offset=offsets[0];
    const bool worldLayer=auxiliary && scene && auxVertex && transformVertex && UiAnchorTransform::WorldLayer(constantCounts[1],constantCounts[2],strides[1],strides[2]);
    const bool aimCandidate=UiAnchorTransform::AimLayer(worldLayer,count,stride);
    if((worldLayer || aimCandidate) && !uiFrameAimValid) {
        if(Runtime::desktopAuditOnly) {uiFrameAim={Runtime::hudTestAimX.load(),Runtime::hudTestAimY.load()};uiFrameAimValid=true;}
        else {
            uiFrameAimValid=BF2Service::HudAim(uiFrameAim);
            uiFrameReproject=Runtime::hudMarkerReprojection && BF2Service::HudReprojection(uiFrameMapping);
            static auto last=Timing::Clock::time_point{};
            const auto now=Timing::Clock::now();
            if(now-last>=std::chrono::seconds(5)) {
                last=now;
                Logging::Log(std::format("[HUD_ANCHOR] Aim valid={} NDC=({:.3f},{:.3f}) marker reprojection={}",
                    uiFrameAimValid,uiFrameAim.x,uiFrameAim.y,uiFrameReproject));
            }
        }
    }
    if(worldLayer && Runtime::hudMarkerReprojection && !uiFrameReproject && !Runtime::desktopAuditOnly) {draw();return;}
    const bool aimLayer=uiFrameAimValid && aimCandidate;
    if(!original || !vertex || (stride!=32 && stride!=40 && stride!=56) || count<5 || count>64 || (!worldLayer && !aimLayer)) {draw();return;}
    // Preserve the game's render pass for unsupported target layouts.
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depth;
    ID3D11RenderTargetView* targets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,targets,&depth);
    bool singleTarget=true;
    for(UINT slot=0;slot<D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;++slot) {
        if(!targets[slot])continue;
        if(slot!=0)singleTarget=false;
        targets[slot]->Release();
    }
    D3D11_DEPTH_STENCIL_DESC depthDesc{};
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthState;
    if(depth) {
        context->OMGetDepthStencilState(&depthState,nullptr);
        if(depthState)depthState->GetDesc(&depthDesc);
    }
    if(!singleTarget || !HudOverlayDepthIndependent(depth.Get()!=nullptr,depthState?&depthDesc:nullptr)) {
        static bool reported=false;
        if(!reported) {
            reported=true;
            Logging::Log(std::format("[HUD_ANCHOR] Native fallback for unsupported targets: single={} depth_view={} depth_enabled={} stencil_enabled={}",
                singleTarget,depth.Get()!=nullptr,depthState?depthDesc.DepthEnable!=FALSE:true,
                depthState?depthDesc.StencilEnable!=FALSE:false));
        }
        draw();return;
    }
    UINT indexOffset=0;DXGI_FORMAT indexFormat{};context->IAGetIndexBuffer(&index,&indexFormat,&indexOffset);
    auto& transform=uiAnchors[context];
    if(!transform) {
        auto made=std::make_unique<UiAnchorTransform>();std::string error;const HRESULT hr=made->Initialize(D3DService::pDevice,context,error);
        if(FAILED(hr)) {uiAnchorFailed=true;Logging::Log("[HUD_ANCHOR] GPU initialization failed; fixed HUD retained",static_cast<int>(hr));if(!error.empty())Logging::Log(error);draw();return;}
        transform=std::move(made);
    }
    UiAnchorTransform::Parameters p;p.vertexOffset=offset;p.stride=stride;p.baseVertex=base;p.scale=Runtime::hudScale;p.splitLayers=1;
    p.indexed=kind==1 || kind==2;
    if(p.indexed) {
        if(!index || (indexFormat!=DXGI_FORMAT_R16_UINT && indexFormat!=DXGI_FORMAT_R32_UINT)) {draw();return;}
        p.indexBits=indexFormat==DXGI_FORMAT_R16_UINT?16:32;p.indexOffset=indexOffset+start*(p.indexBits/8);
    } else p.baseVertex=static_cast<int>(start);
    p.worldEnabled=worldLayer?1.f:0.f;p.aimEnabled=aimLayer?1.f:0.f;
    p.aimX=uiFrameAim.x;p.aimY=uiFrameAim.y;
    if(worldLayer && uiFrameReproject) {
        p.worldReproject=1;memcpy(p.worldX,uiFrameMapping,16);memcpy(p.worldY,uiFrameMapping+4,16);memcpy(p.worldW,uiFrameMapping+8,16);
    }
    HRESULT hr;
    { HudProcessingGuard guard;hr=transform->Prepare(original.Get(),first,count,vertex.Get(),index.Get(),p); }
    if(FAILED(hr)) {uiAnchorFailed=true;Logging::Log("[HUD_ANCHOR] Placement preparation failed; fixed HUD retained",static_cast<int>(hr));draw();return;}
    hr=PrepareHudOverlay(context,target.Get());
    if(FAILED(hr)) {uiAnchorFailed=true;Logging::Log("[HUD_ANCHOR] Overlay target setup failed; native HUD retained",static_cast<int>(hr));draw();return;}
    static bool overlayLogged=false;
    if(!overlayLogged) {
        overlayLogged=true;
        Logging::Log(std::format("[HUD_ANCHOR] Split HUD draw active; inactive native depth view bound={}",depth.Get()!=nullptr));
    }
    auto* replacement=transform->Constant();
    UINT nativeOffset=64;context1->VSSetConstantBuffers1(0,1,&replacement,&nativeOffset,&count);
    struct Restore {ID3D11DeviceContext1* ctx;ID3D11Buffer* buffer;UINT first,count;~Restore(){ctx->VSSetConstantBuffers1(0,1,&buffer,&first,&count);}} restore{context1.Get(),original.Get(),first,count};
    draw();
    auto* overlay=hudOverlayTarget.Get();
    context->OMSetRenderTargets(1,&overlay,nullptr);
    UINT overlayOffset=0;context1->VSSetConstantBuffers1(0,1,&replacement,&overlayOffset,&count);
    draw();
    auto* native=target.Get();
    context->OMSetRenderTargets(1,&native,depth.Get());
}

thread_local bool insideNativeDraw=false;
template<class Draw> void DispatchUiDraw(ID3D11DeviceContext* context,unsigned kind,unsigned count,unsigned instances,unsigned start,int base,unsigned first,Draw&& original) {
    if(insideNativeDraw) {original();return;}
    struct Guard {Guard(){insideNativeDraw=true;}~Guard(){insideNativeDraw=false;}} guard;
    MarkerGeometryAudit::Draw(context,kind,count,instances,start,base,first);
    RecordDraw(context,kind,count,instances);
    ExecuteAnchoredUiDraw(context,kind,start,base,[&]{ExecuteHudDraw(context,original);});
}
void DeferredDraw(ID3D11DeviceContext* ctx,UINT count,UINT start) {
    DispatchUiDraw(ctx,0,count,1,start,0,0,[&]{deferredDrawHook.unsafe_call<void>(ctx,count,start);});
}
void DeferredIndexed(ID3D11DeviceContext* ctx,UINT count,UINT start,INT base) {
    DispatchUiDraw(ctx,1,count,1,start,base,0,[&]{deferredIndexedHook.unsafe_call<void>(ctx,count,start,base);});
}
void DeferredIndexedInstanced(ID3D11DeviceContext* ctx,UINT count,UINT instances,UINT start,INT base,UINT first) {
    DispatchUiDraw(ctx,2,count,instances,start,base,first,[&]{deferredIndexedInstancedHook.unsafe_call<void>(ctx,count,instances,start,base,first);});
}
void DeferredInstanced(ID3D11DeviceContext* ctx,UINT count,UINT instances,UINT start,UINT first) {
    DispatchUiDraw(ctx,3,count,instances,start,0,first,[&]{deferredInstancedHook.unsafe_call<void>(ctx,count,instances,start,first);});
}
void DeferredResources(ID3D11DeviceContext* ctx,UINT first,UINT count,ID3D11ShaderResourceView* const* views) {
    TrackHudResources(ctx,first,count,views);deferredResourcesHook.unsafe_call<void>(ctx,first,count,views);
}
std::vector<DrawSnapshot> auditRows;
std::vector<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>> auditSurfaces;
void SaveAuditSurfaces() {
    if(auditSurfaces.empty())return;
    FrameBlitter convert;
    if(FAILED(convert.Initialize(D3DService::pDevice,D3DService::pContext))) { Logging::Log("[HUD_AUDIT] Surface converter unavailable");return; }
    const auto folder=auditFile.parent_path()/auditFile.stem();
    std::filesystem::create_directories(folder);
    std::ofstream notes(folder/"surfaces.csv");notes << "index,rtv,format,width,height,result\n";
    for(size_t i=0;i<auditSurfaces.size();++i) {
        auto* target=auditSurfaces[i].Get();
        Microsoft::WRL::ComPtr<ID3D11Resource> resource;target->GetResource(&resource);
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        if(FAILED(resource.As(&texture)))continue;
        D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
        D3D11_RENDER_TARGET_VIEW_DESC view{};target->GetDesc(&view);
        // Convert diagnostic HDR/typeless surfaces to a readable RGBA8 preview.
        // This clips out-of-range colours; it is not a compositing reference.
        D3D11_TEXTURE2D_DESC outputDesc{};outputDesc.Width=desc.Width;outputDesc.Height=desc.Height;
        outputDesc.MipLevels=outputDesc.ArraySize=1;outputDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        outputDesc.SampleDesc.Count=1;outputDesc.BindFlags=D3D11_BIND_RENDER_TARGET;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> output;Microsoft::WRL::ComPtr<ID3D11RenderTargetView> outputTarget;
        HRESULT result=D3DService::pDevice->CreateTexture2D(&outputDesc,nullptr,&output);
        if(SUCCEEDED(result))result=D3DService::pDevice->CreateRenderTargetView(output.Get(),nullptr,&outputTarget);
        if(SUCCEEDED(result))result=convert.Copy(texture.Get(),view.Format,outputTarget.Get(),desc.Width,desc.Height);
        EyePixels pixels;
        if(SUCCEEDED(result))result=ReadEyePixels(D3DService::pDevice,D3DService::pContext,outputTarget.Get(),pixels);
        if(SUCCEEDED(result)) {
            std::ofstream file(folder/("surface-"+std::to_string(i)+".ppm"),std::ios::binary);
            file << "P6\n" << pixels.width << " " << pixels.height << "\n255\n";
            file.write(reinterpret_cast<const char*>(pixels.rgb.data()),static_cast<std::streamsize>(pixels.rgb.size()));
            if(!file)result=E_FAIL;
            std::ofstream alpha(folder/("surface-"+std::to_string(i)+"-alpha.pgm"),std::ios::binary);
            alpha << "P5\n" << pixels.width << " " << pixels.height << "\n255\n";
            alpha.write(reinterpret_cast<const char*>(pixels.alpha.data()),static_cast<std::streamsize>(pixels.alpha.size()));
            if(!alpha)result=E_FAIL;
        }
        notes << std::format("{},{},{},{},{},{}\n",i,reinterpret_cast<uintptr_t>(target),static_cast<int>(view.Format),desc.Width,desc.Height,static_cast<int>(result));
    }
    Logging::Log("[HUD_AUDIT] Candidate surfaces saved to "+folder.string()+"; colour-clipped previews, not final HUD classification");
}
void RecordDraw(ID3D11DeviceContext* context,unsigned kind,unsigned count,unsigned instances) {
    if(insideHudTransform || !auditActive || context!=D3DService::pContext)return;
    std::lock_guard lock(Runtime::mutex);
    if(auditActive && auditRows.size()<8192) {
        const auto row=SnapshotDraw(context,kind,count,instances);auditRows.push_back(row);
        if(Runtime::desktopAuditOnly)MarkerCameraAudit::Draw(context,row.width,row.height,row.depth,row.blend);
        if(!row.depth && row.blend && row.width>=640 && row.height>=480 && auditSurfaces.size()<8) {
            Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;context->OMGetRenderTargets(1,&target,nullptr);
            bool seen=false;for(const auto& saved:auditSurfaces)seen |= saved.Get()==target.Get();
            if(target && !seen)auditSurfaces.push_back(target);
        }
    }
}
void FinishDrawAudit() {
    if(!auditActive.exchange(false))return;
    if(startupSelection) {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> backbuffer;
        D3D11_TEXTURE2D_DESC screen{};
        if(D3DService::pSwapChain && SUCCEEDED(D3DService::pSwapChain->GetBuffer(0,IID_PPV_ARGS(&backbuffer))))backbuffer->GetDesc(&screen);
        int selected=-1;bool ambiguous=false;
        for(size_t i=0;i<auditSurfaces.size();++i) {
            Microsoft::WRL::ComPtr<ID3D11Resource> resource;auditSurfaces[i]->GetResource(&resource);
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;D3D11_TEXTURE2D_DESC desc{};
            D3D11_RENDER_TARGET_VIEW_DESC view{};auditSurfaces[i]->GetDesc(&view);
            if(SUCCEEDED(resource.As(&texture)))texture->GetDesc(&desc);
            if(HudStartupSelection::Matches(static_cast<unsigned>(view.Format),desc.Width,desc.Height,screen.Width,screen.Height)) {
                if(selected>=0)ambiguous=true;
                selected=static_cast<int>(i);
            }
        }
        const uintptr_t key=selected>=0 && !ambiguous ? reinterpret_cast<uintptr_t>(auditSurfaces[selected].Get()) : 0;
        if(startupConfirmation.Confirm(key)) {
            if(auditSurfaces[selected].Get()!=selectedHudTarget.Get()) {
                selectableSurfaces=auditSurfaces;D3DService::SelectHudSurface(selected);
            }
            startupSelection=false;
            nextHudDiscovery=uiPlacementFrame+30;
            Logging::Log("[HUD_STARTUP] UI target confirmed across three frames; automatic HUD/menu preset ready, no image readback");
        } else if(++startupFrames>=8) {
            startupSelection=false;
            nextHudDiscovery=uiPlacementFrame+30;
            Logging::Log("[HUD_STARTUP] UI target not uniquely confirmed; waiting for a stable target");
        } else auditPending=true;
        auditRows.clear();auditSurfaces.clear();return;
    }
    try {
        std::ofstream file(auditFile);
        file << "order,kind,count,instances,ps,vs,rtv,width,height,depth,blend,vx,vy,vw,vh\n";
        for(size_t i=0;i<auditRows.size();++i) { const auto& r=auditRows[i];
            file << std::format("{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}\n",i,r.kind,r.count,r.instances,r.pixel,r.vertex,r.target,r.width,r.height,r.depth,r.blend,r.viewport.TopLeftX,r.viewport.TopLeftY,r.viewport.Width,r.viewport.Height); }
        if(!file)throw std::runtime_error("audit write failed");
        if(Runtime::desktopAuditOnly && !MarkerCameraAudit::Active() && !Runtime::hudAnchorsEnabled)SaveAuditSurfaces();
        else {
            const auto folder=auditFile.parent_path()/auditFile.stem();std::filesystem::create_directories(folder);
            std::ofstream notes(folder/"surfaces.csv");notes << "index,rtv,format,width,height,result\n";
            for(size_t i=0;i<auditSurfaces.size();++i) {
                auto* target=auditSurfaces[i].Get();Microsoft::WRL::ComPtr<ID3D11Resource> resource;target->GetResource(&resource);
                Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;D3D11_TEXTURE2D_DESC desc{};D3D11_RENDER_TARGET_VIEW_DESC view{};target->GetDesc(&view);
                if(SUCCEEDED(resource.As(&texture)))texture->GetDesc(&desc);
                notes << std::format("{},{},{},{},{},metadata_only\n",i,reinterpret_cast<uintptr_t>(target),static_cast<int>(view.Format),desc.Width,desc.Height);
            }
            Logging::Log("[HUD_AUDIT] Metadata only; no GPU readback or image export performed");
        }
        selectableSurfaces=auditSurfaces;
        Logging::Log(std::format("[HUD_AUDIT] Saved {} draw records (limit8192), methods mask={}, file={}; ignore capture timing",auditRows.size(),auditMethods,auditFile.string()));
    } catch(const std::exception& e) { Logging::Log(std::string("[HUD_AUDIT] Failed: ")+e.what()); }
    auditRows.clear();auditSurfaces.clear();
}
unsigned captureMask=0;
void* ResolveLocalJumpStub(void* target) {
    HMODULE originalModule = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(target), &originalModule)) return nullptr;
    void* visited[8]{};
    void* current = target;
    for (size_t depth = 0; depth < 8; ++depth) {
        if (!IsReadable(current, 5)) return nullptr;
        auto* bytes = reinterpret_cast<const unsigned char*>(current);
        if (bytes[0] != 0xE9) return current != target ? current : nullptr;
        for (size_t i = 0; i < depth; ++i) if (visited[i] == current) return nullptr;
        visited[depth] = current;
        int32_t displacement = 0;
        memcpy(&displacement, bytes + 1, sizeof(displacement));
        auto address = static_cast<int64_t>(reinterpret_cast<uintptr_t>(current)) + 5 + displacement;
        if (address <= 0) return nullptr;
        void* next = reinterpret_cast<void*>(static_cast<uintptr_t>(address));
        HMODULE nextModule = nullptr;
        MEMORY_BASIC_INFORMATION info{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(next), &nextModule) || nextModule != originalModule ||
            !VirtualQuery(next, &info, sizeof(info)) || info.State != MEM_COMMIT ||
            (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
            !(info.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return nullptr;
        Logging::Log(std::format("[D3D11] Native E9 stub: 0x{:X} -> 0x{:X}",
            reinterpret_cast<uintptr_t>(current), reinterpret_cast<uintptr_t>(next)));
        current = next;
    }
    return nullptr; // reject cycles and unexpectedly long chains
}
const char* HookErrorName(safetyhook::InlineHook::Error error) {
    using E = safetyhook::InlineHook::Error;
    switch (error.type) {
    case E::BAD_ALLOCATION: return "BAD_ALLOCATION";
    case E::FAILED_TO_DECODE_INSTRUCTION: return "FAILED_TO_DECODE_INSTRUCTION";
    case E::SHORT_JUMP_IN_TRAMPOLINE: return "SHORT_JUMP_IN_TRAMPOLINE";
    case E::IP_RELATIVE_INSTRUCTION_OUT_OF_RANGE: return "IP_RELATIVE_INSTRUCTION_OUT_OF_RANGE";
    case E::UNSUPPORTED_INSTRUCTION_IN_TRAMPOLINE: return "UNSUPPORTED_INSTRUCTION_IN_TRAMPOLINE";
    case E::FAILED_TO_UNPROTECT: return "FAILED_TO_UNPROTECT";
    case E::NOT_ENOUGH_SPACE: return "NOT_ENOUGH_SPACE";
    default: return "UNKNOWN";
    }
}
bool InstallGraphicsHook(SafetyHookInline& destination, void* target, void* callback, const char* name) {
    HMODULE module = nullptr;
    char path[32768]{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(target), &module);
    if (module) GetModuleFileNameA(module, path, sizeof(path));
    Logging::Log(std::format("[D3D11] {} target=0x{:X} module={} offset=0x{:X}", name,
        reinterpret_cast<uintptr_t>(target), path, reinterpret_cast<uintptr_t>(target) - reinterpret_cast<uintptr_t>(module)));
    if (IsReadable(target, 24)) {
        std::string bytes;
        for (size_t i = 0; i < 24; ++i) bytes += std::format("{:02X} ", reinterpret_cast<const unsigned char*>(target)[i]);
        Logging::Log(std::string("[D3D11] Target bytes: ") + bytes);
    }
    // Keep the structured error, which the easy API otherwise discards.
    auto result = safetyhook::InlineHook::create(target, callback, safetyhook::InlineHook::StartDisabled);
    if (!result && result.error().type == safetyhook::InlineHook::Error::IP_RELATIVE_INSTRUCTION_OUT_OF_RANGE) {
        if (void* implementation = ResolveLocalJumpStub(target)) {
            Logging::Log(std::string("[D3D11] ") + name + " relocation failed at entry stub; trying native implementation");
            result = safetyhook::InlineHook::create(implementation, callback, safetyhook::InlineHook::StartDisabled);
        }
    }
    if (!result) {
        const auto error = result.error();
        Logging::Log(std::string("[D3D11] ") + name + " hook failed: " + HookErrorName(error), static_cast<int>(error.type));
        return false;
    }
    destination = std::move(*result);
    if (auto enabled = destination.enable(); !enabled) {
        Logging::Log(std::string("[D3D11] ") + name + " enable failed: " + HookErrorName(enabled.error()));
        return false;
    }
    Logging::Log(std::string("[D3D11] ") + name + " hook installed");
    return true;
}
}
void D3DService::ConfigureStartupHud(const std::string& shader) {
    pendingHudShader=shader;
    EnableHudTransform(true);
    if(auditMethods==15 && hudHookReady) {
        autoHudDiscovery=!Runtime::desktopAuditOnly;
        startupSelection=true;startupFrames=0;startupConfirmation={};auditPending=true;
        Logging::Log("[HUD_STARTUP] Automatic 25% HUD/menu preset; discovering current UI target");
    } else Logging::Log("[HUD_STARTUP] Required draw hooks unavailable; native UI retained");
}
void D3DService::SelectHudSurface(int index) {
    MenuInput::SetVisualActive(false);
    hudSlots.fill(false);hudMayBeBound=false;
    selectedHudTargetKey=nullptr;selectedHudTarget.Reset();
    hudOverlayTarget.Reset();hudOverlayView.Reset();hudOverlayFrame=~0ull;
    selectedHudView.Reset();selectedHudResource.Reset();
    knownHudSlot=D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
    lastHudProducer=lastHudBinding=uiPlacementFrame;
    uiAnchorFailed=false;
    if(index<0 || static_cast<size_t>(index)>=selectableSurfaces.size()) {
        Logging::Log("[HUD_TRANSFORM] No valid selected surface; native HUD retained");return;
    }
    selectedHudTarget=selectableSurfaces[index];selectedHudTargetKey=selectedHudTarget.Get();
    selectableSurfaces[index]->GetResource(&selectedHudResource);
    if(!Runtime::desktopAuditOnly)selectableSurfaces.clear();
    Logging::Log(std::format("[HUD_TRANSFORM] Selected audit surface {}; resource=0x{:X}",index,reinterpret_cast<uintptr_t>(selectedHudResource.Get())));
}
void D3DService::QueueHudShader(const std::string& code) { pendingHudShader=code; }
void D3DService::EnableHudTransform(bool enable) {
    if(enable && !hudHookReady) { Logging::Log("[HUD_TRANSFORM] Resource hook unavailable; native HUD retained");return; }
    if(!enable)MenuInput::SetVisualActive(false);
    if(hudEnabled.exchange(enable)!=enable)Logging::Log(enable ? "[HUD_TRANSFORM] Enabled for selected texture; no CPU readback" : "[HUD_TRANSFORM] Disabled; native bindings retained");
}
void D3DService::RequestHudPreview(const std::filesystem::path& file) {
    if(Runtime::desktopAuditOnly)hudPreviewFile=file;
}
namespace {
void TrackHudResources(ID3D11DeviceContext* context,UINT first,UINT count,ID3D11ShaderResourceView* const* views) {
    if(!insideHudTransform && context==D3DService::pContext && Runtime::enabled && hudEnabled && views &&
       first<D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT && count<=D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT-first) {
        std::lock_guard lock(Runtime::mutex);
        for(UINT i=0;i<count;++i) {
            bool match=false;
            if(views[i] && selectedHudResource) {
                match=views[i]==selectedHudView.Get();
                // The engine can recreate the SRV while keeping the same texture.
                if(!match && (!selectedHudView || first+i==knownHudSlot)) {
                    Microsoft::WRL::ComPtr<ID3D11Resource> resource;views[i]->GetResource(&resource);
                    match=resource.Get()==selectedHudResource.Get();
                    if(match) {
                        selectedHudView=views[i];knownHudSlot=first+i;
                        Logging::Log(std::format("[HUD_TRANSFORM] Native HUD binding identified at slot {}; processing deferred until draw",first+i));
                    }
                }
                if(match)lastHudBinding=uiPlacementFrame;
            }
            hudSlots[first+i]=match;
        }
        hudMayBeBound=std::any_of(hudSlots.begin(),hudSlots.end(),[](bool x){return x;});
    }
}
}
void D3DService::ResourcesDetour(ID3D11DeviceContext* context,UINT first,UINT count,ID3D11ShaderResourceView* const* views) {
    TrackHudResources(context,first,count,views);
    resourcesHook.unsafe_call<void>(context,first,count,views);
}
void D3DService::DrawDetour(ID3D11DeviceContext* context,UINT count,UINT start) {
    DispatchUiDraw(context,0,count,1,start,0,0,[&]{drawHook.unsafe_call<void>(context,count,start);});
}
void D3DService::IndexedDetour(ID3D11DeviceContext* context,UINT count,UINT start,INT base) {
    DispatchUiDraw(context,1,count,1,start,base,0,[&]{indexedHook.unsafe_call<void>(context,count,start,base);});
}
void D3DService::IndexedInstancedDetour(ID3D11DeviceContext* context,UINT count,UINT instances,UINT start,INT base,UINT first) {
    DispatchUiDraw(context,2,count,instances,start,base,first,[&]{indexedInstancedHook.unsafe_call<void>(context,count,instances,start,base,first);});
}
void D3DService::InstancedDetour(ID3D11DeviceContext* context,UINT count,UINT instances,UINT start,UINT first) {
    DispatchUiDraw(context,3,count,instances,start,0,first,[&]{instancedHook.unsafe_call<void>(context,count,instances,start,first);});
}
void D3DService::RequestHudAudit(const std::filesystem::path& file) {
    if(!auditMethods) { Logging::Log("[HUD_AUDIT] No draw audit hooks available");return; }
    if(auditActive || auditPending) { Logging::Log("[HUD_AUDIT] Capture already pending");return; }
    startupSelection=false;
    if(Runtime::desktopAuditOnly)MarkerCameraAudit::RequestDraws();
    auditFile=file;auditPending=true;
    Logging::Log("[HUD_AUDIT] One-frame draw-state audit requested; drawing unchanged");
}
void D3DService::OnUIDraw() {
    std::lock_guard lock(Runtime::mutex);
    if (!Runtime::enabled || !OpenXRService::xrRunning) return;
    if (auto* target = OpenXRService::UITarget()) {
        if (!uiTargetsSaved) {
            pContext->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedUI, &savedDepth);
            uiTargetsSaved = true;
        }
        pContext->OMSetRenderTargets(1, &target, nullptr);
    }
}
void D3DService::RestoreUITargets() {
    if (!uiTargetsSaved) return;
    pContext->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, savedUI, savedDepth);
    for (auto*& view : savedUI) { if (view) view->Release(); view = nullptr; }
    if (savedDepth) savedDepth->Release();
    savedDepth = nullptr; uiTargetsSaved = false;
}
void D3DService::RequestStereoCapture(const std::filesystem::path& directory) {
    if(!Runtime::desktopAuditOnly) { Logging::Log("[STEREO_CAPTURE] GPU readback disabled in VR");return; }
    captureDirectory=directory; capturePending=true; captureMask=0;
    Logging::Log("[STEREO_CAPTURE] Requested one pair; readback will briefly stall, disregard timing windows");
}
HRESULT D3DService::PresentDetour(IDXGISwapChain* swapChain, UINT interval, UINT flags) {
    // Ignore other swapchains: the methods are shared across D3D11 objects.
    if (flags & DXGI_PRESENT_TEST) return presentHook.call<HRESULT>(swapChain, interval, flags);
    bool gamePresent = false;
    const auto entry = Timing::Clock::now();
    {
        std::unique_lock lock(Runtime::mutex);
        const auto lockAcquired = Timing::Clock::now();
        if (Runtime::enabled && (swapChain == pSwapChain || MatchesGameSwapchain(swapChain, pDevice, gameWindow))) {
            gamePresent = true;
            if(Runtime::desktopAuditOnly)MarkerGeometryAudit::Present(pDevice,pContext);
            ++uiPlacementFrame;
            for(auto& [ctx,transform]:uiAnchors)if(transform)transform->NextFrame();
            FinishDrawAudit();
            Timing::Add(Timing::LockWait, static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(lockAcquired-entry).count()));
            static Timing::Clock::time_point previousReturn{};
            if (previousReturn.time_since_epoch().count()) Timing::Add(Timing::GameGap,
                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(entry-previousReturn).count()));
            previousReturn = entry;
            if (pSwapChain != swapChain) {
                pSwapChain = swapChain;
                Logging::Log("[D3D11] Actual game DXGI Present callback observed");
            }
            if(!Runtime::desktopAuditOnly) {
            BF2Service::RefreshMainView();
            RestoreUITargets();
            OpenXRService::HandleStates();
            if (!OpenXRService::xrExiting) {
                if (auto* eye = OpenXRService::EyeTarget()) {
                    if (Runtime::cameraEnabled && Runtime::projectionEnabled) {
                        static uint64_t samples=0, eyeMismatch=0, revisionMismatch=0;
                        ++samples;
                        eyeMismatch += BF2Service::projectionEye != OpenXRService::RenderEye();
                        revisionMismatch += BF2Service::projectionRevision != OpenXRService::poseRevision;
                        static auto auditStart = Timing::Clock::now();
                        if (Timing::Clock::now()-auditStart >= std::chrono::seconds(5)) {
                            Logging::Log(std::format("[EYE_AUDIT] samples={} eye_mismatch={} pose_revision_mismatch={}; CPU hook attribution, not GPU provenance", samples, eyeMismatch, revisionMismatch));
                            samples=eyeMismatch=revisionMismatch=0; auditStart=Timing::Clock::now();
                        }
                    }
                    Timing::Scope captureTiming(Timing::CopyCPU);
                    Microsoft::WRL::ComPtr<ID3D11Texture2D> backbuffer;
                    DXGI_SWAP_CHAIN_DESC desc{};
                    HRESULT hr = swapChain->GetDesc(&desc);
                    if (SUCCEEDED(hr)) hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&backbuffer));
                    if (SUCCEEDED(hr)) {
                        D3D11_TEXTURE2D_DESC actual{}; backbuffer->GetDesc(&actual);
                        static UINT loggedWidth=0,loggedHeight=0;
                        if (actual.Width!=loggedWidth || actual.Height!=loggedHeight) {
                            loggedWidth=actual.Width; loggedHeight=actual.Height;
                            auto* screen=DXRenderer::getScreen();
                            Logging::Log(std::format("[D3D11] Source backbuffer={}x{} engine viewport={}x{} XR destination={}x{}",actual.Width,actual.Height,isValidPtr(screen)?screen->bufferWidth:0,isValidPtr(screen)?screen->bufferHeight:0,OpenXRService::swapchainWidth,OpenXRService::swapchainHeight));
                        }
                    }
                    if (SUCCEEDED(hr)) {
                        static float lastSharpness=0.f;
                        const float sharpness=Runtime::eyeSharpness;
                        if(sharpness!=lastSharpness) {
                            lastSharpness=sharpness;
                            const HRESULT filter=frameBlitter->SetEyeSharpness(sharpness);
                            if(SUCCEEDED(filter))Logging::Log(std::format("[D3D11] Eye sharpening set to {:.2f}",sharpness));
                            else Logging::Log("[D3D11] Eye sharpening update failed; previous filter retained",static_cast<int>(filter));
                        }
                        hr = frameBlitter->Copy(backbuffer.Get(), desc.BufferDesc.Format, eye,
                            OpenXRService::swapchainWidth, OpenXRService::swapchainHeight);
                    }
                    if (FAILED(hr)) {
                        Logging::Log("[D3D11] Completed-frame capture failed", static_cast<int>(hr));
                        OpenXRService::xrExiting = true;
                    } else {
                        const int captureEye=OpenXRService::RenderEye();
                        if (capturePending && captureEye==0) { captureMask=3; capturePending=false; }
                        if (captureMask & (1u<<captureEye)) {
                            try {
                                EyePixels pixels;
                                const HRESULT read=ReadEyePixels(pDevice,pContext,eye,pixels);
                                if (FAILED(read)) {
                                    Logging::Log("[STEREO_CAPTURE] Readback failed",static_cast<int>(read)); captureMask=0;
                                } else {
                                    std::filesystem::create_directories(captureDirectory);
                                    std::ofstream file(captureDirectory/(captureEye ? "right.ppm" : "left.ppm"),std::ios::binary);
                                    file << "P6\n" << pixels.width << " " << pixels.height << "\n255\n";
                                    file.write(reinterpret_cast<const char*>(pixels.rgb.data()),static_cast<std::streamsize>(pixels.rgb.size()));
                                    if (!file) throw std::runtime_error("eye image file write failed");
                                    std::ofstream metadata(captureDirectory/"metadata.txt",std::ios::app);
                                    metadata << std::format("eye={} pose_revision={} projection_eye={} projection_revision={} swap_images={} dimensions={}x{}\n",captureEye,OpenXRService::poseRevision,BF2Service::projectionEye,BF2Service::projectionRevision,Runtime::swapCapturedEyes.load(),pixels.width,pixels.height);
                                    captureMask &= ~(1u<<captureEye);
                                    if (!captureMask) Logging::Log("[STEREO_CAPTURE] Pair saved to "+captureDirectory.string());
                                }
                            } catch (const std::exception& error) {
                                captureMask=0; capturePending=false;
                                Logging::Log(std::string("[STEREO_CAPTURE] Aborted: ")+error.what());
                            }
                        }
                        static bool firstCopy = true;
                        if (firstCopy) { Logging::Log("[D3D11] Completed game backbuffer captured into eye image"); firstCopy = false; }
                    }
                }
                if (!OpenXRService::xrExiting) {
                    OpenXRService::FinishEye();
                    OpenXRService::PrepareDraw();

                }
            }
            }
            if(autoHudDiscovery && hudEnabled && !startupSelection && !auditPending && !auditActive &&
                uiPlacementFrame>=nextHudDiscovery &&
                HudStartupSelection::NeedsRediscovery(uiPlacementFrame,lastHudProducer,lastHudBinding)) {
                startupSelection=true;startupFrames=0;startupConfirmation={};auditPending=true;
                Logging::Log("[HUD_STARTUP] HUD draw or binding lost; checking for replacement target");
            }
            if(auditPending) {
                auditRows.clear();auditSurfaces.clear();auditRows.reserve(8192);auditPending=false;auditActive=true;
            }
        }
    }
    // CPU duration includes any desktop/vsync/driver wait.
    const auto presentStart = Timing::Clock::now();
    const HRESULT result = presentHook.call<HRESULT>(swapChain, interval, flags);
    if (gamePresent) { Timing::Elapsed(Timing::DesktopPresent, presentStart); Timing::Report(); }
    return result;
}
int D3DService::CaptureResources() {
    pDevice = DXRenderer::getDevice(); pContext = DXRenderer::getContext();
    gameWindow = FindWindowW(L"Frostbite", L"STAR WARS Battlefront II");
    if (!pDevice || !pContext || !gameWindow) { Logging::Log("[D3D11] Game device/window unavailable; use DX11 and wait for the menu"); return -1; }
    DxgiProbe probe;
    const HRESULT hr = probe.Create(pDevice);
    if (FAILED(hr)) { Logging::Log("[D3D11] DXGI method probe failed", static_cast<int>(hr)); return -1; }
    frameBlitter = std::make_unique<FrameBlitter>();
    const HRESULT blitHr = frameBlitter->Initialize(pDevice, pContext);
    if (FAILED(blitHr)) { Logging::Log("[D3D11] Isolated frame-copy pipeline unavailable", static_cast<int>(blitHr)); return -1; }
    presentAddress = probe.PresentAddress();
    pSwapChain = nullptr;
    Logging::Log("[D3D11] Captured real IDXGISwapChain::Present from temporary hidden probe");
    return 0;
}
int D3DService::Initialize() {
    if (!InstallGraphicsHook(presentHook, presentAddress, reinterpret_cast<void*>(PresentDetour), "DXGI Present")) return -1;
    // Method slots verified against installed D3D11 SDK interface definition.
    auto** methods=*reinterpret_cast<void***>(pContext);
    if(InstallGraphicsHook(drawHook,methods[13],reinterpret_cast<void*>(DrawDetour),"Draw audit"))auditMethods|=1;
    if(InstallGraphicsHook(indexedHook,methods[12],reinterpret_cast<void*>(IndexedDetour),"DrawIndexed audit"))auditMethods|=2;
    if(InstallGraphicsHook(indexedInstancedHook,methods[20],reinterpret_cast<void*>(IndexedInstancedDetour),"DrawIndexedInstanced audit"))auditMethods|=4;
    if(InstallGraphicsHook(instancedHook,methods[21],reinterpret_cast<void*>(InstancedDetour),"DrawInstanced audit"))auditMethods|=8;
    hudHookReady=InstallGraphicsHook(resourcesHook,methods[8],reinterpret_cast<void*>(ResourcesDetour),"PS resource HUD transform");
    if(Runtime::desktopAuditOnly || Runtime::hudAnchorsEnabled) {
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> deferred;
        if(SUCCEEDED(pDevice->CreateDeferredContext(0,&deferred))) {
            auto** dm=*reinterpret_cast<void***>(deferred.Get());
            if(dm[8]!=methods[8])InstallGraphicsHook(deferredResourcesHook,dm[8],reinterpret_cast<void*>(DeferredResources),"Alternate PS resource entry");
            if(dm[13]!=methods[13])InstallGraphicsHook(deferredDrawHook,dm[13],reinterpret_cast<void*>(DeferredDraw),"Deferred Draw metadata");
            if(dm[12]!=methods[12])InstallGraphicsHook(deferredIndexedHook,dm[12],reinterpret_cast<void*>(DeferredIndexed),"Deferred DrawIndexed metadata");
            if(dm[20]!=methods[20])InstallGraphicsHook(deferredIndexedInstancedHook,dm[20],reinterpret_cast<void*>(DeferredIndexedInstanced),"Deferred DrawIndexedInstanced metadata");
            if(dm[21]!=methods[21])InstallGraphicsHook(deferredInstancedHook,dm[21],reinterpret_cast<void*>(DeferredInstanced),"Deferred DrawInstanced metadata");
        }
    }
    MenuInput::Initialize(gameWindow);
    // Capture only the finished backbuffer at Present. Replaying arbitrary
    // engine Draw passes into the eye targets mixes incompatible postprocess data.
    OpenXRService::uiLayerEnabled = false;
    Logging::Log("[D3D11] Completed-frame capture enabled; per-Draw replay and separate HUD hook disabled");
    return 0;
}
int D3DService::Uninitialize() {
    uiAnchors.clear();
    MarkerGeometryAudit::Cancel();
    MenuInput::Shutdown();hudEnabled=false;menuTransform.reset();
    selectedHudTargetKey=nullptr;selectedHudTarget.Reset();
    hudOverlayTarget.Reset();hudOverlayView.Reset();hudOverlayFrame=~0ull;
    selectedHudView.Reset();selectedHudResource.Reset();selectableSurfaces.clear();hudTransform.reset();
    auditActive=false;auditPending=false;startupSelection=false;autoHudDiscovery=false;
    RestoreUITargets();
    frameBlitter.reset();
    // Detours stay resident and pass through; removing live trampolines is unsafe.
    return 0;
}
}
