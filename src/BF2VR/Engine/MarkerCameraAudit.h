#pragma once
#include <Classes.h>
#include <Runtime.h>
#include <Logging.h>
#include <safetyhook.hpp>
#include <intrin.h>
#include <atomic>
#include <format>
#include <mutex>
#include <unordered_set>
#include <d3d11.h>
#include <wrl/client.h>
namespace BF2VR {
// Opt-in CPU metadata only. Original camera outputs and rendering remain untouched.
class MarkerCameraAudit {
    static inline SafetyHookInline hook,viewHook;
    static inline std::atomic<ULONGLONG> viewDeadline{0};
    static inline std::mutex auditMutex;
    static inline std::unordered_set<uintptr_t> viewCallers;
    static inline std::atomic<unsigned> drawSamples{0};
    static std::string Stack() {
        std::string stack;void* frames[20]{};const auto count=CaptureStackBackTrace(1,20,frames,nullptr);
        for(unsigned i=0;i<count;++i)stack+=std::format(" 0x{:X}",reinterpret_cast<uintptr_t>(frames[i]));return stack;
    }
    static RenderView* ObserveView(void* owner) {
        const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
        auto* view=viewHook.call<RenderView*>(owner);
        if(Runtime::enabled && GetTickCount64()<viewDeadline.load()) {
            std::lock_guard lock(auditMutex);
            if(viewCallers.size()<32 && viewCallers.insert(caller).second && IsReadable(view,sizeof(*view)))
                Logging::Log(std::format("[MARKER_VIEW] caller=0x{:X} view=0x{:X} fov={} aspect={} transform={} stack={}",caller,reinterpret_cast<uintptr_t>(view),view->fov,view->aspectRatio,view->transform.ToString(),Stack()));
        }
        return view;
    }
    static inline std::atomic<unsigned> samples{0};
    static bool Observe(void* owner,Matrix4* projection,Matrix4* transform,Vec2* viewport) {
        const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
        const bool result=hook.call<bool>(owner,projection,transform,viewport);
        unsigned remaining=samples.load();
        if(Runtime::enabled && remaining && samples.compare_exchange_strong(remaining,remaining-1)) {
            if(result && IsReadable(projection,sizeof(*projection)) && IsReadable(transform,sizeof(*transform)) && IsReadable(viewport,sizeof(*viewport))) {
                std::string stack;void* frames[12]{};
                const auto count=CaptureStackBackTrace(0,12,frames,nullptr);
                for(unsigned i=0;i<count;++i)stack+=std::format(" 0x{:X}",reinterpret_cast<uintptr_t>(frames[i]));
                Logging::Log(std::format("[MARKER_CAMERA] caller=0x{:X} owner=0x{:X} viewport={}x{} VP={} transform={} stack={}",caller,reinterpret_cast<uintptr_t>(owner),viewport->x,viewport->y,projection->ToString(),transform->ToString(),stack));
            } else Logging::Log("[MARKER_CAMERA] Getter returned no camera; outputs untouched");
        }
        return result;
    }
public:
    static void Request() {
        if(hook)samples=3;
        { std::lock_guard lock(auditMutex);viewCallers.clear(); }
        if(viewHook)viewDeadline=GetTickCount64()+1500;
    }
    static bool Active() { return static_cast<bool>(hook) || static_cast<bool>(viewHook); }
    static void RequestDraws() { drawSamples=4; }
    static void Draw(ID3D11DeviceContext* context,unsigned width,unsigned height,bool depth,bool blend) {
        unsigned left=drawSamples.load();
        if(!left || depth || !blend || width<640 || height<480)return;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;context->OMGetRenderTargets(1,&target,nullptr);
        D3D11_RENDER_TARGET_VIEW_DESC desc{};if(!target)return;target->GetDesc(&desc);
        if(desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || !drawSamples.compare_exchange_strong(left,left-1))return;
        ID3D11Buffer* buffers[4]{};UINT strides[4]{},offsets[4]{};context->IAGetVertexBuffers(0,4,buffers,strides,offsets);
        std::string vertices;
        for(unsigned i=0;i<4;++i)if(buffers[i]) { D3D11_BUFFER_DESC info{};buffers[i]->GetDesc(&info);
            vertices+=std::format(" slot{}:ptr=0x{:X},bytes={},usage={},stride={},offset={}",i,reinterpret_cast<uintptr_t>(buffers[i]),info.ByteWidth,static_cast<unsigned>(info.Usage),strides[i],offsets[i]);buffers[i]->Release(); }
        Logging::Log(std::format("[MARKER_DRAW] target=0x{:X} size={}x{} VB={} stack={}",reinterpret_cast<uintptr_t>(target.Get()),width,height,vertices,Stack()));
    }
    static void Initialize() {
        const unsigned char expected[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x30};
        auto* address=reinterpret_cast<void*>(GameAddress(0x146830160));
        if(!IsReadable(address,sizeof(expected)) || memcmp(address,expected,sizeof(expected))) { Logging::Log("[MARKER_CAMERA] Camera-info signature mismatch; diagnostic skipped");return; }
        auto made=safetyhook::InlineHook::create(address,reinterpret_cast<void*>(Observe),safetyhook::InlineHook::StartDisabled);
        if(!made) { Logging::Log("[MARKER_CAMERA] Diagnostic hook creation failed");return; }
        hook=std::move(*made);
        if(!hook.enable()) { Logging::Log("[MARKER_CAMERA] Diagnostic hook enable failed");return; }
        const unsigned char viewExpected[]{0x48,0x8b,0x81,0x80,0x05,0,0,0xc3};
        auto* viewAddress=reinterpret_cast<void*>(GameAddress(0x147c60e80));
        if(IsReadable(viewAddress,sizeof(viewExpected)) && !memcmp(viewAddress,viewExpected,sizeof(viewExpected))) {
            auto madeView=safetyhook::InlineHook::create(viewAddress,reinterpret_cast<void*>(ObserveView),safetyhook::InlineHook::StartDisabled);
            if(madeView) { viewHook=std::move(*madeView);if(!viewHook.enable())Logging::Log("[MARKER_VIEW] Observation enable failed"); }
        } else Logging::Log("[MARKER_VIEW] Getter signature mismatch; skipped");
        Request();
        Logging::Log("[MARKER_CAMERA] Read-only camera-info observation active; native outputs retained");
    }
};
}
