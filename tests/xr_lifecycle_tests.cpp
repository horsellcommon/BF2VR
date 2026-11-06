// Exercises production OpenXRService with a strict fake XR runtime and real
// software D3D11 textures. No game, injection, HMD, or installed XR runtime.
#include <OpenXRService.h>
#include <ActionsService.h>
#include <D3DService.h>
#include <Logging.h>
#include <Runtime.h>
#include <VrOptions.h>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <wrl/client.h>
using namespace BF2VR;
namespace {
struct Chain {
    ID3D11Texture2D* texture = nullptr;
    bool acquired = false, waited = false;
    int index = 0;
} chains[6];
std::deque<XrSessionState> events;
int nextChain = 0, failures = 0, frames = 0, layers = 0, resetCalls = 0;
bool running = false, begun = false, render = true, tracking = true;
bool failFormats = false, failAcquire = false, failWait = false, failBegin = false;
bool stageSupported = true;
bool rejectSecondLargeEye=false;
uint32_t rightEyeMaxWidth=40;
bool showOptions=false;
bool expectedNativeFov = false;
std::vector<int> releases;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
XrResult protocol(bool valid) { if (!valid) { ++failures; return XR_ERROR_CALL_ORDER_INVALID; } return XR_SUCCESS; }
Chain& chain(XrSwapchain handle) { return chains[reinterpret_cast<uintptr_t>(handle)-10]; }
XrResult XRAPI_CALL requirements(XrInstance, XrSystemId, XrGraphicsRequirementsD3D11KHR* out) {
    Microsoft::WRL::ComPtr<IDXGIDevice> device;
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC desc{};
    D3DService::pDevice->QueryInterface(IID_PPV_ARGS(&device)); device->GetAdapter(&adapter); adapter->GetDesc(&desc);
    out->adapterLuid = desc.AdapterLuid; out->minFeatureLevel = D3D_FEATURE_LEVEL_11_0; return XR_SUCCESS;
}
void reset() {
    showOptions=false;rejectSecondLargeEye=false;rightEyeMaxWidth=40; Runtime::xrProjectionScale=1.f;
    events.clear(); nextChain = 0; running = begun = false;
    render = tracking = stageSupported = true;
    failFormats = failAcquire = failWait = failBegin = false;
    releases.clear();
    OpenXRService::xrExiting = false; Runtime::swapCapturedEyes = false;
}
void ready() { events.push_back(XR_SESSION_STATE_READY); events.push_back(XR_SESSION_STATE_FOCUSED); OpenXRService::HandleStates(); }
}
namespace BF2VR {
void Logging::Log(const std::string& text, int code) { std::cout << text << " (" << code << ")\n"; }
void ActionsService::ResetInput() { ++resetCalls; }
void ActionsService::SyncActions() {}
void ActionsService::Uninitialize() {}
bool VrOptions::Draw(ID3D11Device*,ID3D11DeviceContext*,ID3D11RenderTargetView*,UINT,UINT) {return showOptions;}
void VrOptions::Shutdown() {}
}
extern "C" {
XRAPI_ATTR XrResult XRAPI_CALL xrCreateInstance(const XrInstanceCreateInfo*, XrInstance* out) { *out = reinterpret_cast<XrInstance>(1); return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrDestroyInstance(XrInstance) { return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrGetInstanceProcAddr(XrInstance, const char*, PFN_xrVoidFunction* out) { *out = reinterpret_cast<PFN_xrVoidFunction>(requirements); return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrGetSystem(XrInstance, const XrSystemGetInfo*, XrSystemId* out) { *out = 1; return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateEnvironmentBlendModes(XrInstance, XrSystemId, XrViewConfigurationType, uint32_t capacity, uint32_t* count, XrEnvironmentBlendMode* out) { *count = 2; if (capacity) { out[0] = XR_ENVIRONMENT_BLEND_MODE_OPAQUE; out[1] = XR_ENVIRONMENT_BLEND_MODE_ADDITIVE; } return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrCreateSession(XrInstance, const XrSessionCreateInfo*, XrSession* out) { *out = reinterpret_cast<XrSession>(2); return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrDestroySession(XrSession) { running = begun = false; return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateViewConfigurationViews(XrInstance, XrSystemId, XrViewConfigurationType, uint32_t capacity, uint32_t* count, XrViewConfigurationView* out) {
    *count = 2; if (capacity) for (int i = 0; i < 2; ++i) { out[i].recommendedImageRectWidth = 32; out[i].recommendedImageRectHeight = 32; out[i].maxImageRectWidth = i ? rightEyeMaxWidth : 64; out[i].maxImageRectHeight = 64; } return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateSwapchainFormats(XrSession, uint32_t capacity, uint32_t* count, int64_t* out) { if (failFormats) return XR_ERROR_RUNTIME_FAILURE; *count = 1; if (capacity) *out = DXGI_FORMAT_R8G8B8A8_UNORM; return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrCreateSwapchain(XrSession, const XrSwapchainCreateInfo* info, XrSwapchain* out) {
    nextChain=0;
    while(nextChain<6 && chains[nextChain].texture)++nextChain;
    if(nextChain==6 || (rejectSecondLargeEye && (nextChain==1 || nextChain==4) && info->width>32))return XR_ERROR_RUNTIME_FAILURE;
    auto& item = chains[nextChain]; item = {};
    D3D11_TEXTURE2D_DESC desc{}; desc.Width = info->width; desc.Height = info->height; desc.MipLevels = desc.ArraySize = 1;
    desc.Format = static_cast<DXGI_FORMAT>(info->format); desc.SampleDesc.Count = 1; desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(D3DService::pDevice->CreateTexture2D(&desc, nullptr, &item.texture))) return XR_ERROR_RUNTIME_FAILURE;
    *out = reinterpret_cast<XrSwapchain>(static_cast<uintptr_t>(10 + nextChain++)); return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrDestroySwapchain(XrSwapchain handle) { auto& item = chain(handle); if (item.texture) item.texture->Release(); item = {}; return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateSwapchainImages(XrSwapchain handle, uint32_t capacity, uint32_t* count, XrSwapchainImageBaseHeader* out) {
    *count = 3; if (capacity) for (int i = 0; i < 3; ++i) reinterpret_cast<XrSwapchainImageD3D11KHR*>(out)[i].texture = chain(handle).texture; return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrCreateReferenceSpace(XrSession, const XrReferenceSpaceCreateInfo* info, XrSpace* out) { if (!stageSupported && info->referenceSpaceType == XR_REFERENCE_SPACE_TYPE_STAGE) return XR_ERROR_REFERENCE_SPACE_UNSUPPORTED; *out = reinterpret_cast<XrSpace>(3); return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrDestroySpace(XrSpace) { return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrPollEvent(XrInstance, XrEventDataBuffer* out) {
    if (events.empty()) return XR_EVENT_UNAVAILABLE;
    auto event = reinterpret_cast<XrEventDataSessionStateChanged*>(out);
    event->type = XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED; event->session = reinterpret_cast<XrSession>(2); event->state = events.front(); events.pop_front(); return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrBeginSession(XrSession, const XrSessionBeginInfo*) { if (failBegin) return XR_ERROR_RUNTIME_FAILURE; running = true; return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrEndSession(XrSession) { auto result = protocol(running && !begun); running = false; return result; }
XRAPI_ATTR XrResult XRAPI_CALL xrRequestExitSession(XrSession) { events.push_back(XR_SESSION_STATE_STOPPING); return XR_SUCCESS; }
XRAPI_ATTR XrResult XRAPI_CALL xrWaitFrame(XrSession, const XrFrameWaitInfo*, XrFrameState* out) {
    if (XR_FAILED(protocol(running && !begun))) return XR_ERROR_CALL_ORDER_INVALID;
    out->predictedDisplayTime = 1000000 + frames; out->shouldRender = render; return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrBeginFrame(XrSession, const XrFrameBeginInfo*) { auto result = protocol(running && !begun); begun = true; return result; }
XRAPI_ATTR XrResult XRAPI_CALL xrLocateViews(XrSession, const XrViewLocateInfo*, XrViewState* state, uint32_t, uint32_t* count, XrView* out) {
    *count = 2; state->viewStateFlags = tracking ? XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT : 0;
    for (int i = 0; i < 2; ++i) { out[i].pose.orientation.w = 1; out[i].pose.position.x = i ? .03f : -.03f; out[i].fov = {-.7f+.1f*i,.7f+.1f*i,.7f,-.7f}; } return XR_SUCCESS;
}
XRAPI_ATTR XrResult XRAPI_CALL xrAcquireSwapchainImage(XrSwapchain handle, const XrSwapchainImageAcquireInfo*, uint32_t* index) {
    if (failAcquire) return XR_ERROR_RUNTIME_FAILURE;
    auto& item = chain(handle); auto result = protocol(begun && !item.acquired); item.acquired = true; item.waited = false;
    // Deliberately give HUD and eye different indices.
    *index = reinterpret_cast<uintptr_t>(handle) == 12 ? 2 : item.index++ % 2; return result;
}
XRAPI_ATTR XrResult XRAPI_CALL xrWaitSwapchainImage(XrSwapchain handle, const XrSwapchainImageWaitInfo*) {
    auto& item = chain(handle); if (failWait) return XR_TIMEOUT_EXPIRED;
    auto result = protocol(item.acquired && !item.waited); item.waited = true; return result;
}
XRAPI_ATTR XrResult XRAPI_CALL xrReleaseSwapchainImage(XrSwapchain handle, const XrSwapchainImageReleaseInfo*) {
    auto& item = chain(handle); auto result = protocol(item.acquired && item.waited);
    item.acquired = item.waited = false; releases.push_back(static_cast<int>(reinterpret_cast<uintptr_t>(handle)-10)); return result;
}
XRAPI_ATTR XrResult XRAPI_CALL xrEndFrame(XrSession, const XrFrameEndInfo* info) {
    auto result = protocol(begun && !chains[0].acquired && !chains[1].acquired && !chains[2].acquired);
    if (info->layerCount) {
        const auto* layer = reinterpret_cast<const XrCompositionLayerProjection*>(info->layers[0]);
        for (int eye=0; eye<2; ++eye) {
            const int image = Runtime::swapCapturedEyes ? 1-eye : eye;
            require(layer->views[eye].subImage.swapchain == reinterpret_cast<XrSwapchain>(static_cast<uintptr_t>(10+image)), "submitted image routing");
            require(layer->views[eye].pose.position.x == (eye ? .03f : -.03f) &&
                    layer->views[eye].fov.angleLeft == (expectedNativeFov ? -.8f : -.7f+.1f*eye),
                    "eye pose and asymmetric FOV stay with compositor eye");
        }
    }
    begun = false; ++frames; layers = info->layerCount; return result;
}
}
int main() {
    try {
        require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
            &D3DService::pDevice, nullptr, &D3DService::pContext)), "software D3D11 device");
        reset(); Runtime::xrProjectionScale=2.f;
        require(OpenXRService::Initialize()==0 && OpenXRService::swapchainWidth==40 &&
                OpenXRService::swapchainHeight==40, "projection scale respects the smaller eye limit and preserves aspect");
        D3D11_TEXTURE2D_DESC hudSize{};chains[2].texture->GetDesc(&hudSize);
        require(hudSize.Width==32 && hudSize.Height==32, "separate HUD swapchain stays at recommended size");
        OpenXRService::Uninitialize();
        reset(); Runtime::xrProjectionScale=2.f;rightEyeMaxWidth=64;
        require(OpenXRService::Initialize()==0 && OpenXRService::swapchainWidth==64 &&
                OpenXRService::swapchainHeight==64,"2x allocation when the runtime supports it");
        OpenXRService::Uninitialize();
        reset(); Runtime::xrProjectionScale=1.25f;rejectSecondLargeEye=true;
        require(OpenXRService::Initialize()==0 && OpenXRService::swapchainWidth==32,
                "failed larger second eye releases partial swapchains and retries recommended size");
        OpenXRService::Uninitialize();
        reset();require(OpenXRService::Initialize()==0,"live resize setup");ready();
        require(OpenXRService::PrepareDraw(),"prepare first eye before live resize");
        Runtime::xrProjectionScale=1.25f;
        require(!OpenXRService::FinishEye() && OpenXRService::PrepareDraw() &&
                OpenXRService::swapchainWidth==32,"live resize waits for the second eye");
        require(OpenXRService::FinishEye() && OpenXRService::PrepareDraw() &&
                OpenXRService::swapchainWidth==40,"live resize takes effect at the next stereo frame");
        OpenXRService::Uninitialize();
        reset();rightEyeMaxWidth=64;
        require(OpenXRService::Initialize()==0,"live resize rejection setup");ready();
        Runtime::xrProjectionScale=2.f;rejectSecondLargeEye=true;
        require(OpenXRService::PrepareDraw() && OpenXRService::swapchainWidth==32 &&
                !chains[3].texture && !chains[4].texture,"live resize rolls back partial allocation");
        require(!OpenXRService::FinishEye() && OpenXRService::PrepareDraw() && OpenXRService::FinishEye(),
                "previous stereo pair still submits after resize rejection");
        OpenXRService::Uninitialize();
        reset();require(OpenXRService::Initialize()==0,"options overlay setup");ready();
        OpenXRService::uiLayerEnabled=false;showOptions=true;
        require(OpenXRService::PrepareDraw() && !OpenXRService::FinishEye() &&
                OpenXRService::PrepareDraw() && OpenXRService::FinishEye() && layers==2,
                "options quad submits even when the legacy UI layer is disabled");
        showOptions=false;
        require(OpenXRService::PrepareDraw() && !OpenXRService::FinishEye() &&
                OpenXRService::PrepareDraw() && OpenXRService::FinishEye() && layers==1,
                "closing options restores projection-only submission");
        OpenXRService::uiLayerEnabled=true;OpenXRService::Uninitialize();
        reset(); stageSupported = false;
        require(OpenXRService::Initialize() == 0, "STAGE to LOCAL fallback and multi-mode enumeration");
        ready(); require(OpenXRService::xrRunning && OpenXRService::xrFocused, "READY then FOCUSED");
        for (int i = 0; i < 10; ++i) {
            require(OpenXRService::PrepareDraw(), "prepare left");
            require(OpenXRService::EyeTarget() && OpenXRService::UITarget(), "separate image indices");
            require(!OpenXRService::FinishEye(), "left is not a complete stereo frame");
            require(OpenXRService::PrepareDraw(), "prepare right");
            require(OpenXRService::FinishEye(), "complete stereo frame");
            require(layers == 2, "eye and HUD layers submitted");
        }
        for (bool swapped : {true, false}) {
            Runtime::swapCapturedEyes = swapped;
            require(OpenXRService::PrepareDraw(), "swap diagnostic left acquire");
            require(!OpenXRService::FinishEye(), "swap diagnostic left release");
            require(OpenXRService::PrepareDraw(), "swap diagnostic right acquire");
            require(OpenXRService::FinishEye(), "swap diagnostic pair submits");
        }
        Runtime::nativeSubmissionFov=true; expectedNativeFov=true;
        require(OpenXRService::PrepareDraw(), "native FOV pair left");
        OpenXRService::RecordNativeFov({-.8f,.8f,.6f,-.6f});
        require(!OpenXRService::FinishEye(), "native FOV left release");
        require(OpenXRService::PrepareDraw(), "native FOV pair right");
        OpenXRService::RecordNativeFov({-.8f,.8f,.6f,-.6f});
        require(OpenXRService::FinishEye(), "native FOV pair submit");
        expectedNativeFov=false;
        require(OpenXRService::PrepareDraw(), "fresh pair fallback left");
        OpenXRService::RecordNativeFov({-.8f,.8f,.6f,-.6f});
        require(!OpenXRService::FinishEye(), "fallback left release");
        require(OpenXRService::PrepareDraw(), "fallback right without native record");
        require(OpenXRService::FinishEye(), "incomplete native FOV pair keeps headset metadata");
        Runtime::nativeSubmissionFov=false;
        for (size_t i = 0; i < releases.size(); i += 3) require(releases[i] == 0 && releases[i+1] == 1 && releases[i+2] == 2, "release left then right then HUD");
        events.push_back(XR_SESSION_STATE_VISIBLE); OpenXRService::HandleStates();
        require(!OpenXRService::xrFocused && resetCalls > 0, "focus loss clears input");
        events.push_back(XR_SESSION_STATE_STOPPING); OpenXRService::HandleStates();
        require(!OpenXRService::xrRunning && !OpenXRService::PrepareDraw(), "stopped session does not render");
        ready(); require(OpenXRService::xrRunning, "session restarts after stop");
        render = false; require(!OpenXRService::PrepareDraw() && layers == 0, "shouldRender false submits no layers");
        render = true; tracking = false; require(!OpenXRService::PrepareDraw() && layers == 0, "invalid tracking submits no layers");
        tracking = true; require(OpenXRService::PrepareDraw(), "begin frame before stop");
        events.push_back(XR_SESSION_STATE_STOPPING); OpenXRService::HandleStates();
        require(!OpenXRService::xrRunning && !begun, "stop closes partial stereo frame");
        OpenXRService::Uninitialize(); OpenXRService::Uninitialize();
        reset(); failFormats = true; require(OpenXRService::Initialize() != 0, "format error propagated"); OpenXRService::Uninitialize();
        reset(); require(OpenXRService::Initialize() == 0, "reinitialize after failure"); failBegin = true; ready();
        require(!OpenXRService::xrRunning, "failed begin never marks running"); OpenXRService::Uninitialize();
        reset(); require(OpenXRService::Initialize() == 0, "acquire failure setup"); ready(); failAcquire = true;
        require(!OpenXRService::PrepareDraw() && OpenXRService::xrExiting, "acquire failure stops submission"); OpenXRService::Uninitialize();
        reset(); require(OpenXRService::Initialize() == 0, "timeout setup"); ready(); failWait = true;
        require(!OpenXRService::PrepareDraw() && OpenXRService::xrExiting, "timeout stops submission"); OpenXRService::Uninitialize();
        require(failures == 0, "no invalid runtime call ordering");
        D3DService::pContext->ClearState(); D3DService::pContext->Release(); D3DService::pDevice->Release();
        std::cout << "XR lifecycle regression scenarios passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
