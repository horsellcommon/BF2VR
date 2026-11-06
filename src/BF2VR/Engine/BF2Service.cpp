// Copyright Ethan Porcaro

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.

#include <Engine/BF2Service.h>
#include <OpenXRService.h>
#include <Classes.h>
#include <Logging.h>
#include <ActionsService.h>
#include <D3DService.h>
#include <Runtime.h>
#include <Timing.h>
#include <CameraAnchor.h>
#include <AimProjection.h>
#include <unordered_map>

namespace BF2VR {
namespace {
struct WorldSnapshot { float aa; bool reflection, motion, sky; char outdoor; };
struct PostSnapshot { bool raytrace, lens; int32_t dof; float blur; };
std::unordered_map<WorldRenderSettings*, WorldSnapshot> worldSnapshots;
std::unordered_map<GlobalPostProcessSettings*, PostSnapshot> postSnapshots;
std::unordered_map<GameRenderSettings*, float> fovSnapshots;
struct ViewSnapshot { float aspect, width, height; };
std::unordered_map<RenderView*, ViewSnapshot> viewSnapshots;
CameraAnchor cameraAnchor;
RenderView* anchorOwner=nullptr;
Matrix4 hudNative{},hudEye{},hudProjection{},hudSourceProjection{};bool hudCameraValid=false,hudProjectionValid=false;
}

RenderView* BF2Service::PrepareCamera(RenderView* a2) {
    Timing::Scope timing(Timing::CameraPrepare);
    RenderView* toReturn = a2;

    std::lock_guard lock(Runtime::mutex);
    if (!Runtime::enabled || !OpenXRService::xrRunning || !OpenXRService::poseValid) {
        return toReturn;
    }

    GameRenderer* gameRenderer = GameRenderer::GetInstance();
    if (!isValidPtr(gameRenderer)) {
        return toReturn;
    }
    if (!isValidPtr(gameRenderer->gameRenderSettings)) return toReturn;
    if (Runtime::visualEnabled) {
    fovSnapshots.try_emplace(gameRenderer->gameRenderSettings, gameRenderer->gameRenderSettings->forceFov);
    gameRenderer->gameRenderSettings->forceFov = 100;  // Fixes some occulsion culling stuff and reflections
    }

    // Wrong camera to update
    if (a2 != reinterpret_cast<void*>(gameRenderer->renderView)) {
        return toReturn;
    }

    static bool cameraLogged = false;
    if (!cameraLogged) { Logging::Log("[BF2] Main camera transform override reached"); cameraLogged = true; }
    if (Runtime::poseEnabled) {
    if (Runtime::followCharacterCamera) {
        if(anchorOwner!=a2) { cameraAnchor.Reset();anchorOwner=a2; }
        Matrix4 followed{};
        if(cameraAnchor.Apply(a2->transform,viewMatrix,headCenterMatrix,followed,Runtime::worldUnitsPerMeter.load())) {
            a2->transform=followed;inGame=true;hudEye=followed;hudCameraValid=cameraAnchor.Native(hudNative);
        } else hudCameraValid=false;
    } else {
    a2->transform = viewMatrix;

    // Get player position if player is even in the game
    bool soldierFound = false;
    GameContext* gameContext = GameContext::GetInstance();
    if (isValidPtr(gameContext)) {
        // Game context valid
        ClientPlayerManager* clientPlayerManager = gameContext->clientPlayerManager;
        if (isValidPtr(clientPlayerManager)) {
            // Player manager valid
            ClientPlayer* localPlayer = clientPlayerManager->localPlayer;
            if (isValidPtr(localPlayer)) {
                // Local player valid
                ClientControllableEntity* soldier = localPlayer->controlledcontrollable;
                if (isValidPtr(soldier)) {
                    // Solider valid
                    ClientSoldierPrediction* soldierPrediction = soldier->clientSoldierPrediction;

                    if (isValidPtr(soldierPrediction)) {
                        a2->transform.o.x += soldierPrediction->location.x - .2f;  // 3rd person offset
                        a2->transform.o.y += soldierPrediction->location.y - soldier->HeightOffset + 3.1f;
                        a2->transform.o.z += soldierPrediction->location.z;

                        soldierFound = true;
                    }
                }
            }
        }
    }
    inGame = soldierFound;
    }
    }

    if (Runtime::visualEnabled) {
    // Disable TAA and local reflections
    WorldRenderSettings* worldRenderSettings = WorldRenderSettings::GetInstance();
    if (isValidPtr(worldRenderSettings)) {
        worldSnapshots.try_emplace(worldRenderSettings, WorldSnapshot{worldRenderSettings->aaDisocclusionFactor,
            worldRenderSettings->localReflectionEnable, worldRenderSettings->motionBlurEnable,
            worldRenderSettings->skyLightingEnable, worldRenderSettings->outdoorLightEnable});
        if (worldRenderSettings->aaDisocclusionFactor != 1.f) {
            worldRenderSettings->aaDisocclusionFactor = 1.f;
        }
        if (worldRenderSettings->localReflectionEnable) {
            worldRenderSettings->localReflectionEnable = false;
        }
        if (worldRenderSettings->motionBlurEnable) {
            worldRenderSettings->motionBlurEnable = false;
        }
        if (worldRenderSettings->skyLightingEnable) {
            worldRenderSettings->skyLightingEnable = false;
        }
        if (worldRenderSettings->outdoorLightEnable) {
            worldRenderSettings->outdoorLightEnable = false;
        }
    }

    // Set post options for the user
    if (isValidPtr(postSettings)) {
        postSnapshots.try_emplace(postSettings, PostSnapshot{postSettings->ScreenSpaceRaytraceEnable,
            postSettings->LensDistortionAllowed, postSettings->forceDofEnable, postSettings->forceDofBlurFactor});
        postSettings->ScreenSpaceRaytraceEnable = false;
        postSettings->LensDistortionAllowed = false;
        postSettings->forceDofEnable = true;
        postSettings->forceDofBlurFactor = 0.f;
    }

    }

    if (!Runtime::poseEnabled || Runtime::followCharacterCamera) return toReturn;
    // Invalid or lost hand tracking must never drive the aimer.
    if (!ActionsService::handValid[1]) return toReturn;
    // Calculate aim angles
    const auto [hq0, hq1, hq2, hq3] = ActionsService::handLocations[1].pose.orientation;
    Vec3 aimEuler = eulerFromQuat({hq0, hq1, hq2, hq3});

    // Update aim angles
    LocalAimer* localAimer = LocalAimer::GetInstance();
    if (isValidPtr(localAimer)) {
        AimingComponentSwitch* viewAngleSwitch = localAimer->aimingComponentSwitch;
        if (isValidPtr(viewAngleSwitch)) {
            // Attempt to set the primary angle
            if (isValidPtr(viewAngleSwitch->primary)) {
                // The correct viewangle will start with 12 0xff bytes
                int i = 0;
                while (i < 12) {
                    if (viewAngleSwitch->primary->signature[i] != 0xFF) {
                        break;
                    }
                    i++;
                }
                if (i == 12) {
                    viewAngleSwitch->primary->pitch = aimEuler.z - 1.22173f;
                    viewAngleSwitch->primary->yaw = -aimEuler.y + 3.14159f;
                    return toReturn;
                }
            }

            // Attempt to set the secondary
            if (isValidPtr(viewAngleSwitch->secondary)) {
                // The correct viewangle will start with 12 0xff bytes
                int i = 0;
                while (i < 12) {
                    if (viewAngleSwitch->secondary->signature[i] != 0xFF) {
                        break;
                    }
                    i++;
                }
                if (i == 12) {
                    viewAngleSwitch->secondary->pitch = aimEuler.z - 1.22173f;
                    viewAngleSwitch->secondary->yaw = -aimEuler.y + 3.14159f;
                    return toReturn;
                }
            }
        }
    }

    // Aim was not set
    return toReturn;
}

void BF2Service::BuildViews(RenderView* view) {
    std::lock_guard lock(Runtime::mutex);
    if (!Runtime::enabled || !Runtime::cameraEnabled || !OpenXRService::xrRunning || !OpenXRService::poseValid) return;

    auto* renderer = GameRenderer::GetInstance();
    if (!isValidPtr(renderer) || view != renderer->renderView || !isValidPtr(view)) return;
    static bool projectionLogged = false;
    if (!projectionLogged) { Logging::Log("[BF2] Main camera projection override reached"); projectionLogged = true; }
    view->projectionMatrix = projMatrix;
    view->orthoWidth = static_cast<float>(OpenXRService::swapchainWidth);
    view->orthoHeight = static_cast<float>(OpenXRService::swapchainHeight);
    view->aspectRatio = static_cast<float>(OpenXRService::swapchainWidth) / static_cast<float>(OpenXRService::swapchainHeight);
}

void BF2Service::GamepadUpdate(GamepadState* state) {
    std::lock_guard lock(Runtime::mutex);
    if (!Runtime::enabled || !OpenXRService::xrRunning) return;
    if (!isValidPtr(state)) {
        return;
    }

    state->bitmask = 0x0000;
    if (ActionsService::jumpValue.currentState) {
        state->bitmask |= 0x0020;  // A
    }
    if (ActionsService::rollValue.currentState) {
        state->bitmask |= 0x0080;  // B
    }
    if (ActionsService::basicValue.currentState) {
        state->bitmask |= 0x0040;  // X
    }
    if (ActionsService::reloadValue.currentState) {
        state->bitmask |= 0x0010;  // Y
    }
    if (ActionsService::triggerValue[0].currentState > .8f) {
        state->bitmask |= 0x0400;  // L3
    }
    if (ActionsService::gripValue[0].currentState > .8f) {
        state->bitmask |= 0x10000;  // LB
    }
    if (ActionsService::gripValue[1].currentState > .8f) {
        state->bitmask |= 0x20000;  // RB
    }

    state->LeftThumb.x = ActionsService::walkValue.currentState.x;
    state->LeftThumb.y = ActionsService::walkValue.currentState.y;
    state->RT = ActionsService::triggerValue[1].currentState;
    state->RightThumb = {ActionsService::lookValue.currentState.x, ActionsService::lookValue.currentState.y};
}

namespace {
bool HasCurrentHudProjection() {
    return Runtime::cameraEnabled && Runtime::poseEnabled && Runtime::followCharacterCamera &&
        OpenXRService::xrRunning && OpenXRService::poseValid &&
        BF2Service::projectionEye == OpenXRService::RenderEye() &&
        BF2Service::projectionRevision == OpenXRService::poseRevision && hudCameraValid && hudProjectionValid;
}
const Matrix4& CurrentHudImageProjection() {
    return HudImageProjection(hudSourceProjection, hudProjection, Runtime::nativeSubmissionFov);
}
}

bool BF2Service::HudAim(Vec2& output) {
    std::lock_guard lock(Runtime::mutex);
    return HasCurrentHudProjection() &&
        CharacterAimNdc(hudNative, hudEye, CurrentHudImageProjection(), output);
}
bool BF2Service::HudReprojection(float* rows) {
    std::lock_guard lock(Runtime::mutex);
    return HasCurrentHudProjection() &&
        CharacterHudReprojection(hudNative, hudEye, hudSourceProjection, CurrentHudImageProjection(), rows);
}
void BF2Service::ResetCameraAnchor(bool recenterOnly) {
    hudCameraValid=hudProjectionValid=false;
    if(recenterOnly)cameraAnchor.Recenter();
    else { cameraAnchor.Reset();anchorOwner=nullptr; }
}

void BF2Service::RefreshMainView() {
    auto* renderer = GameRenderer::GetInstance();
    cachedMainView = isValidPtr(renderer) && isValidPtr(renderer->renderView) ? renderer->renderView : nullptr;
}

Matrix4* BF2Service::ProjectionOverride(RenderView* view, Matrix4* output, void* camera) {
    // Generate native depth first, outside the shared lock. The engine applies
    // its own depth conversion later; a complete reverse-Z XR matrix here
    // would be converted twice.
    Matrix4* result = projectionHook.call<Matrix4*>(view, output, camera);
    if (!Runtime::enabled || !Runtime::cameraEnabled || !Runtime::projectionEnabled || view != cachedMainView.load()) return result;
    std::lock_guard lock(Runtime::mutex);
    auto* renderer = GameRenderer::GetInstance();
    if (Runtime::enabled && Runtime::cameraEnabled && Runtime::projectionEnabled &&
        OpenXRService::xrRunning && OpenXRService::poseValid && isValidPtr(renderer) &&
        view == renderer->renderView && isValidPtr(view) && output && result == output) {
        PrepareCamera(view);
        XrFovf nativeFov{};
        if (fovFromProjection(*output,nativeFov)) OpenXRService::RecordNativeFov(nativeFov);
        hudSourceProjection=*output;
        *output = withXrFov(*output, projMatrix);
        hudProjection=*output;hudProjectionValid=true;
        projectionRevision = OpenXRService::poseRevision;
        projectionEye = OpenXRService::RenderEye();
        static bool logged = false;
        if (!logged) {
            Logging::Log("[BF2] XR FOV applied; native generator depth/W and view dimensions preserved");
            logged = true;
        }
    }
    return result;
}

int BF2Service::Initialize() {
    RefreshMainView();
    // The inspected routine takes (RenderView*, Matrix4* output, camera*) and
    // returns output. Reject a mismatched prologue instead of trusting the RVA.
    const unsigned char expected[]{0x49,0x89,0xe3,0x53,0x48,0x81,0xec,0xb0,0x00,0x00,0x00};
    const auto* address = reinterpret_cast<const void*>(OFFSETMAKEPROJECTION);
    if (!IsReadable(address, sizeof(expected)) || memcmp(address, expected, sizeof(expected)) != 0) {
        Logging::Log("[BF2] Projection-generator signature does not match inspected game build"); return -1;
    }
    auto result = safetyhook::InlineHook::create(reinterpret_cast<void*>(OFFSETMAKEPROJECTION),
        reinterpret_cast<void*>(ProjectionOverride), safetyhook::InlineHook::StartDisabled);
    if (!result) { Logging::Log("[BF2] Projection-generator hook creation failed", static_cast<int>(result.error().type)); return -1; }
    projectionHook = std::move(*result);
    if (auto enabled = projectionHook.enable(); !enabled) {
        Logging::Log("[BF2] Projection-generator hook enable failed", static_cast<int>(enabled.error().type)); return -1;
    }
    Logging::Log("[BF2] Projection-generator hook installed; late source-copy/epilogue overrides removed");
    const unsigned char earlyExpected[]{0x44,0x89,0xc0,0x49,0x89,0x6b,0x18,0x23,0x81,0xf0,0,0,0};
    auto* earlyAddress = reinterpret_cast<void*>(OFFSETEARLYVIEW);
    if (!IsReadable(earlyAddress, sizeof(earlyExpected)) || memcmp(earlyAddress, earlyExpected, sizeof(earlyExpected))) {
        Logging::Log("[BF2] Early view-builder signature mismatch"); return -1;
    }
    auto early = safetyhook::MidHook::create(earlyAddress, [](safetyhook::Context& ctx) {
        // Compare addresses only before the lock: never dereference the cached view.
        if (!Runtime::enabled || !Runtime::cameraEnabled || reinterpret_cast<RenderView*>(ctx.rcx) != cachedMainView.load()) return;
        std::lock_guard lock(Runtime::mutex);
        if (!Runtime::enabled || !Runtime::cameraEnabled || !OpenXRService::xrRunning || !OpenXRService::poseValid) return;
        auto* view = reinterpret_cast<RenderView*>(ctx.rcx);
        auto* renderer = GameRenderer::GetInstance();
        if (!isValidPtr(renderer) || view != renderer->renderView || !isValidPtr(view)) return;
        PrepareCamera(view);
        // Inspected engine bits: 0x1 rebuilds view/projection derivatives;
        // 0x8 rebuilds its culling view. Both dirty and requested masks must agree.
        const uint32_t mask = Runtime::cullingEnabled ? 0x9u : 0x1u;
        *reinterpret_cast<uint32_t*>(reinterpret_cast<unsigned char*>(view) + 0xf0) |= mask;
        ctx.r8 |= mask;
        static bool logged = false;
        if (!logged) { Logging::Log("[BF2] Main cached view marked for XR matrix rebuild before engine flag checks"); logged = true; }
    });
    if (!early) { Logging::Log("[BF2] Early view hook failed", static_cast<int>(early.error().type)); return -1; }
    earlyViewHook = std::move(*early);


    // Completed-frame capture scales the native game backbuffer into XR.
    // Changing only engine viewport dimensions to the XR texture dimensions
    // clips a 2704-high view inside a 1440-high desktop backbuffer.
    Logging::Log("[BF2] Native game viewport retained; XR dimensions used only for destination textures");

    safetyhook::MidHookFn postFn = [](safetyhook::Context& ctx) {
        std::lock_guard lock(Runtime::mutex);
        if (!Runtime::enabled) return;
        GlobalPostProcessSettings* settings = reinterpret_cast<GlobalPostProcessSettings*>(ctx.r8);
        if (isValidPtr(settings)) {
            postSettings = settings;
        }
    };
    postHook = safetyhook::create_mid(reinterpret_cast<void*>(OFFSETPOST), postFn);
    if (!postHook) {
        Logging::Log("[BF2] Post processing hook failed to install");
        return -1;
    }

    safetyhook::MidHookFn gamepadUpdateFn = [](safetyhook::Context& ctx) {
        GamepadUpdate(reinterpret_cast<GamepadState*>(ctx.rbx));
    };
    gamepadHook = safetyhook::create_mid(reinterpret_cast<void*>(OFFSETGAMEPADUPDATE), gamepadUpdateFn);
    if (!gamepadHook) {
        Logging::Log("[BF2] Gamepad hook failed to install.");
        return -1;
    }

    return 0;
}

void BF2Service::RestoreVisualSettings() {
    // Restore only the currently active view, just as for visual-setting owners.
    auto* active = GameRenderer::GetInstance();
    if (isValidPtr(active)) for (const auto& [view, original] : viewSnapshots) {
        if (active->renderView == view && isValidPtr(view)) {
            view->aspectRatio = original.aspect; view->orthoWidth = original.width; view->orthoHeight = original.height;
        }
    }
    viewSnapshots.clear();
    // Keep pass-through hooks alive: callbacks may have entered before disable.
    for (const auto& [settings, original] : fovSnapshots) if (isValidPtr(GameRenderer::GetInstance()) && GameRenderer::GetInstance()->gameRenderSettings == settings && isValidPtr(settings)) settings->forceFov = original;
    for (const auto& [settings, original] : worldSnapshots) if (WorldRenderSettings::GetInstance() == settings && isValidPtr(settings)) {
        settings->aaDisocclusionFactor = original.aa;
        settings->localReflectionEnable = original.reflection;
        settings->motionBlurEnable = original.motion;
        settings->skyLightingEnable = original.sky;
        settings->outdoorLightEnable = original.outdoor;
    }
    for (const auto& [settings, original] : postSnapshots) if (postSettings == settings && isValidPtr(settings)) {
        settings->ScreenSpaceRaytraceEnable = original.raytrace;
        settings->LensDistortionAllowed = original.lens;
        settings->forceDofEnable = original.dof;
        settings->forceDofBlurFactor = original.blur;
    }
    worldSnapshots.clear(); postSnapshots.clear(); fovSnapshots.clear(); postSettings = nullptr;
    auto* renderer = GameRenderer::GetInstance();
    if (isValidPtr(renderer) && isValidPtr(renderer->renderView))
        *reinterpret_cast<uint32_t*>(reinterpret_cast<unsigned char*>(renderer->renderView) + 0xf0) |= 0x9u;
}
int BF2Service::Uninitialize() {
    RestoreVisualSettings();
    return 0;
}
}  // namespace BF2VR
