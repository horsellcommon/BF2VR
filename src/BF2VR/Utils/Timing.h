#pragma once
#include <Logging.h>
#include <Runtime.h>
#include <array>
#include <atomic>
#include <chrono>
#include <format>
namespace BF2VR {
struct Timing {
    enum Tag { LockWait, GameGap, CopyCPU, CopyGPU, WaitFrame, AcquireImage, WaitImage, EndXRFrame, DesktopPresent, CameraPrepare, Count };
    struct Stat { std::atomic_uint64_t count{0}, total{0}, maximum{0}; };
    static inline std::array<Stat, Count> stats{};
    using Clock = std::chrono::steady_clock;
    static void Add(Tag tag, uint64_t ns) {
        auto& s = stats[tag]; s.count.fetch_add(1); s.total.fetch_add(ns);
        auto maximum = s.maximum.load();
        while (maximum < ns && !s.maximum.compare_exchange_weak(maximum, ns)) {}
    }
    static void Elapsed(Tag tag, Clock::time_point start) {
        Add(tag, static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count()));
    }
    struct Scope {
        Tag tag; Clock::time_point start = Clock::now();
        explicit Scope(Tag value) : tag(value) {}
        ~Scope() { Elapsed(tag, start); }
    };
    static void Report() {
        static auto last = Clock::now();
        const auto now = Clock::now();
        if (now-last < std::chrono::seconds(5)) return;
        last = now;
        static constexpr const char* names[]{"lock_wait", "game_present_interval", "copy_cpu", "copy_gpu", "xr_wait_frame", "xr_acquire", "xr_wait_image", "xr_end_frame", "desktop_present", "camera_prepare"};
        Logging::Log(Runtime::desktopAuditOnly ? "[PERF] WINDOW MODE=DESKTOP_AUDIT" : !Runtime::cameraEnabled ? "[PERF] WINDOW MODE=CAPTURE_ONLY" : Runtime::visualEnabled ? "[PERF] WINDOW MODE=VR_CAMERA" : "[PERF] WINDOW MODE=CAMERA_ONLY");
        Logging::Log(std::format("[PERF] CONFIG camera={} visuals={} pose={} projection={} force_culling={} swap_images={} camera_follow={} world_units_per_meter={:.3f}", Runtime::cameraEnabled.load(), Runtime::visualEnabled.load(), Runtime::poseEnabled.load(), Runtime::projectionEnabled.load(), Runtime::cullingEnabled.load(), Runtime::swapCapturedEyes.load(), Runtime::followCharacterCamera.load(), Runtime::worldUnitsPerMeter.load()));
        Logging::Log("[PERF] ~5s window; CPU durations include waiting; game_present_interval is entry-to-entry wall time, including all stages; GPU copy is asynchronous timestamp measurement");
        for (int i=0; i<Count; ++i) {
            auto n=stats[i].count.exchange(0), total=stats[i].total.exchange(0), max=stats[i].maximum.exchange(0);
            if (n) Logging::Log(std::format("[PERF] {} n={} avg={:.3f}ms max={:.3f}ms", names[i], n,
                static_cast<double>(total)/n/1e6, static_cast<double>(max)/1e6));
        }
    }
};
}
