#include <VrOptions.h>
bool BF2VR::VrOptions::Key(UINT,WPARAM,LPARAM) {return false;}
#include <MenuInput.h>
#include <Logging.h>
#include <DxgiProbe.h>
#include <safetyhook.hpp>
#include <iostream>
#include <stdexcept>
SafetyHookInline hook;
int callbacks = 0;
HRESULT Present(IDXGISwapChain* chain, UINT interval, UINT flags) {
    ++callbacks;
    return hook.unsafe_call<HRESULT>(chain, interval, flags);
}
namespace BF2VR { void Logging::Log(const std::string& text,int){std::cout<<text<<"\n";} }
WNDPROC nativeTestWindow=nullptr;LPARAM observedMouse=0;
LRESULT CALLBACK TestMenuWindow(HWND h,UINT message,WPARAM w,LPARAM l) {
    if(message==WM_MOUSEMOVE)observedMouse=l;
    return CallWindowProcW(nativeTestWindow,h,message,w,l);
}
SafetyHookInline drawAuditHook,indexedAuditHook,instancedAuditHook,indexedInstancedAuditHook;
SafetyHookInline resourceAuditHook;bool resourceArguments=false;
void TestResources(ID3D11DeviceContext* c,UINT first,UINT count,ID3D11ShaderResourceView* const* views) {resourceArguments=first==3 && count==1 && views && !views[0];resourceAuditHook.unsafe_call<void>(c,first,count,views);}
unsigned drawCalls=0;bool drawArguments=true;
void TestDraw(ID3D11DeviceContext* c,UINT n,UINT start) { ++drawCalls;drawArguments &= n==0 && start==7;drawAuditHook.unsafe_call<void>(c,n,start); }
void TestIndexed(ID3D11DeviceContext* c,UINT n,UINT start,INT base) { ++drawCalls;drawArguments &= n==0 && start==9 && base==-3;indexedAuditHook.unsafe_call<void>(c,n,start,base); }
void TestInstanced(ID3D11DeviceContext* c,UINT n,UINT instances,UINT start,UINT first) { ++drawCalls;drawArguments &= n==0 && instances==0 && start==5 && first==11;instancedAuditHook.unsafe_call<void>(c,n,instances,start,first); }
void TestIndexedInstanced(ID3D11DeviceContext* c,UINT n,UINT instances,UINT start,INT base,UINT first) { ++drawCalls;drawArguments &= n==0 && instances==0 && start==13 && base==-2 && first==17;indexedInstancedAuditHook.unsafe_call<void>(c,n,instances,start,base,first); }
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &device, nullptr, nullptr)), "create software D3D11 device");
        BF2VR::DxgiProbe probe, live;
        require(SUCCEEDED(probe.Create(device.Get())), "capture real DXGI method");
        require(SUCCEEDED(live.Create(device.Get())), "create independent live swapchain");
        require(BF2VR::MatchesGameSwapchain(live.chain.Get(), device.Get(), live.window), "accept matching device and window");
        require(!BF2VR::MatchesGameSwapchain(live.chain.Get(), device.Get(), probe.window), "reject unrelated window");
        auto result = safetyhook::InlineHook::create(probe.PresentAddress(), reinterpret_cast<void*>(Present), safetyhook::InlineHook::StartDisabled);
        require(result.has_value(), "install real DXGI Present hook");
        hook = std::move(*result);
        require(hook.enable().has_value(), "enable after storing trampoline");
        for (int i = 0; i < 4; ++i) require(SUCCEEDED(live.chain->Present(0, DXGI_PRESENT_TEST)), "independent swapchain Present");
        require(callbacks == 4, "probe target intercepts real Present calls");
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;device->GetImmediateContext(&context);
        auto** methods=*reinterpret_cast<void***>(context.Get());
        auto install=[](SafetyHookInline& h,void* target,void* callback) {
            auto made=safetyhook::InlineHook::create(target,callback,safetyhook::InlineHook::StartDisabled);
            require(made.has_value(),"draw method hook creation");h=std::move(*made);
            require(h.enable().has_value(),"draw method hook enabled after storage");
        };
        install(drawAuditHook,methods[13],reinterpret_cast<void*>(TestDraw));
        install(indexedAuditHook,methods[12],reinterpret_cast<void*>(TestIndexed));
        install(instancedAuditHook,methods[21],reinterpret_cast<void*>(TestInstanced));
        install(indexedInstancedAuditHook,methods[20],reinterpret_cast<void*>(TestIndexedInstanced));
        install(resourceAuditHook,methods[8],reinterpret_cast<void*>(TestResources));
        ID3D11ShaderResourceView* emptyView=nullptr;context->PSSetShaderResources(3,1,&emptyView);
        require(resourceArguments,"PS resource hook preserves slot/count/resource-array calling convention");
        resourceAuditHook.reset();
        context->Draw(0,7);context->DrawIndexed(0,9,-3);
        context->DrawInstanced(0,0,5,11);context->DrawIndexedInstanced(0,0,13,-2,17);
        require(drawCalls==4 && drawArguments,"actual context methods preserve all draw call arguments");
        drawAuditHook.reset();indexedAuditHook.reset();instancedAuditHook.reset();indexedInstancedAuditHook.reset();
        nativeTestWindow=reinterpret_cast<WNDPROC>(GetWindowLongPtrW(probe.window,GWLP_WNDPROC));
        require(SetWindowLongPtrW(probe.window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(TestMenuWindow))!=0,"install hidden test window handler");
        BF2VR::MenuInput::Initialize(probe.window);
        require(GetWindowLongPtrW(probe.window,GWLP_WNDPROC)!=reinterpret_cast<LONG_PTR>(TestMenuWindow),"native menu input window hook installed");
        const LPARAM originalMouse=MAKELPARAM(7,9);SendMessageW(probe.window,WM_MOUSEMOVE,0,originalMouse);
        require(observedMouse==originalMouse,"disabled menu mapping passes native mouse message unchanged");
        POINT cursor{0x7fffffff,0x7fffffff};
        require(GetCursorPos(&cursor) && cursor.x!=0x7fffffff && cursor.y!=0x7fffffff,"cursor hook preserves native BOOL and POINT output");
        BF2VR::MenuInput::Shutdown();
        require(GetWindowLongPtrW(probe.window,GWLP_WNDPROC)==reinterpret_cast<LONG_PTR>(TestMenuWindow),"menu shutdown restores previous window procedure");
        SetWindowLongPtrW(probe.window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(nativeTestWindow));
        hook.reset();
        std::cout << "DXGI discovery, identity filtering and Present hook checks passed\n";
        return 0;
    } catch (const std::exception& error) { BF2VR::MenuInput::Shutdown();resourceAuditHook.reset();drawAuditHook.reset();indexedAuditHook.reset();instancedAuditHook.reset();indexedInstancedAuditHook.reset();hook.reset(); std::cerr << error.what() << '\n'; return 1; }
}
