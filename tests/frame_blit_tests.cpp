#include <FrameBlitter.h>
#include <EyeReadback.h>
#include <DrawAudit.h>
#include <HudTransform.h>
#include <HudMenuGuard.h>
#include <MenuCoordinates.h>
#include <MenuShader.h>
#include <HudStartup.h>
#include <MarkerGeometryAudit.h>
#include <UiAnchorTransform.h>
#include <array>
#include <algorithm>
#include <vector>
#include <iostream>
#include <stdexcept>
namespace BF2VR { void Logging::Log(const std::string& message, int) { std::cout << message << "\n"; } }
using Microsoft::WRL::ComPtr;
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
int main() {
    try {
        D3D11_DEPTH_STENCIL_DESC depthState{};
        require(BF2VR::HudOverlayDepthIndependent(false,nullptr) &&
                BF2VR::HudOverlayDepthIndependent(true,&depthState),
                "HUD overlay accepts an attached but inactive depth view");
        depthState.DepthEnable=TRUE;
        require(!BF2VR::HudOverlayDepthIndependent(true,&depthState) &&
                !BF2VR::HudOverlayDepthIndependent(true,nullptr),
                "HUD overlay rejects active and default depth testing");
        depthState.DepthEnable=FALSE;depthState.StencilEnable=TRUE;
        require(!BF2VR::HudOverlayDepthIndependent(true,&depthState),
                "HUD overlay rejects active stencil testing");
        BF2VR::HudStartupSelection startup;
        require(!BF2VR::HudStartupSelection::NeedsRediscovery(10,9,9) &&
                !BF2VR::HudStartupSelection::NeedsRediscovery(10,8,8) &&
                BF2VR::HudStartupSelection::NeedsRediscovery(10,7,9) &&
                BF2VR::HudStartupSelection::NeedsRediscovery(10,9,7),
                "HUD discovery tolerates short gaps and retries after producer or binding loss");
        require(BF2VR::HudStartupSelection::Matches(29,3440,1440,3440,1440), "native full-resolution sRGB HUD eligible");
        require(!BF2VR::HudStartupSelection::Matches(29,1720,720,3440,1440) &&
                !BF2VR::HudStartupSelection::Matches(28,3440,1440,3440,1440), "particle and final colour targets rejected");
        require(!startup.Confirm(10) && !startup.Confirm(10) && startup.Confirm(10), "startup requires three complete frames");
        require(!startup.Confirm(0) && !startup.Confirm(10) && !startup.Confirm(10), "ambiguous or missing UI resets confirmation");
        require(!startup.Confirm(20) && !startup.Confirm(20) && startup.Confirm(20), "replacement target must independently settle");
        for(double factor:{.25,.5,.75,1.0}) {
            const double nativeX=720,nativeY=610,width=3440,height=1440;
            BF2VR::MenuPoint display{width*.5+factor*(nativeX-width*.5),height*.5+factor*(nativeY-height*.5)};
            const auto source=BF2VR::MenuPointToNative(display,width,height,factor);
            require(source && std::abs(source->x-nativeX)<1e-6 && std::abs(source->y-nativeY)<1e-6,
                    "menu pointer inverse matches rendered button at every supported trial scale");
        }
        require(!BF2VR::MenuPointToNative({0,0},3440,1440,.5), "outside shrunken menu cannot activate native buttons");
        require(!BF2VR::MenuPointToNative({1,1},3440,1440,0), "invalid menu scale rejected");
        BF2VR::HudMenuGuard menu;
        require(!menu.Observe(false,true,false), "unknown background state bypasses HUD scaling");
        require(menu.Observe(true,true,false), "focused hidden gameplay cursor allows scaling");
        require(!menu.Observe(true,true,true), "visible menu cursor preserves native visuals and hit boxes");
        require(!menu.Observe(false,true,false), "another app cursor cannot re-enable paused UI scaling");
        require(!menu.Observe(true,false,false), "cursor query error bypasses scaling");
        require(menu.Observe(true,true,false), "return to gameplay re-enables HUD scaling");
        ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
        require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
            &device, nullptr, &context)), "software D3D11 device");
        BF2VR::FrameBlitter blitter;
        require(SUCCEEDED(blitter.Initialize(device.Get(), context.Get())), "isolated blitter setup");
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = 32; desc.Height = 16; desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1; desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> output; ComPtr<ID3D11RenderTargetView> target;
        require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &output)), "output texture");
        require(SUCCEEDED(device->CreateRenderTargetView(output.Get(), nullptr, &target)), "output view");
        for (UINT size : {16u,8u}) {
            desc.Width = size; desc.Height = size/2; desc.BindFlags = 0;
            std::vector<unsigned> pixels(size * size/2);
            for (UINT y = 0; y < size/2; ++y) for (UINT x = 0; x < size; ++x)
                pixels[y*size+x] = x < size/2 ? 0xff0000ff : 0xff00ff00;
            D3D11_SUBRESOURCE_DATA data{pixels.data(), size*4, 0};
            ComPtr<ID3D11Texture2D> input;
            require(SUCCEEDED(device->CreateTexture2D(&desc, &data, &input)), "input without SRV binding");
            D3D11_VIEWPORT original{1,2,3,4,0,1}; context->RSSetViewports(1, &original);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
            auto* rtv = target.Get(); context->OMSetRenderTargets(1, &rtv, nullptr);
            require(SUCCEEDED(blitter.Copy(input.Get(), desc.Format, target.Get(), 32, 16)), "scaled frame copy");
            BF2VR::EyePixels diagnostic;
            require(SUCCEEDED(BF2VR::ReadEyePixels(device.Get(),context.Get(),target.Get(),diagnostic)), "diagnostic texture readback");
            require(diagnostic.width==32 && diagnostic.height==16 && diagnostic.rgb.size()==32*16*3 && diagnostic.alpha.size()==32*16,
                    "readback dimensions and packed row size");
            const size_t red=(8*32+4)*3, green=(8*32+27)*3;
            require(diagnostic.rgb[red]==255 && diagnostic.rgb[red+1]==0 && diagnostic.rgb[red+2]==0 &&
                    diagnostic.rgb[green]==0 && diagnostic.rgb[green+1]==255 && diagnostic.rgb[green+2]==0 && diagnostic.alpha[8*32+4]==255 && diagnostic.alpha[8*32+27]==255,
                    "diagnostic preserves pixel colours and row pitches");
            const auto audit=BF2VR::SnapshotDraw(context.Get(),1,6,2);
            require(audit.kind==1 && audit.count==6 && audit.instances==2 && audit.width==32 && audit.height==16 &&
                    audit.viewport.TopLeftX==1 && audit.viewport.TopLeftY==2 && audit.viewport.Width==3 && audit.viewport.Height==4,
                    "draw audit captures actual target and viewport without rebinding");
            UINT count = 1; D3D11_VIEWPORT restored{}; context->RSGetViewports(&count, &restored);
            require(count == 1 && restored.TopLeftX == 1 && restored.TopLeftY == 2 && restored.Width == 3 && restored.Height == 4, "viewport restored");
            D3D11_PRIMITIVE_TOPOLOGY topology{}; context->IAGetPrimitiveTopology(&topology);
            require(topology == D3D11_PRIMITIVE_TOPOLOGY_LINELIST, "topology restored");
            ComPtr<ID3D11RenderTargetView> restoredTarget; context->OMGetRenderTargets(1, &restoredTarget, nullptr);
            require(restoredTarget.Get() == target.Get(), "render target restored");
            context->OMSetRenderTargets(0, nullptr, nullptr);
            D3D11_TEXTURE2D_DESC stagingDesc{}; output->GetDesc(&stagingDesc);
            stagingDesc.Usage = D3D11_USAGE_STAGING; stagingDesc.BindFlags = 0; stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> staging;
            require(SUCCEEDED(device->CreateTexture2D(&stagingDesc, nullptr, &staging)), "readback texture");
            context->CopyResource(staging.Get(), output.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            require(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "GPU readback");
            auto* row = reinterpret_cast<const unsigned*>(static_cast<const char*>(mapped.pData) + 8*mapped.RowPitch);
            const bool correct = row[4] == 0xff0000ff && row[27] == 0xff00ff00;
            context->Unmap(staging.Get(), 0);
            require(correct, "left/right pixels preserved while scaling and resizing source");
        }
        D3D11_TEXTURE2D_DESC sharpDesc=desc;
        sharpDesc.Width=8;sharpDesc.Height=4;sharpDesc.BindFlags=0;
        std::array<unsigned,32> sharpPixels{};
        for(unsigned i=0;i<sharpPixels.size();++i)
            sharpPixels[i]=i%8<4?0x80606060:0x80a0a0a0;
        D3D11_SUBRESOURCE_DATA sharpData{sharpPixels.data(),32,0};
        ComPtr<ID3D11Texture2D> sharpInput;
        require(SUCCEEDED(device->CreateTexture2D(&sharpDesc,&sharpData,&sharpInput)), "eye sharpening fixture");
        auto copyEye=[&] {
            require(SUCCEEDED(blitter.Copy(sharpInput.Get(),sharpDesc.Format,target.Get(),32,16)), "eye image scaling");
            BF2VR::EyePixels pixels;
            require(SUCCEEDED(BF2VR::ReadEyePixels(device.Get(),context.Get(),target.Get(),pixels)), "eye image readback");
            return pixels;
        };
        const auto linearEye=copyEye();
        require(SUCCEEDED(blitter.SetEyeSharpness(.35f)), "eye sharpening shader setup");
        const auto sharpEye=copyEye();
        require(sharpEye.rgb[(8*32+15)*3]<linearEye.rgb[(8*32+15)*3] &&
                sharpEye.rgb[(8*32+16)*3]>linearEye.rgb[(8*32+16)*3] &&
                sharpEye.alpha==linearEye.alpha,
                "edge sharpening raises local contrast and preserves alpha");
        require(*std::min_element(sharpEye.rgb.begin(),sharpEye.rgb.end())>=96 &&
                *std::max_element(sharpEye.rgb.begin(),sharpEye.rgb.end())<=160,
                "eye sharpening does not overshoot the source neighborhood");
        require(FAILED(blitter.SetEyeSharpness(.51f)) && SUCCEEDED(blitter.SetEyeSharpness(0.f)),
                "bounded sharpening and instant return to baseline");
        require(copyEye().rgb==linearEye.rgb, "zero sharpening restores the original linear copy");
        D3D11_TEXTURE2D_DESC hdrDesc{};hdrDesc.Width=2;hdrDesc.Height=1;
        hdrDesc.MipLevels=hdrDesc.ArraySize=1;hdrDesc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;hdrDesc.SampleDesc.Count=1;
        const std::array<unsigned short,8> hdrPixels{0x3c00,0,0,0x3800,0,0x3c00,0,0};
        D3D11_SUBRESOURCE_DATA hdrData{hdrPixels.data(),16,0};
        ComPtr<ID3D11Texture2D> hdr;
        require(SUCCEEDED(device->CreateTexture2D(&hdrDesc,&hdrData,&hdr)), "HDR HUD sample texture");
        require(SUCCEEDED(blitter.Copy(hdr.Get(),hdrDesc.Format,target.Get(),32,16)), "HDR to RGBA preview conversion");
        BF2VR::EyePixels hdrReadback;
        require(SUCCEEDED(BF2VR::ReadEyePixels(device.Get(),context.Get(),target.Get(),hdrReadback)), "converted HDR alpha readback");
        require(hdrReadback.rgb[(8*32+4)*3]==255 && hdrReadback.alpha[8*32+4]==128 &&
                hdrReadback.rgb[(8*32+27)*3+1]==255 && hdrReadback.alpha[8*32+27]==0,
                "native HUD-format conversion preserves fractional and zero alpha");
        D3D11_TEXTURE2D_DESC hudDesc{};hudDesc.Width=32;hudDesc.Height=16;hudDesc.MipLevels=hudDesc.ArraySize=1;
        hudDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;hudDesc.SampleDesc.Count=1;hudDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        std::vector<unsigned> hudPixels(32*16,0x800000ff);D3D11_SUBRESOURCE_DATA hudData{hudPixels.data(),32*4,0};
        ComPtr<ID3D11Texture2D> hudTexture;ComPtr<ID3D11ShaderResourceView> hudSource;
        require(SUCCEEDED(device->CreateTexture2D(&hudDesc,&hudData,&hudTexture)) &&
                SUCCEEDED(device->CreateShaderResourceView(hudTexture.Get(),nullptr,&hudSource)), "HUD transform fixture");
        BF2VR::HudTransform hud;
        require(SUCCEEDED(hud.Initialize(device.Get(),context.Get())), "HUD GPU transform initialized");
        const std::string identity=R"(Texture2D image:register(t0);SamplerState linearClamp:register(s0);
struct Vertex {float4 position:SV_Position;float2 uv:TEXCOORD0;};
float4 PS(Vertex v):SV_Target{return image.Sample(linearClamp,v.uv);})";
        std::string shaderErrors;
        require(SUCCEEDED(hud.SetShader(identity,shaderErrors)), "HUD identity shader compiles");
        ID3D11ShaderResourceView* processed=nullptr;
        auto readProcessed=[&]() {
            require(SUCCEEDED(hud.Apply(hudSource.Get(),&processed)) && processed!=hudSource.Get(), "selected HUD copied into separate texture");
            ComPtr<ID3D11Resource> resource;processed->GetResource(&resource);
            ComPtr<ID3D11RenderTargetView> processedTarget;
            require(SUCCEEDED(device->CreateRenderTargetView(resource.Get(),nullptr,&processedTarget)), "processed HUD readback view");
            BF2VR::EyePixels pixels;require(SUCCEEDED(BF2VR::ReadEyePixels(device.Get(),context.Get(),processedTarget.Get(),pixels)), "processed HUD readback");
            return pixels;
        };
        auto identityPixels=readProcessed();
        require(identityPixels.rgb[0]==255 && identityPixels.alpha[0]==128, "HUD pass-through preserves colour and alpha");
        require(FAILED(hud.SetShader("invalid shader",shaderErrors)), "bad HUD reload rejected");
        auto retainedPixels=readProcessed();require(retainedPixels.rgb==identityPixels.rgb && retainedPixels.alpha==identityPixels.alpha,
                "failed shader reload preserves previous working shader");
        const std::string compact=R"(Texture2D image:register(t0);SamplerState linearClamp:register(s0);
