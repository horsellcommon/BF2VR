#pragma once
#include <CameraAnchor.h>
namespace BF2VR {
// HUD coordinates go into the same image that the compositor interprets
// using the chosen submission FOV. Keep the target intrinsics consistent.
inline const Matrix4& HudImageProjection(const Matrix4& native, const Matrix4& headset,
                                         bool useNativeSubmissionFov) {
    XrFovf fov{};
    return useNativeSubmissionFov && fovFromProjection(native, fov) ? native : headset;
}
inline bool CharacterHudReprojection(const Matrix4& native,const Matrix4& eye,const Matrix4& source,const Matrix4& rendered,float* rows) {
    if(!rows || !ValidRigid(native)||!ValidRigid(eye) || !std::isfinite(source.x.x)||!std::isfinite(source.y.y)||std::abs(source.x.x)<1e-5f||std::abs(source.y.y)<1e-5f||!std::isfinite(source.z.w)||std::abs(source.z.w+1)>1e-4f)return false;
    const auto inverse=InverseRigid(eye);
    const Vec4 rays[]{ {1/source.x.x,0,0,0},{0,1/source.y.y,0,0},{source.z.x/source.x.x,source.z.y/source.y.y,-1,0} };
    for(int i=0;i<3;++i) {
        const auto ray=RigidVector(inverse,RigidVector(native,rays[i]));
        const float x=rendered.x.x*ray.x+rendered.y.x*ray.y+rendered.z.x*ray.z;
        const float y=rendered.x.y*ray.x+rendered.y.y*ray.y+rendered.z.y*ray.z;
        const float w=rendered.x.w*ray.x+rendered.y.w*ray.y+rendered.z.w*ray.z;
        if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(w))return false;
        rows[i]=x;rows[4+i]=y;rows[8+i]=w;
    }
    rows[3]=rows[7]=rows[11]=0;return true;
}
inline bool CharacterAimNdc(const Matrix4& native,const Matrix4& eye,const Matrix4& projection,Vec2& output) {
    if(!ValidRigid(native)||!ValidRigid(eye))return false;
    auto local=RigidVector(InverseRigid(eye),{-native.z.x,-native.z.y,-native.z.z,0});
    const float x=projection.x.x*local.x+projection.y.x*local.y+projection.z.x*local.z;
    const float y=projection.x.y*local.x+projection.y.y*local.y+projection.z.y*local.z;
    const float w=projection.x.w*local.x+projection.y.w*local.y+projection.z.w*local.z;
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(w))return false;
    if(w<=1e-5f) { output={8,8};return true; }
    output={x/w,y/w};return std::isfinite(output.x)&&std::isfinite(output.y);
}
}
