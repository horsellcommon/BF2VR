#include <MenuInput.h>
#include <VrOptions.h>
#include <ActionsService.h>
#include <OpenXRService.h>
#include <CameraAnchor.h>
#include <QuestPointer.h>
#include <QuestGamepad.h>
#include <MenuCoordinates.h>
#include <Runtime.h>
#include <Logging.h>
#include <safetyhook.hpp>
#include <intrin.h>
#include <atomic>
#include <chrono>
#include <format>
namespace BF2VR {
namespace {
HWND window=nullptr;std::atomic<WNDPROC> previous{nullptr};bool unicode=true;
SafetyHookInline cursorHook;
std::atomic_bool ready=false,enabled=false,visual=false;
std::atomic<float> scale{.5f};
std::atomic_uint64_t cursorReads=0,mouseMessages=0,mapped=0;
QuestPointer questPointer;
POINT lastPointer{};
bool pointerDown=false;
void ReleaseQuestClick() {
    if(pointerDown && window && IsWindow(window))
        PostMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(lastPointer.x,lastPointer.y));
    pointerDown=false;
}
#ifndef BF2VR_MENU_INPUT_TEST
QuestPress pausePress;
QuestHold perspectiveHold;
bool aimDown=false;
class QuestKey {
    const WORD key;
    const char* label;
    bool held=false;
    std::chrono::steady_clock::time_point release{};
    void Set(bool down) {
        INPUT event{};
        event.type=INPUT_KEYBOARD;
        event.ki.wScan=static_cast<WORD>(MapVirtualKeyW(key,MAPVK_VK_TO_VSC));
        if(!event.ki.wScan)return;
        event.ki.dwFlags=KEYEVENTF_SCANCODE|(down?0:KEYEVENTF_KEYUP);
        const bool sent=SendInput(1,&event,sizeof(event))==1;
        if(sent)held=down;
        if(Runtime::questInputTrace && down)
            Logging::Log(std::format("[QUEST_INPUT] {} scan-code press {}",label,sent?"sent":"rejected"));
    }
public:
    QuestKey(WORD code,const char* name):key(code),label(name) {}
    void Hold(bool down) {
        if(held==down)return;
        if(!down || (GetForegroundWindow()==window && !(GetAsyncKeyState(key)&0x8000)))Set(down);
    }
    void Update(bool active,bool pressed) {
        const auto now=std::chrono::steady_clock::now();
        if(held && (!active || now>=release))Set(false);
        if(active && pressed && !held && GetForegroundWindow()==window && !(GetAsyncKeyState(key)&0x8000)) {
            Set(true);
            release=now+std::chrono::milliseconds(50);
        }
    }
};
QuestKey pauseKey(VK_ESCAPE,"Escape"),perspectiveKey('C',"Perspective");
void SetQuestAim(bool aim) {
    if(aim==aimDown)return;
    INPUT event{};
    event.type=INPUT_MOUSE;
    event.mi.dwFlags=aim?MOUSEEVENTF_RIGHTDOWN:MOUSEEVENTF_RIGHTUP;
    const bool sent=SendInput(1,&event,sizeof(event))==1;
    if(sent)aimDown=aim;
    if(Runtime::questInputTrace && sent)Logging::Log(aim?"[QUEST_INPUT] Mouse aim down":"[QUEST_INPUT] Mouse aim up");
}
void UpdateQuestMenuAndAim() {
    const bool active=Runtime::enabled && OpenXRService::xrFocused &&
        window && GetForegroundWindow()==window && !VrOptions::IsVisible();
    const auto& grip=ActionsService::gripValue[0];
    const bool pause=pausePress.Update(active && grip.isActive,grip.currentState>.8f);
    CURSORINFO cursor{sizeof(CURSORINFO)};
    const auto& trigger=ActionsService::triggerValue[0];
    const bool gameplay=active && GetCursorInfo(&cursor) && !(cursor.flags&CURSOR_SHOWING);
    const bool aim=gameplay && !pause && trigger.isActive && trigger.currentState>(aimDown?.4f:.55f);
    SetQuestAim(aim);
    pauseKey.Update(active,pause);
    const auto& rightClick=ActionsService::stickClick[1];
    const bool perspective=perspectiveHold.Update(gameplay && !pause && rightClick.isActive,
        rightClick.currentState!=XR_FALSE);
    perspectiveKey.Hold(perspective);
}
#endif
uintptr_t imageBegin=0,imageEnd=0;
bool VisibleGameCursor() {
    if(!window || GetForegroundWindow()!=window)return false;
    CURSORINFO info{};info.cbSize=sizeof(info);
    return GetCursorInfo(&info) && (info.flags&CURSOR_SHOWING);
}
bool MapPoint(POINT& p,bool screen) {
    if(!Runtime::enabled || !ready || !enabled || !visual || !VisibleGameCursor())return false;
    RECT bounds{};if(!GetClientRect(window,&bounds) || bounds.right<=0 || bounds.bottom<=0)return false;
    if(screen && !ScreenToClient(window,&p))return false;
    auto source=MenuPointToNative({static_cast<double>(p.x),static_cast<double>(p.y)},bounds.right,bounds.bottom,scale.load());
    if(source) { p.x=static_cast<LONG>(std::lround(source->x));p.y=static_cast<LONG>(std::lround(source->y)); }
    else { p.x=-16000;p.y=-16000; }
    if(screen && !ClientToScreen(window,&p))return false;
    ++mapped;return true;
}
BOOL WINAPI CursorPosition(LPPOINT point) {
    const BOOL ok=cursorHook.unsafe_call<BOOL>(point);
    const auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress());
    if(ok && point && caller>=imageBegin && caller<imageEnd && VisibleGameCursor()) { ++cursorReads;MapPoint(*point,true); }
    return ok;
}
LRESULT CALLBACK WindowProcedure(HWND h,UINT message,WPARAM w,LPARAM l) {
    if(h==window && VrOptions::Key(message,w,l))return 0;
    bool client=false,screen=false;
    switch(message) {
    case WM_MOUSEMOVE:case WM_LBUTTONDOWN:case WM_LBUTTONUP:case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:case WM_RBUTTONUP:case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:case WM_MBUTTONUP:case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:case WM_XBUTTONUP:case WM_XBUTTONDBLCLK:client=true;break;
    case WM_MOUSEWHEEL:case WM_MOUSEHWHEEL:screen=true;break;
    }
    if(h==window && (client||screen) && VisibleGameCursor()) {
        ++mouseMessages;POINT p{static_cast<short>(LOWORD(l)),static_cast<short>(HIWORD(l))};
        if(MapPoint(p,screen))l=MAKELPARAM(static_cast<short>(p.x),static_cast<short>(p.y));
    }
    const auto original=previous.load();
    if(!original)return unicode?DefWindowProcW(h,message,w,l):DefWindowProcA(h,message,w,l);
    return unicode ? CallWindowProcW(original,h,message,w,l) : CallWindowProcA(original,h,message,w,l);
}
}
void MenuInput::Initialize(HWND h) {
    window=h;if(!window)return;
    DWORD owner=0;GetWindowThreadProcessId(window,&owner);if(owner!=GetCurrentProcessId())return;
    imageBegin=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(imageBegin);
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(imageBegin+dos->e_lfanew);imageEnd=imageBegin+nt->OptionalHeader.SizeOfImage;
    auto made=safetyhook::InlineHook::create(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetCursorPos")),reinterpret_cast<void*>(CursorPosition),safetyhook::InlineHook::StartDisabled);
    if(!made) { Logging::Log("[MENU_INPUT] Cursor hook unavailable; native menus retained");return; }
    cursorHook=std::move(*made);
    if(!cursorHook.enable()) { Logging::Log("[MENU_INPUT] Cursor hook enable failed; native menus retained");return; }
    unicode=IsWindowUnicode(window)!=FALSE;
    previous=reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window,GWLP_WNDPROC));
    if(!previous)return;
    SetLastError(0);
    auto old=SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(WindowProcedure));
    if(!old) { Logging::Log("[MENU_INPUT] Window input interception unavailable; native menus retained",GetLastError());return; }
    previous=reinterpret_cast<WNDPROC>(old);ready=true;
    Logging::Log("[MENU_INPUT] Pointer sources observed; remapping remains opt-in");
}
void MenuInput::Shutdown() {
#ifndef BF2VR_MENU_INPUT_TEST
    SetQuestAim(false);
    pauseKey.Update(false,false);perspectiveKey.Hold(false);
    pausePress.Update(false,false);perspectiveHold.Update(false,false);
#endif
    ReleaseQuestClick();questPointer.Reset();
    enabled=false;visual=false;ready=false;
    if(window && previous && IsWindow(window) && GetWindowLongPtrW(window,GWLP_WNDPROC)==reinterpret_cast<LONG_PTR>(WindowProcedure))
        SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(previous.load()));
    // Cursor trampoline stays resident, passing through after Runtime disables.
}
void MenuInput::Configure(bool value,float factor) {
    if(!std::isfinite(factor)||factor<.25f||factor>1.f)return;
    if(scale.load()!=factor) { visual=false;scale=factor; }
    if(!value)visual=false;
    if(enabled.exchange(value)!=value)Logging::Log(value?"[MENU_INPUT] Scaled menus requested; waiting for verified pointer activity":"[MENU_INPUT] Native menu size/input restored");
}
bool MenuInput::GameCursorVisible() { return VisibleGameCursor(); }
#ifndef BF2VR_MENU_INPUT_TEST
void MenuInput::UpdateQuestPointer() {
    UpdateQuestMenuAndAim();
    const auto& trigger=ActionsService::triggerValue[1];
    const bool down=trigger.isActive && trigger.currentState>.7f;
    const bool active=ready && VisibleGameCursor() && !VrOptions::IsVisible() &&
        OpenXRService::xrFocused && ActionsService::handValid[1];
    if(!active) {ReleaseQuestClick();questPointer.Reset();lastPointer={-1,-1};return;}

    XrView view{XR_TYPE_VIEW};
    view.pose=ActionsService::handLocations[1].pose;
    const auto hand=fromOpenXR(view);
    if(!ValidRigid(hand)) {ReleaseQuestClick();questPointer.Reset();lastPointer={-1,-1};return;}
    const auto pointer=questPointer.Update(hand.z,down,scale.load());
    if(!pointer)return;
    RECT client{};
    if(!GetClientRect(window,&client) || client.right<=0 || client.bottom<=0)return;
    POINT local{static_cast<LONG>(pointer->x*client.right),static_cast<LONG>(pointer->y*client.bottom)};
    POINT screen=local;
    if(!ClientToScreen(window,&screen) || GetForegroundWindow()!=window)return;
    const bool moved=local.x!=lastPointer.x || local.y!=lastPointer.y;
    if(moved && !SetCursorPos(screen.x,screen.y)) {ReleaseQuestClick();return;}
    lastPointer=local;
    if(pointer->click && !pointerDown)
        pointerDown=PostMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(lastPointer.x,lastPointer.y))!=FALSE;
    if(!pointer->click)ReleaseQuestClick();
}
#endif
bool MenuInput::CanScaleMenu() { return ready && enabled && VisibleGameCursor() && (cursorReads.load()>0||mouseMessages.load()>0); }
bool MenuInput::PhysicalCursor(float* values) {
    values[0]=values[1]=-1.f;values[2]=values[3]=0;
    if(!ready || !VisibleGameCursor())return false;
    POINT point{};RECT bounds{};
    // Bypass our game-input inverse mapping: this is the actual displayed tip.
    if(!cursorHook.unsafe_call<BOOL>(&point) || !ScreenToClient(window,&point) || !GetClientRect(window,&bounds) || bounds.right<=0 || bounds.bottom<=0)return false;
    if(point.x<0 || point.y<0 || point.x>=bounds.right || point.y>=bounds.bottom)return false;
    values[0]=static_cast<float>(point.x)/bounds.right;values[1]=static_cast<float>(point.y)/bounds.bottom;
    values[2]=20.f/bounds.right;values[3]=28.f/bounds.bottom;return true;
}
float MenuInput::Scale() { return scale.load(); }
void MenuInput::SetVisualActive(bool value) { visual=value; }
void MenuInput::Report() {
    static auto last=std::chrono::steady_clock::now();auto now=std::chrono::steady_clock::now();
    if(now-last<std::chrono::seconds(5))return;last=now;
    if(cursorReads||mouseMessages)Logging::Log(std::format("[MENU_INPUT] cursor_reads={} mouse_messages={} mapped={} visual_active={}",cursorReads.load(),mouseMessages.load(),mapped.load(),visual.load()));
}
}
