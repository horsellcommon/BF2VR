#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace BF2VR {
enum class GrenadeSlot { Left=1, Middle=2, Right=3 };

struct QuestGamepad {
    static constexpr uint32_t Jump=0x20, Reload=0x40, Sprint=0x400, Perspective=0x800;
    static constexpr uint32_t LeftAbility=0x10000, RightAbility=0x20000;
    static constexpr uint32_t Mask=Jump|Reload|Sprint|Perspective|LeftAbility|RightAbility;
    static uint32_t Buttons(bool jump,bool reload,bool sprint,bool perspective,
                            bool left,bool right,bool grenade,GrenadeSlot slot) {
        if(grenade) {
            left=left || slot!=GrenadeSlot::Right;
            right=right || slot!=GrenadeSlot::Left;
        }
        return (jump?Jump:0)|(reload?Reload:0)|(sprint?Sprint:0)|(perspective?Perspective:0)|
            (left?LeftAbility:0)|(right?RightAbility:0);
    }
    static float Trigger(float value) {
        return std::isfinite(value)?std::clamp(value,0.f,1.f):0.f;
    }
    static float Axis(float value) {
        return std::isfinite(value)?std::clamp(value,-1.f,1.f):0.f;
    }
};

class PerspectiveClick {
    using Clock=std::chrono::steady_clock;
    Clock::time_point until{};
    bool pressed=false;
public:
    bool Update(bool active,bool down,Clock::time_point now) {
        if(!active) {
            pressed=false;
            until={};
            return false;
        }
        // A short gamepad tap changes shoulders. Send a single native hold.
        if(down && !pressed && now>=until)until=now+std::chrono::milliseconds(900);
        pressed=down;
        return now<until;
    }
};
}
