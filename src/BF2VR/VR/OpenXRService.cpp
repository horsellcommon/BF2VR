// Copyright Ethan Porcaro

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.

#include <OpenXRService.h>
#include <ActionsService.h>
#include <Timing.h>
#include <BF2Service.h>
#include <D3DService.h>
#include <Logging.h>
#include <Types.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <string>
#include <format>
#include <Runtime.h>
#include <VrOptions.h>
#include <wrl/client.h>
namespace BF2VR {
bool OpenXRService::Check(XrResult result, const char* operation) {
    if (XR_FAILED(result)) { Logging::Log(std::string("[OpenXR] ") + operation, result); return false; }
    return true;
}
int OpenXRService::Initialize() {
    Logging::Log("[OpenXR] Entering instance initialization");
    XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
    const char* extensions[]{XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
    info.enabledExtensionCount = 1; info.enabledExtensionNames = extensions;
    info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    strcpy_s(info.applicationInfo.applicationName, "BF2VR improved");
    Logging::Log("[OpenXR] Calling xrCreateInstance");
    const XrResult instanceResult = xrCreateInstance(&info, &xrInstance);
    Logging::Log("[OpenXR] xrCreateInstance returned", instanceResult);
    if (!Check(instanceResult, "create instance")) return -1;
    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    if (!Check(xrGetSystem(xrInstance, &systemInfo, &system), "get HMD")) return -1;
    PFN_xrGetD3D11GraphicsRequirementsKHR getRequirements = nullptr;
    if (!Check(xrGetInstanceProcAddr(xrInstance, "xrGetD3D11GraphicsRequirementsKHR",
        reinterpret_cast<PFN_xrVoidFunction*>(&getRequirements)), "load D3D requirements")) return -1;
    XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    if (!Check(getRequirements(xrInstance, system, &requirements), "D3D requirements")) return -1;
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC adapterDesc{};
    if (FAILED(D3DService::pDevice->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) ||
        FAILED(dxgiDevice->GetAdapter(&adapter)) || FAILED(adapter->GetDesc(&adapterDesc)) ||
        memcmp(&adapterDesc.AdapterLuid, &requirements.adapterLuid, sizeof(LUID)) != 0 ||
        D3DService::pDevice->GetFeatureLevel() < requirements.minFeatureLevel) {
        Logging::Log("[OpenXR] Game device does not meet the runtime GPU requirements"); return -1;
    }
    uint32_t count = 0;
    if (!Check(xrEnumerateEnvironmentBlendModes(xrInstance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &count, nullptr), "count blend modes") || count == 0) return -1;
    std::vector<XrEnvironmentBlendMode> modes(count);
    if (!Check(xrEnumerateEnvironmentBlendModes(xrInstance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, count, &count, modes.data()), "get blend modes")) return -1;
    blendMode = std::find(modes.begin(), modes.end(), XR_ENVIRONMENT_BLEND_MODE_OPAQUE) != modes.end() ? XR_ENVIRONMENT_BLEND_MODE_OPAQUE : modes.front();
    XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR}; binding.device = D3DService::pDevice;
    XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO}; sessionInfo.next = &binding; sessionInfo.systemId = system;
    if (!Check(xrCreateSession(xrInstance, &sessionInfo, &xrSession), "create session")) return -1;
    Logging::Log("[OpenXR] Session created");
    if (!Check(xrEnumerateViewConfigurationViews(xrInstance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &count, nullptr), "count views") || count != 2) return -1;
    std::array<XrViewConfigurationView, 2> configs{{{XR_TYPE_VIEW_CONFIGURATION_VIEW}, {XR_TYPE_VIEW_CONFIGURATION_VIEW}}};
    if (!Check(xrEnumerateViewConfigurationViews(xrInstance, system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &count, configs.data()), "get views")) return -1;
    hudWidth=configs[0].recommendedImageRectWidth;
    hudHeight=configs[0].recommendedImageRectHeight;
    if(hudWidth<=0 || hudHeight<=0)return -1;
    maxProjectionScale=std::min({double(Runtime::maxXrProjectionScale),
        double(D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)/hudWidth,
        double(D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)/hudHeight});
    for(const auto& view:configs) {
        maxProjectionScale=std::min({maxProjectionScale,double(view.maxImageRectWidth)/hudWidth,
            double(view.maxImageRectHeight)/hudHeight});
    }
    maxProjectionScale=std::max(1.0,maxProjectionScale);
    attemptedScale=Runtime::xrProjectionScale;
    const double scale=std::min<double>(attemptedScale,maxProjectionScale);
    swapchainWidth=static_cast<int>(hudWidth*scale);
    swapchainHeight=static_cast<int>(hudHeight*scale);
    if (!Check(xrEnumerateSwapchainFormats(xrSession, 0, &count, nullptr), "count formats") || count == 0) return -1;
    std::vector<int64_t> formats(count);
    if (!Check(xrEnumerateSwapchainFormats(xrSession, count, &count, formats.data()), "get formats")) return -1;
    int64_t format = 0;
    for (auto candidate : {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM}) {
        if (std::find(formats.begin(), formats.end(), candidate) != formats.end()) { format = candidate; break; }
    }
    if (!format) { Logging::Log("[OpenXR] No supported RGBA swapchain format"); return -1; }
    swapchainFormat=format;
    auto createSwapchains=[&] {
        for(int i=0;i<3;++i) {
            if(!CreateChain(i==2?hudWidth:swapchainWidth,i==2?hudHeight:swapchainHeight,
                swapchains[i],targets[i]))return false;
        }
        return true;
    };
    if(!createSwapchains()) {
        if((swapchainWidth==hudWidth && swapchainHeight==hudHeight) || !DestroySwapchains())return -1;
        Logging::Log("[OpenXR] Larger projection images unavailable; retrying recommended size");
        swapchainWidth=hudWidth;
        swapchainHeight=hudHeight;
        if(!createSwapchains())return -1;
    }
    Logging::Log(std::format("[OpenXR] Projection request {:.2f}x; recommended {}x{}, allocated {}x{} per eye",
        attemptedScale,hudWidth,hudHeight,swapchainWidth,swapchainHeight));
    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.poseInReferenceSpace.orientation.w = 1;
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    XrResult result = xrCreateReferenceSpace(xrSession, &spaceInfo, &xrSpace);
    if (result == XR_ERROR_REFERENCE_SPACE_UNSUPPORTED) {
        spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        result = xrCreateReferenceSpace(xrSession, &spaceInfo, &xrSpace);
        Logging::Log("[OpenXR] Using LOCAL tracking space; STAGE is unavailable");
    }
    if (!Check(result, "create tracking space")) return -1;
    spaceInfo.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW;
    if(XR_FAILED(xrCreateReferenceSpace(xrSession,&spaceInfo,&optionsSpace)))optionsSpace=XR_NULL_HANDLE;
    for (auto& view : views) view = {XR_TYPE_VIEW};
    Logging::Log("[OpenXR] Resources ready; waiting for runtime events on the render thread");
    return 0;
}
bool OpenXRService::CreateChain(int width,int height,XrSwapchain& chain,std::vector<ID3D11RenderTargetView*>& views) {
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT|XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    info.format=swapchainFormat;info.width=width;info.height=height;
    info.arraySize=info.faceCount=info.mipCount=info.sampleCount=1;
    if(!Check(xrCreateSwapchain(xrSession,&info,&chain),"create swapchain"))return false;
    uint32_t count=0;
    if(!Check(xrEnumerateSwapchainImages(chain,0,&count,nullptr),"count images") || !count)return false;
    std::vector<XrSwapchainImageD3D11KHR> images(count,{XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    if(!Check(xrEnumerateSwapchainImages(chain,count,&count,reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),"get images"))return false;
    D3D11_RENDER_TARGET_VIEW_DESC targetInfo{};
    targetInfo.Format=static_cast<DXGI_FORMAT>(swapchainFormat);
    targetInfo.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
    for(uint32_t i=0;i<count;++i) {
        ID3D11RenderTargetView* target=nullptr;
        const HRESULT hr=D3DService::pDevice->CreateRenderTargetView(images[i].texture,&targetInfo,&target);
        if(FAILED(hr)) {Logging::Log("[OpenXR] Could not create image target",static_cast<int>(hr));return false;}
        views.push_back(target);
    }
    return true;
}

void OpenXRService::ApplyProjectionScale() {
    const float requested=Runtime::xrProjectionScale;
    if(requested==attemptedScale)return;
    attemptedScale=requested;
    const double scale=std::min<double>(requested,maxProjectionScale);
    const int width=static_cast<int>(hudWidth*scale),height=static_cast<int>(hudHeight*scale);
    if(width==swapchainWidth && height==swapchainHeight)return;

    std::array<XrSwapchain,2> next{};
    std::array<std::vector<ID3D11RenderTargetView*>,2> nextViews;
    if(CreateChain(width,height,next[0],nextViews[0]) && CreateChain(width,height,next[1],nextViews[1])) {
        for(int i=0;i<2;++i) {
            std::swap(next[i],swapchains[i]);
            std::swap(nextViews[i],targets[i]);
        }
        swapchainWidth=width;swapchainHeight=height;
        Logging::Log(std::format("[OpenXR] Live projection size {}x{} per eye",width,height));
    } else Logging::Log("[OpenXR] Projection resize rejected; keeping active eye images");
    for(int i=0;i<2;++i)DestroyChain(next[i],nextViews[i]);
}

void OpenXRService::HandleStates() {
    if (!xrInstance) return;
    for (;;) {
        XrEventDataBuffer buffer{XR_TYPE_EVENT_DATA_BUFFER};
        XrResult result = xrPollEvent(xrInstance, &buffer);
        if (result == XR_EVENT_UNAVAILABLE) return;
        if (!Check(result, "poll events")) { xrExiting = true; return; }
        if (buffer.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) { xrExiting = true; xrFocused = false; return; }
        if (buffer.type != XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) continue;
        auto event = reinterpret_cast<XrEventDataSessionStateChanged*>(&buffer);
        if (event->session != xrSession) continue;
        Logging::Log("[OpenXR] Session state", static_cast<int>(event->state));
        xrFocused = event->state == XR_SESSION_STATE_FOCUSED;
        switch (event->state) {
        case XR_SESSION_STATE_READY: {
            if (xrRunning) break;
            XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO}; begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            xrRunning = Check(xrBeginSession(xrSession, &begin), "begin session");
            if (xrRunning) Logging::Log("[OpenXR] Session began successfully");
            cycle.Reset(); poseValid = false;
            break;
        }
        case XR_SESSION_STATE_STOPPING:
            if (cycle.begun) EndFrame(false);
            if (xrRunning) Check(xrEndSession(xrSession), "end session");
            xrRunning = false; poseValid = false; ActionsService::ResetInput(); break;
        case XR_SESSION_STATE_EXITING:
        case XR_SESSION_STATE_LOSS_PENDING:
            xrExiting = true; poseValid = false; ActionsService::ResetInput(); break;
        default: break;
        }
        if (!xrFocused) ActionsService::ResetInput();
    }
}
bool OpenXRService::Acquire(int chain) {
    XrSwapchainImageAcquireInfo info{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrResult acquireResult;
    { Timing::Scope timer(Timing::AcquireImage); acquireResult = xrAcquireSwapchainImage(swapchains[chain], &info, &imageIndices[chain]); }
    if (!Check(acquireResult, "acquire image")) return false;
    acquired[chain] = true;
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO}; wait.timeout = 1000000000; // bounded 1 s
    XrResult result;
    { Timing::Scope timer(Timing::WaitImage); result = xrWaitSwapchainImage(swapchains[chain], &wait); }
    if (result != XR_SUCCESS) { Logging::Log("[OpenXR] Image wait failed or timed out", result); return false; }
    writable[chain] = true;
    return imageIndices[chain] < targets[chain].size();
}
bool OpenXRService::Release(int chain) {
    if (!acquired[chain]) return true;
    if (!writable[chain]) return false; // destroy the session rather than release an unwaited image
    XrSwapchainImageReleaseInfo info{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if (!Check(xrReleaseSwapchainImage(swapchains[chain], &info), "release image")) return false;
    acquired[chain] = writable[chain] = false;
    return true;
}
bool OpenXRService::PrepareDraw() {
    if (!xrRunning || xrExiting) return false;
    if (!cycle.begun) {
        ApplyProjectionScale();
        XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO}; xrFrameState = {XR_TYPE_FRAME_STATE};
        XrResult waitResult;
        { Timing::Scope timer(Timing::WaitFrame); waitResult = xrWaitFrame(xrSession, &wait, &xrFrameState); }
        if (!Check(waitResult, "wait frame")) { xrExiting = true; return false; }
        XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
        if (!Check(xrBeginFrame(xrSession, &begin), "begin frame")) { xrExiting = true; return false; }
        cycle.begun = true;
        shouldRender = xrFrameState.shouldRender == XR_TRUE;
        XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
        locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate.displayTime = xrFrameState.predictedDisplayTime; locate.space = xrSpace;
        XrViewState state{XR_TYPE_VIEW_STATE}; uint32_t count = 0;
        poseValid = Check(xrLocateViews(xrSession, &locate, &state, 2, &count, views.data()), "locate views") && count == 2 &&
            (state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) && (state.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT);
        if (xrFocused) ActionsService::SyncActions(); else ActionsService::ResetInput();
        if (!shouldRender || !poseValid) { EndFrame(false); return false; }
        if (!Acquire(2)) { xrExiting = true; return false; }
        const float transparent[4]{};
        D3DService::pContext->ClearRenderTargetView(UITarget(), transparent);
        hud = {XR_TYPE_COMPOSITION_LAYER_QUAD}; hud.space = xrSpace;
        hud.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        hud.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        hud.pose = views[0].pose;
        Vec3 position{hud.pose.position.x, hud.pose.position.y, hud.pose.position.z};
        auto orientation = hud.pose.orientation;
        Vec3 offset = rotateAround({.25f, .2f, -1.f}, {}, -eulerFromQuat({orientation.x, orientation.y, orientation.z, orientation.w}).y);
        hud.pose.position = {position.x + offset.x, position.y + offset.y, position.z + offset.z};
        hud.size = {1.f, static_cast<float>(hudHeight) / hudWidth};
        hud.subImage.swapchain = swapchains[2]; hud.subImage.imageRect.extent = {hudWidth, hudHeight};
        const int panelWidth=std::min(hudWidth,static_cast<int>(VrOptions::Width));
        const int panelHeight=std::min(hudHeight,static_cast<int>(VrOptions::Height));
        optionsDrawn=optionsSpace && VrOptions::Draw(D3DService::pDevice,D3DService::pContext,
            UITarget(),panelWidth,panelHeight);
        if(optionsDrawn) {
            hud.space=optionsSpace;
            hud.pose={{0,0,0,1},{0,0,-1.15f}};
            hud.size={.86f,.632f};
            hud.subImage.imageRect.extent={panelWidth,panelHeight};
        }
    }
    if (!cycle.acquired) {
        if (!Acquire(cycle.eye)) { xrExiting = true; return false; }
        cycle.acquired = true;
        nativeFovValid[cycle.eye]=false;
        auto& projection = projectionViews[cycle.eye]; projection = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        projection.pose = views[cycle.eye].pose; projection.fov = views[cycle.eye].fov;
        projection.subImage.swapchain = swapchains[cycle.eye];
        projection.subImage.imageRect.extent = {swapchainWidth, swapchainHeight};
        UpdatePose();
    }
    renderReady = true;
    return true;
}
void OpenXRService::RecordNativeFov(const XrFovf& fov) {
    if (cycle.begun && cycle.acquired) { nativeFovs[cycle.eye]=fov; nativeFovValid[cycle.eye]=true; }
}
void OpenXRService::UpdatePose() {
    if (!poseValid) return;
    ++poseRevision;
    BF2Service::viewMatrix = fromOpenXR(views[cycle.eye]);
    XrView center=views[0];
    center.pose.position.x=(views[0].pose.position.x+views[1].pose.position.x)*.5f;
    center.pose.position.y=(views[0].pose.position.y+views[1].pose.position.y)*.5f;
    center.pose.position.z=(views[0].pose.position.z+views[1].pose.position.z)*.5f;
    BF2Service::headCenterMatrix=fromOpenXR(center);
    auto fov = views[cycle.eye].fov;
    BF2Service::projMatrix = fromFOV(fov.angleLeft, fov.angleRight, fov.angleUp, fov.angleDown);
}
bool OpenXRService::FinishEye() {
    renderReady = false;
    if (!cycle.begun || !cycle.acquired) return false;
    if (!Release(cycle.eye)) { xrExiting = true; return false; }
    if (cycle.ReleaseEye()) return EndFrame(true);
    UpdatePose(); // the engine's next camera update must use the next eye
    return false;
}
bool OpenXRService::EndFrame(bool submit) {
    renderReady = false;
    bool released = true;
    for (int i = 0; i < 3; ++i) if (!Release(i)) released = false;
    if (!released) { xrExiting = true; return false; }
    if (submit && Runtime::nativeSubmissionFov && nativeFovValid[0] && nativeFovValid[1]) {
        for (int eye=0;eye<2;++eye) projectionViews[eye].fov=nativeFovs[eye];
        static auto lastFovLog=Timing::Clock::time_point{};
        if (Timing::Clock::now()-lastFovLog >= std::chrono::seconds(5)) {
            lastFovLog=Timing::Clock::now();
            Logging::Log(std::format("[FOV_AUDIT] Native generator FOV submitted: left=[{:.4f},{:.4f},{:.4f},{:.4f}] right=[{:.4f},{:.4f},{:.4f},{:.4f}]",nativeFovs[0].angleLeft,nativeFovs[0].angleRight,nativeFovs[0].angleUp,nativeFovs[0].angleDown,nativeFovs[1].angleLeft,nativeFovs[1].angleRight,nativeFovs[1].angleUp,nativeFovs[1].angleDown));
        }
    }
    // Diagnostic only: swap the two completed images, preserving each eye's
    // compositor pose/FOV and the original acquire/release ownership.
    for (int eye = 0; eye < 2; ++eye)
        projectionViews[eye].subImage.swapchain = swapchains[Runtime::swapCapturedEyes ? 1-eye : eye];
    XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    projection.space = xrSpace; projection.viewCount = 2; projection.views = projectionViews.data();
    const XrCompositionLayerBaseHeader* layers[]{reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection), reinterpret_cast<const XrCompositionLayerBaseHeader*>(&hud)};
    XrFrameEndInfo info{XR_TYPE_FRAME_END_INFO};
    info.displayTime = xrFrameState.predictedDisplayTime; info.environmentBlendMode = blendMode;
    info.layerCount = submit && shouldRender && poseValid ? ((uiLayerEnabled || optionsDrawn) ? 2 : 1) : 0; info.layers = layers;
    XrResult endResult;
    { Timing::Scope timer(Timing::EndXRFrame); endResult = xrEndFrame(xrSession, &info); }
    bool ok = Check(endResult, "end frame");
    cycle.Reset();
    static bool firstSubmission = true;
    if (ok && submit && firstSubmission) { Logging::Log("[OpenXR] First stereo frame submitted successfully"); firstSubmission = false; }
    if (!ok) xrExiting = true;
    return ok;
}
ID3D11RenderTargetView* OpenXRService::EyeTarget() {
    int eye = cycle.eye;
    return writable[eye] && imageIndices[eye] < targets[eye].size() ? targets[eye][imageIndices[eye]] : nullptr;
}
ID3D11RenderTargetView* OpenXRService::UITarget() {
    return writable[2] && imageIndices[2] < targets[2].size() ? targets[2][imageIndices[2]] : nullptr;
}
bool OpenXRService::DestroyChain(XrSwapchain& chain,std::vector<ID3D11RenderTargetView*>& views) {
    for(auto* view:views)view->Release();
    views.clear();
    if(!chain)return true;
    if(!Check(xrDestroySwapchain(chain),"destroy swapchain"))return false;
    chain=XR_NULL_HANDLE;
    return true;
}
bool OpenXRService::DestroySwapchains() {
    bool success=true;
    for(int i=0;i<3;++i)success=DestroyChain(swapchains[i],targets[i]) && success;
    return success;
}
int OpenXRService::Uninitialize() {
    renderReady = false;
    xrFocused = false; poseValid = false; ActionsService::ResetInput();
    if (xrSession && xrRunning) {
        if (cycle.begun) EndFrame(false);
        if (XR_SUCCEEDED(xrRequestExitSession(xrSession))) {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (xrRunning && !xrExiting && std::chrono::steady_clock::now() < deadline) {
                HandleStates(); std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }
    ActionsService::Uninitialize();
    VrOptions::Shutdown();
    if(optionsSpace) {xrDestroySpace(optionsSpace);optionsSpace=XR_NULL_HANDLE;}
    optionsDrawn=false;
    if (xrSpace) { xrDestroySpace(xrSpace); xrSpace = XR_NULL_HANDLE; }
    DestroySwapchains();
    if (xrSession) { xrDestroySession(xrSession); xrSession = XR_NULL_HANDLE; }
    swapchains.fill(XR_NULL_HANDLE);
    if (xrInstance) { xrDestroyInstance(xrInstance); xrInstance = XR_NULL_HANDLE; }
    acquired.fill(false); writable.fill(false); cycle.Reset(); xrRunning = false;
    return 0;
}
}