struct Vertex {float4 position:SV_Position;float2 uv:TEXCOORD0;};
float4 PS(Vertex v):SV_Target {float2 p=(v.uv-.5)/.5+.5;if(any(p<0)||any(p>1))return 0;return image.Sample(linearClamp,p);})";
        require(SUCCEEDED(hud.SetShader(compact,shaderErrors)), "compact HUD shader compiles");
        auto compactPixels=readProcessed();require(compactPixels.alpha[0]==0 && compactPixels.alpha[8*32+16]==128,
                "compact HUD has transparent outer area and preserved centre alpha");
        std::vector<unsigned> anchorPixels(32*16);
        anchorPixels[4*32+29]=0x80008000;
        D3D11_SUBRESOURCE_DATA anchorData{anchorPixels.data(),32*4,0};
        ComPtr<ID3D11Texture2D> anchorTexture;
        ComPtr<ID3D11ShaderResourceView> anchorView;
        require(SUCCEEDED(device->CreateTexture2D(&hudDesc,&anchorData,&anchorTexture)) &&
                SUCCEEDED(device->CreateShaderResourceView(anchorTexture.Get(),nullptr,&anchorView)),
                "final-position anchor overlay fixture");
        require(SUCCEEDED(hud.Apply(hudSource.Get(),&processed,anchorView.Get())),
                "compose fixed HUD and final-position anchors");
        BF2VR::EyePixels combined;
        const size_t markerPixel=4*32+29, statusPixel=8*32+16;
        require(SUCCEEDED(BF2VR::ReadEyePixels(device.Get(),context.Get(),hud.Target(),combined)) &&
                combined.rgb[markerPixel*3+1]==128 && combined.alpha[markerPixel]==128 &&
                combined.rgb[statusPixel*3]==255 && combined.alpha[statusPixel]==128 && combined.alpha[0]==0,
                "anchored icon stays outside the reduced HUD while native status color and alpha remain intact");
        BF2VR::EyePixels untouchedWorld;
        require(SUCCEEDED(BF2VR::ReadEyePixels(device.Get(),context.Get(),target.Get(),untouchedWorld)) &&
                untouchedWorld.rgb==hdrReadback.rgb && untouchedWorld.alpha==hdrReadback.alpha,
                "HUD transform leaves existing world render target pixels unchanged");
        // Reproduce producer/consumer ordering: binding can precede UI rendering.
        D3D11_TEXTURE2D_DESC lateDesc=hudDesc;lateDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> lateTexture;ComPtr<ID3D11ShaderResourceView> lateSource;ComPtr<ID3D11RenderTargetView> lateTarget;
        require(SUCCEEDED(device->CreateTexture2D(&lateDesc,nullptr,&lateTexture)) &&
                SUCCEEDED(device->CreateShaderResourceView(lateTexture.Get(),nullptr,&lateSource)) &&
                SUCCEEDED(device->CreateRenderTargetView(lateTexture.Get(),nullptr,&lateTarget)), "late-produced UI texture");
        require(SUCCEEDED(hud.SetShader(identity,shaderErrors)), "identity for production-order test");
        const float empty[4]={0,0,0,0};context->ClearRenderTargetView(lateTarget.Get(),empty);
        auto* earlyBinding=lateSource.Get();context->PSSetShaderResources(1,1,&earlyBinding);
        require(SUCCEEDED(hud.Apply(lateSource.Get(),&processed)), "old early binding-time copy");
        BF2VR::EyePixels earlyPixels;
        require(SUCCEEDED(BF2VR::ReadEyePixels(device.Get(),context.Get(),hud.Target(),earlyPixels)) && earlyPixels.alpha[0]==0,
                "early copy demonstrably contains empty transparent UI");
        const float produced[4]={1,0,0,.5f};context->ClearRenderTargetView(lateTarget.Get(),produced);
        auto* worldTarget=target.Get();context->OMSetRenderTargets(1,&worldTarget,nullptr);
        {
            BF2VR::HudDrawBinding drawBinding;
            require(drawBinding.Prepare(context.Get(),hud,lateTexture.Get(),1)==S_OK, "transform prepared at consuming draw");
            ComPtr<ID3D11ShaderResourceView> bound;context->PSGetShaderResources(1,1,&bound);
            require(bound.Get()!=lateSource.Get(), "replacement bound only during consumer scope");
            BF2VR::EyePixels latePixels;
            require(SUCCEEDED(BF2VR::ReadEyePixels(device.Get(),context.Get(),hud.Target(),latePixels)) &&
                    latePixels.rgb[0]==255 && latePixels.alpha[0]==128,
                    "draw-time processing sees UI populated after binding");
        }
        ComPtr<ID3D11ShaderResourceView> restoredHud;context->PSGetShaderResources(1,1,&restoredHud);
        require(restoredHud.Get()==lateSource.Get(), "native shader resource restored after consuming draw");
        {
            BF2VR::HudDrawBinding unrelated;
            require(unrelated.Prepare(context.Get(),hud,lateTexture.Get(),2)==S_FALSE, "unrelated shader slot stays unchanged");
        }
        auto* producerTarget=lateTarget.Get();context->OMSetRenderTargets(1,&producerTarget,nullptr);
        {
            BF2VR::HudDrawBinding producerBinding;
            require(producerBinding.Prepare(context.Get(),hud,lateTexture.Get(),1)==S_FALSE, "producer pass is not redirected");
        }
        context->OMSetRenderTargets(0,nullptr,nullptr);
        context->ClearRenderTargetView(lateTarget.Get(),empty);
        require(SUCCEEDED(hud.SetShader(BF2VR::BuildMenuShader(.25f),shaderErrors)), "production menu/cursor shader compiles");
        auto readCursor=[&]() {
            require(SUCCEEDED(hud.Apply(lateSource.Get(),&processed)), "menu cursor composite");
            BF2VR::EyePixels pixels;require(SUCCEEDED(BF2VR::ReadEyePixels(device.Get(),context.Get(),hud.Target(),pixels)), "cursor pixel readback");return pixels;
        };
        const std::array<float,4> hiddenPointer{-1,-1,0,0};
        require(SUCCEEDED(hud.SetCursor(hiddenPointer.data())), "hide software cursor constants");
        auto noCursor=readCursor();
        require(std::all_of(noCursor.alpha.begin(),noCursor.alpha.end(),[](auto a){return a==0;}), "hidden cursor leaves transparent UI unchanged");
        D3D11_BUFFER_DESC cbDesc{};cbDesc.ByteWidth=16;cbDesc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        ComPtr<ID3D11Buffer> nativeConstants;require(SUCCEEDED(device->CreateBuffer(&cbDesc,nullptr,&nativeConstants)), "native constant buffer fixture");
        auto* nativeBuffer=nativeConstants.Get();context->PSSetConstantBuffers(0,1,&nativeBuffer);
        const std::array<float,4> visiblePointer{.125f,.125f,.25f,.5f};
        require(SUCCEEDED(hud.SetCursor(visiblePointer.data())), "physical pointer upload");
        auto withCursor=readCursor();
        const size_t black=(3*32+4),white=(6*32+5);
        require(withCursor.alpha[black]==255 && withCursor.rgb[black*3]==0 &&
                withCursor.alpha[white]==255 && withCursor.rgb[white*3]==255 && withCursor.rgb[white*3+1]==255 &&
                withCursor.alpha[0]==0, "cursor outline/fill visible with transparent surroundings, including outside scaled panel");
        ComPtr<ID3D11Buffer> restoredConstants;context->PSGetConstantBuffers(0,1,&restoredConstants);
        require(restoredConstants.Get()==nativeConstants.Get(), "software cursor preserves native game pixel constants");
        const std::array<unsigned,4> markerFixture{7,19,42,99};
        D3D11_BUFFER_DESC markerDesc{};markerDesc.ByteWidth=sizeof(markerFixture);markerDesc.Usage=D3D11_USAGE_DEFAULT;markerDesc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA markerInitial{};markerInitial.pSysMem=markerFixture.data();ComPtr<ID3D11Buffer> markerBuffer;
        require(SUCCEEDED(device->CreateBuffer(&markerDesc,&markerInitial,&markerBuffer)), "marker buffer fixture");
        ID3D11Buffer* markerBound=markerBuffer.Get();UINT markerStride=4,markerOffset=0;context->IASetVertexBuffers(0,1,&markerBound,&markerStride,&markerOffset);
        std::vector<unsigned char> markerBytes;
        require(SUCCEEDED(BF2VR::MarkerGeometryAudit::ReadBuffer(device.Get(),context.Get(),markerBuffer.Get(),markerBytes)) && markerBytes.size()==sizeof(markerFixture) && !memcmp(markerBytes.data(),markerFixture.data(),sizeof(markerFixture)), "bounded geometry readback preserves native data");
        ComPtr<ID3D11Buffer> markerRestored;context->IAGetVertexBuffers(0,1,&markerRestored,&markerStride,&markerOffset);
        require(markerRestored.Get()==markerBuffer.Get() && markerStride==4 && markerOffset==0, "geometry inspection leaves native vertex bindings unchanged");
        require(FAILED(BF2VR::MarkerGeometryAudit::ReadBuffer(device.Get(),context.Get(),nullptr,markerBytes)) && markerBytes.empty(), "missing diagnostic buffer rejected without stale data");
        D3D11_TEXTURE2D_DESC markerTargetDesc{};markerTargetDesc.Width=640;markerTargetDesc.Height=480;markerTargetDesc.MipLevels=markerTargetDesc.ArraySize=1;
        markerTargetDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;markerTargetDesc.SampleDesc.Count=1;markerTargetDesc.BindFlags=D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> markerTarget;ComPtr<ID3D11RenderTargetView> markerRTV;
        require(SUCCEEDED(device->CreateTexture2D(&markerTargetDesc,nullptr,&markerTarget)) && SUCCEEDED(device->CreateRenderTargetView(markerTarget.Get(),nullptr,&markerRTV)), "marker capture UI target fixture");
        auto* markerRT=markerRTV.Get();context->OMSetRenderTargets(1,&markerRT,nullptr);
        D3D11_DEPTH_STENCIL_DESC markerDepthDesc{};markerDepthDesc.DepthEnable=FALSE;ComPtr<ID3D11DepthStencilState> markerDepth;
        require(SUCCEEDED(device->CreateDepthStencilState(&markerDepthDesc,&markerDepth)), "marker depth fixture");context->OMSetDepthStencilState(markerDepth.Get(),0);
        D3D11_BLEND_DESC markerBlendDesc{};auto& markerBlendRT=markerBlendDesc.RenderTarget[0];markerBlendRT.BlendEnable=TRUE;
        markerBlendRT.SrcBlend=markerBlendRT.SrcBlendAlpha=D3D11_BLEND_ONE;markerBlendRT.DestBlend=markerBlendRT.DestBlendAlpha=D3D11_BLEND_ZERO;
        markerBlendRT.BlendOp=markerBlendRT.BlendOpAlpha=D3D11_BLEND_OP_ADD;markerBlendRT.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        ComPtr<ID3D11BlendState> markerBlend;require(SUCCEEDED(device->CreateBlendState(&markerBlendDesc,&markerBlend)), "marker blend fixture");context->OMSetBlendState(markerBlend.Get(),nullptr,~0u);
        const auto markerFolder=std::filesystem::temp_directory_path()/("BF2VR-GeometryRegression-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
        BF2VR::MarkerGeometryAudit::Request(markerFolder);BF2VR::MarkerGeometryAudit::Present(device.Get(),context.Get());
        BF2VR::MarkerGeometryAudit::Draw(context.Get(),1,6,1,12,-3,0);
        const std::array<unsigned,4> laterMarkerData{100,200,300,400};context->UpdateSubresource(markerBuffer.Get(),0,nullptr,laterMarkerData.data(),0,0);context->Flush();
        for(unsigned wait=0;wait<100 && !std::filesystem::exists(markerFolder/"draws.csv");++wait) { BF2VR::MarkerGeometryAudit::Present(device.Get(),context.Get());Sleep(1); }
        std::ifstream markerCSV(markerFolder/"draws.csv");std::string markerHeader,markerRow;std::getline(markerCSV,markerHeader);std::getline(markerCSV,markerRow);markerCSV.close();
        require(markerRow.starts_with("0,1,6,1,12,-3,0,") && std::count(markerRow.begin(),markerRow.end(),'"')==4 && markerHeader.find("vsCBs,psTextures")!=std::string::npos, "one-frame geometry capture retains draw offsets and valid quoted metadata columns");
        auto readCapturedVertex=[&](const std::filesystem::path& destination,const std::string& row) {
            std::istringstream columns(row);std::string token;for(unsigned i=0;i<=12;++i)std::getline(columns,token,',');
            const int id=std::stoi(token);std::ifstream file(destination/("buffer-"+std::to_string(id)+".bin"),std::ios::binary);
            std::vector<char> data{std::istreambuf_iterator<char>(file),{}};
            require(data.size()==sizeof(markerFixture) && !memcmp(data.data(),markerFixture.data(),sizeof(markerFixture)),"draw-time snapshot retains bytes before later buffer overwrite");
        };
        readCapturedVertex(markerFolder,markerRow);
        context->IAGetVertexBuffers(0,1,&markerRestored,&markerStride,&markerOffset);
        require(markerRestored.Get()==markerBuffer.Get(), "complete geometry capture preserves vertex binding");
        for(const auto& entry:std::filesystem::directory_iterator(markerFolder))if(entry.is_regular_file())std::filesystem::remove(entry.path());
        std::filesystem::remove(markerFolder);
        ComPtr<ID3D11DeviceContext> markerDeferred;require(SUCCEEDED(device->CreateDeferredContext(0,&markerDeferred)),"deferred context fixture");
        context->UpdateSubresource(markerBuffer.Get(),0,nullptr,markerFixture.data(),0,0);
        markerDeferred->OMSetRenderTargets(1,&markerRT,nullptr);markerDeferred->OMSetDepthStencilState(markerDepth.Get(),0);markerDeferred->OMSetBlendState(markerBlend.Get(),nullptr,~0u);
        markerDeferred->IASetVertexBuffers(0,1,&markerBound,&markerStride,&markerOffset);
        const auto deferredFolder=std::filesystem::path(markerFolder.string()+"-deferred");
        BF2VR::MarkerGeometryAudit::Request(deferredFolder);BF2VR::MarkerGeometryAudit::Present(device.Get(),context.Get());
        BF2VR::MarkerGeometryAudit::Draw(markerDeferred.Get(),1,6,1,0,0,0);
        ComPtr<ID3D11CommandList> markerCommands;require(SUCCEEDED(markerDeferred->FinishCommandList(FALSE,&markerCommands)),"deferred capture commands finish");
        context->ExecuteCommandList(markerCommands.Get(),TRUE);
        context->UpdateSubresource(markerBuffer.Get(),0,nullptr,laterMarkerData.data(),0,0);context->Flush();
        for(unsigned wait=0;wait<100 && !std::filesystem::exists(deferredFolder/"draws.csv");++wait) { BF2VR::MarkerGeometryAudit::Present(device.Get(),context.Get());Sleep(1); }
        std::ifstream deferredCSV(deferredFolder/"draws.csv");std::getline(deferredCSV,markerHeader);std::getline(deferredCSV,markerRow);deferredCSV.close();
        require(!markerRow.empty(),"deferred HUD geometry observed through its own pipeline state");
        readCapturedVertex(deferredFolder,markerRow);
        std::ifstream deferredContexts(deferredFolder/"contexts.csv");std::string deferredContextData{std::istreambuf_iterator<char>(deferredContexts),{}};deferredContexts.close();
        require(deferredContextData.find(",1,1,1,1")!=std::string::npos,"deferred context/device attribution retained");
        for(const auto& entry:std::filesystem::directory_iterator(deferredFolder))if(entry.is_regular_file())std::filesystem::remove(entry.path());
        std::filesystem::remove(deferredFolder);



        BF2VR::UiAnchorTransform placement;std::string anchorErrors;
        auto anchorHr=placement.Initialize(device.Get(),context.Get(),anchorErrors);if(FAILED(anchorHr))std::cerr<<anchorErrors<<'\n';
        require(SUCCEEDED(anchorHr),"native UI anchor compute shader compiles");
        std::array<float,256> nativeUI{};for(unsigned i=0;i<nativeUI.size();++i)nativeUI[i]=static_cast<float>(i)*.125f;
        nativeUI[4]=.0775193721f;nativeUI[5]=nativeUI[6]=0;nativeUI[7]=-100;
        nativeUI[8]=0;nativeUI[9]=-.185185179f;nativeUI[10]=0;nativeUI[11]=100;
        nativeUI[12]=nativeUI[13]=0;nativeUI[14]=.0337f;nativeUI[15]=99.006f;
        nativeUI[16]=nativeUI[17]=0;nativeUI[18]=.0337f;nativeUI[19]=100;
        D3D11_BUFFER_DESC uiDesc{};uiDesc.ByteWidth=sizeof(nativeUI);uiDesc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA uiInitial{};uiInitial.pSysMem=nativeUI.data();ComPtr<ID3D11Buffer> nativeUIBuffer;
        require(SUCCEEDED(device->CreateBuffer(&uiDesc,&uiInitial,&nativeUIBuffer)),"native UI constant fixture");
        std::array<float,56> uiVertices{};D3D11_BUFFER_DESC uiVBDesc{};uiVBDesc.ByteWidth=sizeof(uiVertices);uiVBDesc.BindFlags=D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA uiVBInitial{};uiVBInitial.pSysMem=uiVertices.data();ComPtr<ID3D11Buffer> uiVerticesBuffer;
        require(SUCCEEDED(device->CreateBuffer(&uiVBDesc,&uiVBInitial,&uiVerticesBuffer)),"native UI vertex fixture");
        const std::array<unsigned short,4> uiIndices{3,0,1,2};D3D11_BUFFER_DESC uiIBDesc{};uiIBDesc.ByteWidth=sizeof(uiIndices);uiIBDesc.BindFlags=D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA uiIBInitial{};uiIBInitial.pSysMem=uiIndices.data();ComPtr<ID3D11Buffer> uiIndexBuffer;
        require(SUCCEEDED(device->CreateBuffer(&uiIBDesc,&uiIBInitial,&uiIndexBuffer)),"native UI indexed fixture");
        ComPtr<ID3D11DeviceContext1> uiContext;require(SUCCEEDED(context.As(&uiContext)),"context1 for offset-preservation fixture");
        auto* originalUI=nativeUIBuffer.Get();UINT uiFirst=0,uiCount=64;uiContext->VSSetConstantBuffers1(0,1,&originalUI,&uiFirst,&uiCount);
        context->CSSetConstantBuffers(0,1,&nativeBuffer);
        auto reducedNative=[&] {
            auto reduced=nativeUI;
            for(unsigned i=4;i<12;++i)reduced[i]*=.25f;
            return reduced;
        };
        auto evaluatePlacement=[&](float x,float y,BF2VR::UiAnchorTransform::Parameters options,UINT stride=56) {
            uiVertices.fill(0);uiVertices[0]=x;uiVertices[1]=y;
            context->UpdateSubresource(uiVerticesBuffer.Get(),0,nullptr,uiVertices.data(),0,0);
            context->UpdateSubresource(nativeUIBuffer.Get(),0,nullptr,nativeUI.data(),0,0);
            options.stride=stride;options.indexed=1;options.indexBits=16;options.indexOffset=2;
            placement.NextFrame();
            require(SUCCEEDED(placement.Prepare(nativeUIBuffer.Get(),0,64,uiVerticesBuffer.Get(),uiIndexBuffer.Get(),options)),"GPU placement preparation");
            std::vector<unsigned char> changedUI;require(SUCCEEDED(BF2VR::MarkerGeometryAudit::ReadBuffer(device.Get(),context.Get(),placement.Constant(),changedUI)) && changedUI.size()==2*sizeof(nativeUI),"test-only anchor output readback");
            std::array<float,256> result{};memcpy(result.data(),changedUI.data(),sizeof(result));
            ComPtr<ID3D11Buffer> keptVS,keptCS;UINT keptFirst=0,keptCount=0;uiContext->VSGetConstantBuffers1(0,1,&keptVS,&keptFirst,&keptCount);context->CSGetConstantBuffers(0,1,&keptCS);
            require(keptVS.Get()==nativeUIBuffer.Get() && keptFirst==0 && keptCount==64 && keptCS.Get()==nativeConstants.Get(),"GPU marker pass preserves native vertex and compute bindings");
            return result;
        };
        BF2VR::UiAnchorTransform::Parameters anchorOptions;anchorOptions.aimX=.65f;anchorOptions.aimY=-.45f;
        auto splitOptions=anchorOptions;
        splitOptions.splitLayers=1;
        auto splitMarker=evaluatePlacement(32,32,splitOptions);
        auto readNativeLayer=[&] {
            std::vector<unsigned char> data;
            require(SUCCEEDED(BF2VR::MarkerGeometryAudit::ReadBuffer(device.Get(),context.Get(),placement.Constant(),data)) &&
                    data.size()==2*sizeof(nativeUI),"read both HUD layer matrices");
            std::array<float,256> nativeLayer{};
            memcpy(nativeLayer.data(),data.data()+sizeof(nativeLayer),sizeof(nativeLayer));
            return nativeLayer;
        };
        require(splitMarker[7]==nativeUI[7] && readNativeLayer()[7]==8*nativeUI[19],
                "split marker is drawn only into the anchor overlay");
        auto splitFixed=evaluatePlacement(500,500,splitOptions);
        require(splitFixed[7]==8*nativeUI[19] && readNativeLayer()==nativeUI,
                "split fixed widgets keep their original matrices only on the native layer");
        nativeUI[7]=65;nativeUI[11]=46;
        auto anchoredMarker=evaluatePlacement(32,32,anchorOptions);
        require(std::abs(anchoredMarker[7]-65)<1e-4f && std::abs(anchoredMarker[11]-46)<1e-4f && anchoredMarker[4]==.25f*nativeUI[4] && anchoredMarker[9]==.25f*nativeUI[9],"marker root and 25-percent glyph size are written before raster clipping");
        auto objectiveIcon=evaluatePlacement(56,45.5f,anchorOptions);
        require(std::abs(objectiveIcon[7]+32*objectiveIcon[4]-(65+32*nativeUI[4]))<1e-4f &&
                std::abs(objectiveIcon[11]+32*objectiveIcon[9]-(46+32*nativeUI[9]))<1e-4f,
                "objective icon center stays at its native offset when shrunk");
        auto unitIcon=evaluatePlacement(57,18,anchorOptions,40);
        require(std::abs(unitIcon[7]+75*unitIcon[4]-(65+75*nativeUI[4]))<1e-4f &&
                std::abs(unitIcon[11]+36*unitIcon[9]-(46+36*nativeUI[9]))<1e-4f,
                "unit icon center stays at its native offset when shrunk");
        auto objectiveLetter=evaluatePlacement(25.5f,20.5f,anchorOptions,40);
        require(objectiveLetter[7]==objectiveIcon[7] && objectiveLetter[11]==objectiveIcon[11],
                "objective icon and letter use the same scale pivot");
        auto reprojectOptions=anchorOptions;reprojectOptions.worldReproject=1;reprojectOptions.worldX[2]=.1f;
        auto movedMarker=evaluatePlacement(32,32,reprojectOptions);
        require(std::abs(movedMarker[7]-75)<1e-4f && std::abs(movedMarker[11]-46)<1e-4f,"character-relative marker projection changes position without changing glyph size");
        std::vector<float> pooledConstants(262144,0);const UINT poolFirst=26928;
        memcpy(pooledConstants.data()+poolFirst*4,nativeUI.data(),sizeof(nativeUI));
        D3D11_BUFFER_DESC poolDesc{};poolDesc.ByteWidth=static_cast<UINT>(pooledConstants.size()*sizeof(float));poolDesc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        D3D11_SUBRESOURCE_DATA poolInitial{};poolInitial.pSysMem=pooledConstants.data();ComPtr<ID3D11Buffer> pooledUI;
        require(SUCCEEDED(device->CreateBuffer(&poolDesc,&poolInitial,&pooledUI)),"large pooled native UI constant fixture");
        BF2VR::UiAnchorTransform::Parameters poolOptions=anchorOptions;poolOptions.stride=56;poolOptions.indexed=1;poolOptions.indexBits=16;poolOptions.indexOffset=2;
        require(SUCCEEDED(placement.Prepare(pooledUI.Get(),poolFirst,64,uiVerticesBuffer.Get(),uiIndexBuffer.Get(),poolOptions)),"native UI pooled offsets beyond4096 constants accepted");
        std::vector<unsigned char> poolOutput;require(SUCCEEDED(BF2VR::MarkerGeometryAudit::ReadBuffer(device.Get(),context.Get(),placement.Constant(),poolOutput)),"pooled placement result");
        float pooledTx=0;memcpy(&pooledTx,poolOutput.data()+7*sizeof(float),sizeof(float));require(std::abs(pooledTx-65)<1e-4f,"pooled binding uses the selected native range");
        require(FAILED(placement.Prepare(pooledUI.Get(),~0u,64,uiVerticesBuffer.Get(),uiIndexBuffer.Get(),poolOptions)),"overflowing pooled offset rejected");
        nativeUI[7]=-100;nativeUI[11]=100;
        auto anchoredAim=evaluatePlacement(1287.5f,540,anchorOptions);
        require(std::abs(anchoredAim[7]-40)<1e-4f && std::abs(anchoredAim[11]+20)<1e-4f,"large aim offset stays inside the source clip volume at final size");
        nativeUI[11]=95.55555f;auto zoomReticle=evaluatePlacement(1354,515.25f,anchorOptions);
        require(std::abs(zoomReticle[11]+21.11111f)<1e-4f,"captured zoom-reticle geometry receives the same aim shift");
        nativeUI[11]=26.29629f;auto stamina=evaluatePlacement(1162.5f,496.5f,anchorOptions);
        require(stamina==reducedNative(),"central stamina bar is excluded from reticle placement");
        nativeUI[11]=100;auto fixedHUD=evaluatePlacement(349,1007,anchorOptions);
        require(fixedHUD==reducedNative(),"fixed status and minimap canvas keep their existing placement");
        nativeUI[5]=-.0548f;nativeUI[8]=-.1309f;
        auto mapOptions=anchorOptions;mapOptions.worldEnabled=BF2VR::UiAnchorTransform::WorldLayer(0,0,0,0)?1.f:0.f;
        auto rotatedMap=evaluatePlacement(0,0,mapOptions);
        require(rotatedMap==reducedNative(),"rotated minimap remains untouched outside the world overlay stage");
        nativeUI[7]=65;nativeUI[11]=46;
        auto arrowOptions=anchorOptions;arrowOptions.worldEnabled=BF2VR::UiAnchorTransform::WorldLayer(16,80,64,48)?1.f:0.f;
        auto rotatedArrow=evaluatePlacement(0,0,arrowOptions);
        require(std::abs(rotatedArrow[7]-65)<1e-4f && rotatedArrow[5]==.25f*nativeUI[5] && rotatedArrow[8]==.25f*nativeUI[8],"world arrow root is anchored while its native rotation is preserved");
        nativeUI[5]=nativeUI[8]=0;anchorOptions.aimEnabled=anchorOptions.worldEnabled=0;
        require(evaluatePlacement(32,32,anchorOptions)==reducedNative(),"unanchored geometry receives only the centered HUD scale");
        std::vector<unsigned char> untouchedUI;require(SUCCEEDED(BF2VR::MarkerGeometryAudit::ReadBuffer(device.Get(),context.Get(),nativeUIBuffer.Get(),untouchedUI)) && !memcmp(untouchedUI.data(),nativeUI.data(),sizeof(nativeUI)),"native UI source buffer remains unchanged");
        require(BF2VR::Timing::stats[BF2VR::Timing::CopyGPU].count.load() > 0, "asynchronous GPU timestamp sample collected");
        std::cout << "Frame blit pixel readback and pipeline restoration checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
