#pragma once
#include <CameraAnchor.h>
#include <algorithm>
#include <optional>

namespace BF2VR {
// Aiming the right controller on entry establishes the neutral direction.
class QuestPointer {
    bool calibrated=false,armed=false;
    float yaw0=0.f,pitch0=0.f;
public:
    struct Sample {float x,y;bool click;};
    void Reset() {calibrated=armed=false;}
    std::optional<Sample> Update(const Vec4& backward,bool down,float scale) {
        if(!std::isfinite(backward.x) || !std::isfinite(backward.y) || !std::isfinite(backward.z)) {
            Reset();return {};
        }
        const float yaw=std::atan2(-backward.x,backward.z);
        const float pitch=std::atan2(-backward.y,std::hypot(backward.x,backward.z));
        if(!calibrated) {yaw0=yaw;pitch0=pitch;calibrated=true;}
        if(!down)armed=true;
        const float turn=std::remainder(yaw-yaw0,6.2831853f);
        const float x=std::clamp(.5f+turn*scale/1.2f,.001f,.999f);
        const float y=std::clamp(.5f-(pitch-pitch0)*scale/.8f,.001f,.999f);
        return Sample{x,y,armed && down};
    }
};
}
