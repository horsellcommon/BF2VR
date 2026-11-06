// Copyright Ethan Porcaro

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.

#include <Windows.h>
#include <cstdio>
#include <Logging.h>
#include <BF2Service.h>
#include <D3DService.h>
#include <OpenXRService.h>
#include <ActionsService.h>
#include <Runtime.h>
#include <CrashTrace.h>
#include <filesystem>
#include <fstream>
#include <format>
#include <WorldScale.h>
#include <MenuInput.h>
#include <VrOptions.h>
#include <MarkerCameraAudit.h>
#include <MarkerGeometryAudit.h>
namespace BF2VR {
LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;
void RefreshWindowSize() {
    HWND window = FindWindowW(L"Frostbite", L"STAR WARS Battlefront II");
    RECT rect{};
    if (!window || !GetWindowRect(window, &rect)) return;
    int width = rect.right - rect.left, height = rect.bottom - rect.top;
    if (width < 4 || height < 4) return;
    const UINT flags = SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS;
    SetWindowPos(window, nullptr, 0, 0, width - 1, height, flags);
    SetWindowPos(window, nullptr, 0, 0, width, height, flags);
}
void Shutdown() {
    Runtime::enabled = false;
    std::lock_guard lock(Runtime::mutex);
    Logging::Log(Runtime::desktopAuditOnly ? "[CORE] Disabling desktop audit" : "[CORE] Disabling VR and restoring settings");
    D3DService::Uninitialize();
    if(!Runtime::desktopAuditOnly) {
        OpenXRService::Uninitialize();
        BF2Service::Uninitialize();
        RefreshWindowSize();
    }
    SetUnhandledExceptionFilter(previousFilter);
    CrashTrace::Stop();
    Logging::Log("[CORE] Mod disabled. Restart the game before loading again. DLL retained for callback safety.");
    FreeConsole();
}
void ReadProjectionScale(const std::filesystem::path& folder) {
    std::ifstream file(folder/L"XRProjectionScale.txt");
    float scale=1.f;
    char trailing=0;
    if(!(file>>scale) || (file>>trailing) || !std::isfinite(scale) || scale<1.f || scale>Runtime::maxXrProjectionScale)scale=1.f;
    Runtime::xrProjectionScale=scale;
}
void ApplyHudScaleFile(const std::filesystem::path& folder) {
    std::ifstream file(folder/L"HudScale.txt");
    float scale=0.f;char extra=0;
    if(!(file>>scale) || (file>>extra) || !std::isfinite(scale) || scale<.25f || scale>1.f)return;
    static float previous=0.f;
    if(scale==previous)return;
    const std::string shader=std::format(R"(Texture2D image:register(t0);SamplerState linearClamp:register(s0);
struct V{{float4 position:SV_Position;float2 uv:TEXCOORD0;}};
float4 PS(V v):SV_Target{{float2 p=(v.uv-.5)/{:.6f}+.5;if(any(p<0)||any(p>1))return 0;return image.Sample(linearClamp,p);}})",scale);
    std::lock_guard lock(Runtime::mutex);
    D3DService::QueueHudShader(shader);
    Runtime::hudScale=scale;
    previous=scale;
}
DWORD WINAPI MainThread(void*) {
    HMODULE pinned = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&MainThread), &pinned);
    AllocConsole();
    FILE* stream = nullptr; freopen_s(&stream, "CONOUT$", "w", stdout);
    previousFilter = SetUnhandledExceptionFilter(Logging::UnhandledHandler);
    Logging::Log("[CORE] BF2VR improved; offline Arcade only");
    CrashTrace::Start();
    Logging::Log("[CORE] Candidate 46; Quest infantry controls");
    Logging::Log("[CORE] Using inherited game offsets with ASLR relocation and page checks; game-build compatibility is not signature-verified");
    wchar_t modulePath[32768]{};
    GetModuleFileNameW(pinned, modulePath, 32768);
    const auto folder = std::filesystem::path(modulePath).parent_path();
    VrOptions::Initialize(folder);
    Runtime::desktopAuditOnly=GetFileAttributesW((folder/L"DesktopHudAudit.flag").c_str())!=INVALID_FILE_ATTRIBUTES;
    Runtime::hudAnchorsEnabled=GetFileAttributesW((folder/L"EnableHudAnchors.flag").c_str())!=INVALID_FILE_ATTRIBUTES;
    ReadProjectionScale(folder);
    bool failed=false;
    if(Runtime::desktopAuditOnly) {
        Runtime::cameraEnabled=false;Runtime::visualEnabled=false;Runtime::poseEnabled=false;
        Logging::Log("[CORE] DESKTOP AUDIT ONLY: no OpenXR session, camera hooks or controller hooks");
        failed=D3DService::CaptureResources() || D3DService::Initialize();
    } else {
        failed=!ValidateGameAddresses() || D3DService::CaptureResources() ||
            OpenXRService::Initialize() || ActionsService::Initialize() ||
            BF2Service::Initialize() || D3DService::Initialize();
    }
    if(failed) { Logging::Log("[CORE] Startup failed; rolling back allocated resources");Shutdown();return 1; }
    {
        std::lock_guard lock(Runtime::mutex);
        if((!Runtime::desktopAuditOnly || Runtime::hudAnchorsEnabled) && GetFileAttributesW((folder/L"DisableHudTransform.flag").c_str())==INVALID_FILE_ATTRIBUTES) {
            std::ifstream shaderFile(folder/L"HudTransform.hlsl");
            std::string shader{std::istreambuf_iterator<char>(shaderFile),{}};
            if(shader.empty() || shader.size()>65536)shader=R"(Texture2D image:register(t0);SamplerState linearClamp:register(s0);
struct V{float4 position:SV_Position;float2 uv:TEXCOORD0;};
float4 PS(V v):SV_Target{float2 p=(v.uv-.5)/.25+.5;if(any(p<0)||any(p>1))return 0;return image.Sample(linearClamp,p);})";
            D3DService::ConfigureStartupHud(shader);
        }
    }
    if(Runtime::desktopAuditOnly && GetFileAttributesW((folder/L"ObserveMarkerCamera.flag").c_str())!=INVALID_FILE_ATTRIBUTES)MarkerCameraAudit::Initialize();
    Runtime::enabled=true;
    Logging::Log("[CORE] Ready. END or StopMod.flag disables the mod. Full restart required to reload.");
    const auto captureOnlyFlag = std::filesystem::path(modulePath).parent_path() / L"CaptureOnly.flag";
    const auto nativeVisualsFlag = std::filesystem::path(modulePath).parent_path() / L"NativeVisuals.flag";
    std::string lastHudSelection;
    std::string lastScaleText;
    unsigned controlPoll = 0;
    bool previousRunning = false;
    while (!(GetAsyncKeyState(VK_END) & 0x8000)) {
        const bool optionsChanged=VrOptions::Update();
        if (++controlPoll >= 50 || optionsChanged) {
            controlPoll = 0;
            ReadProjectionScale(folder);
            int grenadeSlot=1;char grenadeTrailing=0;
            std::ifstream grenadeFile(folder/L"QuestGrenadeSlot.txt");
            if(!(grenadeFile>>grenadeSlot) || (grenadeFile>>grenadeTrailing) ||
                grenadeSlot<1 || grenadeSlot>3)grenadeSlot=1;
            Runtime::grenadeSlot=grenadeSlot;
            ApplyHudScaleFile(folder);
            float sharpness=0.f;char extra=0;
            std::ifstream sharpnessFile(folder/L"EyeSharpness.txt");
            if(!(sharpnessFile>>sharpness) || (sharpnessFile>>extra) || !std::isfinite(sharpness) ||
                sharpness<0.f || sharpness>.5f)sharpness=0.f;
            Runtime::eyeSharpness=sharpness;
            const auto stopMarker=folder/L"StopMod.flag";
            if(GetFileAttributesW(stopMarker.c_str())!=INVALID_FILE_ATTRIBUTES && DeleteFileW(stopMarker.c_str()))break;
            const bool anchors=GetFileAttributesW((folder/L"EnableHudAnchors.flag").c_str())!=INVALID_FILE_ATTRIBUTES;
            if(Runtime::hudAnchorsEnabled.exchange(anchors)!=anchors)Logging::Log(anchors?"[HUD_ANCHOR] Experimental placement requested":"[HUD_ANCHOR] Native producer placement restored");
            int markerMode=1;std::ifstream markerModeFile(folder/L"MarkerProjectionMode.txt");
            if(!(markerModeFile>>markerMode)||(markerMode!=0 && markerMode!=1))markerMode=1;
            if(Runtime::hudMarkerReprojection.exchange(markerMode==1)!=(markerMode==1))Logging::Log(markerMode?"[HUD_ANCHOR] Character-camera marker reprojection":"[HUD_ANCHOR] Preserve native marker screen positions");
            if(Runtime::desktopAuditOnly) {
                float x=0,y=0;std::ifstream input(folder/L"AimOffsetTest.txt");
                if(!(input>>x>>y)||!std::isfinite(x)||!std::isfinite(y)||std::abs(x)>1||std::abs(y)>1)x=y=0;
                Runtime::hudTestAimX=x;Runtime::hudTestAimY=y;
            }
            const auto markerGeometry=folder/L"CaptureMarkerGeometry.flag";
            if(Runtime::desktopAuditOnly && GetFileAttributesW(markerGeometry.c_str())!=INVALID_FILE_ATTRIBUTES && DeleteFileW(markerGeometry.c_str())) {
                MarkerGeometryAudit::Request(folder/(L"MarkerGeometry-"+std::to_wstring(GetTickCount64())));
                Logging::Log("[MARKER_GEOMETRY] Requested one desktop UI frame; bounded buffer readback may briefly stall");
            }
            const auto markerCamera=folder/L"CaptureMarkerCamera.flag";
            if(GetFileAttributesW(markerCamera.c_str())!=INVALID_FILE_ATTRIBUTES && DeleteFileW(markerCamera.c_str()))MarkerCameraAudit::Request();
            const auto auditMarker=folder/L"CaptureHudPass.flag";
            if(GetFileAttributesW(auditMarker.c_str())!=INVALID_FILE_ATTRIBUTES && DeleteFileW(auditMarker.c_str())) {
                std::lock_guard lock(Runtime::mutex);
                D3DService::RequestHudAudit(folder/(L"HudAudit-"+std::to_wstring(GetTickCount64())+L".csv"));
            }
            const auto previewMarker=folder/L"CaptureHudResult.flag";
            if(Runtime::desktopAuditOnly && GetFileAttributesW(previewMarker.c_str())!=INVALID_FILE_ATTRIBUTES && DeleteFileW(previewMarker.c_str())) {
                std::lock_guard lock(Runtime::mutex);D3DService::RequestHudPreview(folder/(L"HudResult-"+std::to_wstring(GetTickCount64())));
            }
            const bool mapMenu=GetFileAttributesW((folder/L"DisableMenuInputMapping.flag").c_str())==INVALID_FILE_ATTRIBUTES &&
                (!Runtime::desktopAuditOnly || GetFileAttributesW((folder/L"EnableMenuInputMapping.flag").c_str())!=INVALID_FILE_ATTRIBUTES);
            float menuScale=.25f;std::ifstream menuFile(folder/L"MenuScale.txt");
            if(menuFile && (!(menuFile>>menuScale)||!std::isfinite(menuScale)||menuScale<.25f||menuScale>1.f))menuScale=.25f;
            MenuInput::Configure(mapMenu,menuScale);MenuInput::Report();
            const bool hudEnable=GetFileAttributesW((folder/L"DisableHudTransform.flag").c_str())==INVALID_FILE_ATTRIBUTES &&
                (!Runtime::desktopAuditOnly || GetFileAttributesW((folder/L"EnableHudTransform.flag").c_str())!=INVALID_FILE_ATTRIBUTES);
            { std::lock_guard lock(Runtime::mutex);D3DService::EnableHudTransform(hudEnable); }
            std::ifstream selectionFile(folder/L"HudSurfaceIndex.txt");
            std::string selectedText=selectionFile ? std::string(std::istreambuf_iterator<char>(selectionFile),{}) : "-1";
            if(selectionFile && selectedText!=lastHudSelection) {
                lastHudSelection=selectedText;std::istringstream input(selectedText);int index=-1;char extra=0;
                if(!(input>>index) || (input>>extra) || index<0 || index>7)index=-1;
                std::lock_guard lock(Runtime::mutex);D3DService::SelectHudSurface(index);
            }
            const auto reloadHud=folder/L"ReloadHudShader.flag";
            if(GetFileAttributesW(reloadHud.c_str())!=INVALID_FILE_ATTRIBUTES && DeleteFileW(reloadHud.c_str())) {
                std::ifstream shaderFile(folder/L"HudTransform.hlsl");
                std::string shaderCode{std::istreambuf_iterator<char>(shaderFile),{}};
                if(!shaderCode.empty() && shaderCode.size()<=65536) {
                    std::lock_guard lock(Runtime::mutex);D3DService::QueueHudShader(shaderCode);
                } else Logging::Log("[HUD_TRANSFORM] Shader missing or exceeds64KiB; previous retained");
            }
            if(!Runtime::desktopAuditOnly) {
            std::ifstream scaleInput(folder/L"WorldUnitsPerMeter.txt");
            std::string scaleText=scaleInput ? std::string(std::istreambuf_iterator<char>(scaleInput),{}) : "1.075";
            if(scaleText!=lastScaleText) {
                lastScaleText=scaleText;
                if(auto value=ParseWorldUnitsPerMeter(scaleText)) {
                    Runtime::worldUnitsPerMeter=*value;
                    Logging::Log("[SCALE] World units per tracked metre="+std::to_string(*value)+"; rotation/native camera unchanged");
                } else Logging::Log("[SCALE] Invalid WorldUnitsPerMeter.txt; keep previous value (allowed 0.5 to 2.0)");
            }
            const bool follow=GetFileAttributesW((folder/L"LegacyWorldCamera.flag").c_str())==INVALID_FILE_ATTRIBUTES;
            if(follow != Runtime::followCharacterCamera.load()) {
                std::lock_guard lock(Runtime::mutex);
                Runtime::followCharacterCamera=follow;BF2Service::ResetCameraAnchor();
                Logging::Log(follow ? "[CAMERA] Native camera follow enabled; legacy hand aiming disabled" : "[CAMERA] Legacy absolute headset camera enabled");
            }
            const auto recenterMarker=folder/L"RecenterCamera.flag";
            if(GetFileAttributesW(recenterMarker.c_str())!=INVALID_FILE_ATTRIBUTES && DeleteFileW(recenterMarker.c_str())) {
                std::lock_guard lock(Runtime::mutex);BF2Service::ResetCameraAnchor(true);
                Logging::Log("[CAMERA] Relative head reference will recenter on the next camera update");
            }
            const bool nativeFov=GetFileAttributesW((folder/L"HeadsetSubmissionFov.flag").c_str())==INVALID_FILE_ATTRIBUTES;
            if (nativeFov != Runtime::nativeSubmissionFov.load()) {
                std::lock_guard lock(Runtime::mutex);
                Runtime::nativeSubmissionFov=nativeFov;
                Logging::Log(nativeFov ? "[STEREO] Native-generator submission FOV comparison enabled" : "[STEREO] Headset submission FOV restored");
            }
            const auto captureMarker=folder/L"CaptureStereo.flag";
            if (GetFileAttributesW(captureMarker.c_str())!=INVALID_FILE_ATTRIBUTES && DeleteFileW(captureMarker.c_str())) {
                std::lock_guard lock(Runtime::mutex);
                D3DService::RequestStereoCapture(folder/(L"StereoCapture-"+std::to_wstring(GetTickCount64())));
            }
            const bool swap = GetFileAttributesW((folder / L"SwapCapturedEyes.flag").c_str()) != INVALID_FILE_ATTRIBUTES;
            if (swap != Runtime::swapCapturedEyes.load()) {
                std::lock_guard lock(Runtime::mutex);
                Runtime::swapCapturedEyes = swap;
                Logging::Log(swap ? "[STEREO] Completed eye images SWAPPED; pose/FOV metadata unchanged" :
                                   "[STEREO] Completed eye images NORMAL; pose/FOV metadata unchanged");
            }
            const bool camera = GetFileAttributesW(captureOnlyFlag.c_str()) == INVALID_FILE_ATTRIBUTES;
            const bool visual = camera && GetFileAttributesW(nativeVisualsFlag.c_str()) == INVALID_FILE_ATTRIBUTES;
            const bool pose = GetFileAttributesW((folder / L"NativePose.flag").c_str()) == INVALID_FILE_ATTRIBUTES;
            const bool projection = GetFileAttributesW((folder / L"NativeProjection.flag").c_str()) == INVALID_FILE_ATTRIBUTES;
            const bool culling = GetFileAttributesW((folder / L"NoForcedCulling.flag").c_str()) == INVALID_FILE_ATTRIBUTES;
            if (camera != Runtime::cameraEnabled.load() || visual != Runtime::visualEnabled.load() ||
                pose != Runtime::poseEnabled.load() || projection != Runtime::projectionEnabled.load() || culling != Runtime::cullingEnabled.load()) {
                std::lock_guard lock(Runtime::mutex);
                BF2Service::RestoreVisualSettings();
                Runtime::cameraEnabled = camera;
                Runtime::visualEnabled = visual;
                Runtime::poseEnabled = pose;
                Runtime::projectionEnabled = projection;
                Runtime::cullingEnabled = culling;
                Logging::Log(!camera ? "[PERF] MODE=CAPTURE_ONLY; disregard mixed timing window" :
                    visual ? "[PERF] MODE=VR_CAMERA; disregard mixed timing window" :
                             "[PERF] MODE=CAMERA_ONLY; native visual settings; disregard mixed timing window");
            }
            }
        }
        bool running = false;
        if(!Runtime::desktopAuditOnly) {
            { std::lock_guard lock(Runtime::mutex); if (OpenXRService::xrExiting) break; running = OpenXRService::xrRunning; }
            if (running != previousRunning) { RefreshWindowSize(); previousRunning = running; }
        }
        Sleep(20);
    }
    Shutdown();
    return 0;
}
}
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        // Keep module and trampolines alive until process exit; no hot FreeLibrary.
        HANDLE thread = CreateThread(nullptr, 0, BF2VR::MainThread, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}
