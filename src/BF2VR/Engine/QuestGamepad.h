#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace BF2VR {
struct QuestGamepad {
    static constexpr uint32_t Jump=0x20, Crouch=0x80, Sprint=0x400;
    static constexpr uint32_t LeftAbility=0x10000, RightAbility=0x20000;
    static constexpr uint32_t Mask=Jump|Crouch|Sprint|LeftAbility|RightAbility;
    static uint32_t Buttons(bool jump,bool sprint,bool left,bool right,bool crouch) {
        return (jump?Jump:0)|(sprint?Sprint:0)|(left?LeftAbility:0)|(right?RightAbility:0)|
            (crouch?Crouch:0);
    }
    static float Trigger(float value) {
        return std::isfinite(value)?std::clamp(value,0.f,1.f):0.f;
    }
    static float Axis(float value) {
        return std::isfinite(value)?std::clamp(value,-1.f,1.f):0.f;
    }
};

class QuestPress {
    bool armed=false;
public:
    bool Update(bool active,bool pressed) {
        if(!active) {armed=false;return false;}
        if(!pressed) {armed=true;return false;}
        const bool click=armed;
        armed=false;
        return click;
    }
};

class QuestHold {
    bool armed=false;
public:
    bool Update(bool active,bool pressed) {
        if(!active)armed=false;
        else if(!pressed)armed=true;
        return active && pressed && armed;
    }
};

class QuestMouseLook {
    float carryX=0.f,carryY=0.f;
public:
    struct Delta { int x=0,y=0; };
    Delta Update(bool allowed,float x,float y,float seconds,float speed) {
        x=QuestGamepad::Axis(x);
        y=QuestGamepad::Axis(y);
        const float length=std::hypot(x,y);
        constexpr float deadzone=.16f;
        if(!allowed || length<=deadzone || !std::isfinite(seconds) || seconds<=0.f ||
            !std::isfinite(speed) || speed<=0.f) {
            carryX=carryY=0.f;
            return {};
        }
        const float strength=(std::min(length,1.f)-deadzone)/(1.f-deadzone);
        const float scale=strength*strength*std::min(seconds,.035f)*speed/length;
        carryX+=x*scale;
        carryY-=y*scale;
        const Delta move{static_cast<int>(carryX),static_cast<int>(carryY)};
        carryX-=static_cast<float>(move.x);
        carryY-=static_cast<float>(move.y);
        return move;
    }
};

}
