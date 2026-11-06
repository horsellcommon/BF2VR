#pragma once
#include <atomic>
#include <mutex>

namespace BF2VR {
// Hooks remain resident until process exit. Serialize shared engine/XR state,
// but never hold this lock while calling the original engine camera or Present.
struct Runtime {
    static inline std::recursive_mutex mutex;
    static inline std::atomic_bool enabled = false;
    static inline std::atomic_bool hudAnchorsEnabled=false;
    static inline std::atomic_bool hudMarkerReprojection=true;
    static inline std::atomic<float> hudTestAimX{0.f},hudTestAimY{0.f};
    static inline std::atomic_bool desktopAuditOnly = false;
    static inline std::atomic_bool cameraEnabled = true;
    static inline std::atomic_bool visualEnabled = true;
    static inline std::atomic_bool poseEnabled = true;
    static inline std::atomic_bool projectionEnabled = true;
    static inline std::atomic_bool cullingEnabled = true;
    static inline std::atomic_bool swapCapturedEyes = false;
    static inline std::atomic_bool nativeSubmissionFov = true;
    static inline std::atomic_bool followCharacterCamera = true;
    static constexpr float maxXrProjectionScale=2.f;
    static inline std::atomic<float> xrProjectionScale=1.f;
    static inline std::atomic<float> hudScale=.25f;
    static inline std::atomic<float> eyeSharpness = 0.f;
    static inline std::atomic<float> worldUnitsPerMeter{1.075f};
};
}
