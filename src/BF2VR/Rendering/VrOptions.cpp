#include <VrOptions.h>
#include <FrameBlitter.h>
#include <Runtime.h>
#include <MenuInput.h>
#include <BF2Service.h>
#include <OpenXRService.h>
#include <Logging.h>
#include <atomic>
#include <array>
#include <fstream>
#include <format>
#include <memory>

namespace BF2VR {
namespace {
struct Option { const wchar_t* label;const wchar_t* file;float low,high,step; };
constexpr std::array options{
    Option{L"World units per metre",L"WorldUnitsPerMeter.txt",.5f,2.f,.025f},
    Option{L"Menu scale",L"MenuScale.txt",.25f,1.f,.05f},
    Option{L"Projection render scale",L"XRProjectionScale.txt",1.f,Runtime::maxXrProjectionScale,.05f},
    Option{L"Eye sharpening",L"EyeSharpness.txt",0.f,.5f,.05f},
    Option{L"Gameplay HUD scale",L"HudScale.txt",.25f,1.f,.05f},
    Option{L"Turn speed",L"TurnSpeed.txt",100.f,2000.f,50.f},
    Option{L"Anchor crosshair and markers",nullptr,0.f,1.f,1.f},
    Option{L"Recenter headset",nullptr,0.f,0.f,0.f}
};
constexpr UINT width=VrOptions::Width,height=VrOptions::Height;
std::filesystem::path folder;
std::atomic_bool visible=false;
std::atomic_int selected=0,adjustment=0;
std::atomic_bool recenter=false;
std::unique_ptr<FrameBlitter> blitter;
Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
std::wstring lastText;

float Value(int row) {
    switch(row) {
    case 0:return Runtime::worldUnitsPerMeter;
    case 1:return MenuInput::Scale();
    case 2:return Runtime::xrProjectionScale;
    case 3:return Runtime::eyeSharpness;
    case 4:return Runtime::hudScale;
    case 5:return Runtime::questLookSpeed;
    case 6:return Runtime::hudAnchorsEnabled?1.f:0.f;
    default:return 0.f;
    }
}
bool RefreshTexture(ID3D11DeviceContext* context,int row) {
    std::array<std::wstring,options.size()> lines;
    std::wstring text;
    for(int i=0;i<static_cast<int>(options.size());++i) {
        auto value=i==5?std::format(L"{:.0f}",Value(i)):
            i<5?std::format(L"{:.3f}",Value(i)):
            i==6?(Value(i)>0?L"On":L"Off"):L"Enter";
        lines[i]=std::format(L"{}     {}",options[i].label,value);
        if(i==2)lines[i]+=std::format(L"  ({}x{})",OpenXRService::swapchainWidth,OpenXRService::swapchainHeight);
        text+=lines[i];
    }
    text+=std::to_wstring(row);
    if(text==lastText)return true;

    HDC dc=CreateCompatibleDC(nullptr);
    if(!dc)return false;
    BITMAPINFO info{};
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=width;
    info.bmiHeader.biHeight=-static_cast<LONG>(height);
    info.bmiHeader.biPlanes=1;
    info.bmiHeader.biBitCount=32;
    void* pixels=nullptr;
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    HFONT font=CreateFontW(-24,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    if(!bitmap || !font) {
        if(bitmap)DeleteObject(bitmap);
        if(font)DeleteObject(font);
        DeleteDC(dc);
        return false;
    }
    auto oldBitmap=SelectObject(dc,bitmap);
    auto oldFont=SelectObject(dc,font);
    RECT rect{0,0,width,height};
    auto brush=CreateSolidBrush(RGB(22,28,38));
    FillRect(dc,&rect,brush);DeleteObject(brush);
    rect={16,65+row*49,width-16,110+row*49};
    brush=CreateSolidBrush(RGB(43,64,92));
    FillRect(dc,&rect,brush);DeleteObject(brush);
    SetBkMode(dc,TRANSPARENT);
    SetTextColor(dc,RGB(244,246,255));
    TextOutW(dc,26,16,L"BF2VR OPTIONS",13);
    for(int i=0;i<static_cast<int>(options.size());++i)
        TextOutW(dc,28,71+i*49,lines[i].data(),static_cast<int>(lines[i].size()));
    constexpr wchar_t footer[]=L"Up / Down: select    Left / Right: change    F7: close";
    TextOutW(dc,25,476,footer,static_cast<int>(std::size(footer)-1));
    GdiFlush();
    auto* color=static_cast<uint32_t*>(pixels);
    for(UINT i=0;i<width*height;++i)color[i]|=0xff000000;
    context->UpdateSubresource(texture.Get(),0,nullptr,pixels,width*4,0);
    SelectObject(dc,oldFont);SelectObject(dc,oldBitmap);
    DeleteObject(font);DeleteObject(bitmap);DeleteDC(dc);
    lastText=std::move(text);
    return true;
}
}

void VrOptions::Initialize(const std::filesystem::path& directory) {folder=directory;}

bool VrOptions::Key(UINT message,WPARAM key,LPARAM flags) {
    if(!Runtime::enabled || Runtime::desktopAuditOnly || (message!=WM_KEYDOWN && message!=WM_KEYUP))return false;
    if(key==VK_F7) {
        if(message==WM_KEYDOWN && !(flags&(1ll<<30)))visible=!visible.load();
        return true;
    }
    if(!visible)return false;
    if(key!=VK_UP && key!=VK_DOWN && key!=VK_LEFT && key!=VK_RIGHT && key!=VK_RETURN)return false;
    if(message==WM_KEYUP)return true;
    const int row=selected;
    if(key==VK_UP || key==VK_DOWN)
        selected=(row+static_cast<int>(options.size())+(key==VK_UP?-1:1))%static_cast<int>(options.size());
    else if(key==VK_RETURN && row==7)recenter=true;
    else if((key==VK_LEFT || key==VK_RIGHT) && row<7)
        adjustment=(row+1)*(key==VK_LEFT?-1:1);
    return true;
}

bool VrOptions::IsVisible() {return visible;}

bool VrOptions::Update() {
    if(recenter.exchange(false)) {
        std::lock_guard lock(Runtime::mutex);
        BF2Service::ResetCameraAnchor(true);
    }
    const int change=adjustment.exchange(0);
    if(!change)return false;
    const int row=std::abs(change)-1;
    const auto& option=options[row];
    const float value=std::clamp(Value(row)+(change>0?option.step:-option.step),option.low,option.high);
    if(row==6) {
        const auto path=folder/L"EnableHudAnchors.flag";
        if(value>0) {
            std::ofstream out(path,std::ios::app);
            if(!out)Logging::Log("[VR OPTIONS] Could not enable anchors");
        }
        else {
            std::error_code error;
            std::filesystem::remove(path,error);
            if(error)Logging::Log("[VR OPTIONS] Could not remove anchor setting");
        }
        return true;
    }
    std::ofstream out(folder/option.file);
    if(!(out<<std::format("{:.3f}\n",value)))Logging::Log("[VR OPTIONS] Could not save setting");
    return true;
}

bool VrOptions::Draw(ID3D11Device* device,ID3D11DeviceContext* context,
                    ID3D11RenderTargetView* target,UINT outputWidth,UINT outputHeight) {
    if(!visible || !device || !context || !target)return false;
    try {
    if(!blitter) {
        auto pending=std::make_unique<FrameBlitter>();
        if(FAILED(pending->Initialize(device,context))) {visible=false;return false;}
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=width;desc.Height=height;desc.ArraySize=desc.MipLevels=desc.SampleDesc.Count=1;
        desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        if(FAILED(device->CreateTexture2D(&desc,nullptr,&texture))) {visible=false;return false;}
        blitter=std::move(pending);
    }
    return RefreshTexture(context,selected) && SUCCEEDED(blitter->Copy(texture.Get(),
        DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,target,outputWidth,outputHeight,0.f));
    } catch(const std::exception&) {
        visible=false;
        Logging::Log("[VR OPTIONS] Overlay stopped after a rendering error");
        return false;
    }
}
void VrOptions::Shutdown() {
    visible=false;adjustment=0;recenter=false;
    blitter.reset();texture.Reset();lastText.clear();
}
}
