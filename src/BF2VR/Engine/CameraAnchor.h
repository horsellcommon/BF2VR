#pragma once
#include <Types.h>
#include <cmath>
namespace BF2VR {
inline Vec4 RigidVector(const Matrix4& m,const Vec4& v) {
    return {m.x.x*v.x+m.y.x*v.y+m.z.x*v.z,
            m.x.y*v.x+m.y.y*v.y+m.z.y*v.z,
            m.x.z*v.x+m.y.z*v.y+m.z.z*v.z,0};
}
inline Matrix4 ComposeRigid(const Matrix4& parent,const Matrix4& local) {
    Matrix4 out{};out.x=RigidVector(parent,local.x);out.y=RigidVector(parent,local.y);out.z=RigidVector(parent,local.z);
    out.o=RigidVector(parent,local.o);out.o.x+=parent.o.x;out.o.y+=parent.o.y;out.o.z+=parent.o.z;out.o.w=1;
    return out;
}
inline Matrix4 InverseRigid(const Matrix4& m) {
    Matrix4 out{};
    out.x={m.x.x,m.y.x,m.z.x,0};out.y={m.x.y,m.y.y,m.z.y,0};out.z={m.x.z,m.y.z,m.z.z,0};
    out.o=RigidVector(out,{-m.o.x,-m.o.y,-m.o.z,0});out.o.w=1;
    return out;
}
inline bool SameRigid(const Matrix4& a,const Matrix4& b) {
    const Vec4* av[]{&a.x,&a.y,&a.z,&a.o}; const Vec4* bv[]{&b.x,&b.y,&b.z,&b.o};
    for(int i=0;i<4;++i) if(std::abs(av[i]->x-bv[i]->x)>1e-5f || std::abs(av[i]->y-bv[i]->y)>1e-5f || std::abs(av[i]->z-bv[i]->z)>1e-5f) return false;
    return true;
}
inline bool ValidRigid(const Matrix4& m) {
    for(const auto* v:{&m.x,&m.y,&m.z,&m.o}) if(!std::isfinite(v->x)||!std::isfinite(v->y)||!std::isfinite(v->z))return false;
    for(const auto* v:{&m.x,&m.y,&m.z}) if(std::abs(v->x*v->x+v->y*v->y+v->z*v->z-1)>0.05f)return false;
    return std::abs(m.x.x*m.y.x+m.x.y*m.y.y+m.x.z*m.y.z)<0.05f &&
           std::abs(m.x.x*m.z.x+m.x.y*m.z.y+m.x.z*m.z.z)<0.05f &&
           std::abs(m.y.x*m.z.x+m.y.y*m.z.y+m.y.z*m.z.z)<0.05f;
}
// Use the unmodified game camera as the parent. Keep the last output so
// repeated engine callbacks do not treat our own result as a new parent.
class CameraAnchor {
    Matrix4 native{},last{},referenceInverse{};
    bool nativeValid=false,lastValid=false,referenceValid=false;
public:
    bool Native(Matrix4& output) const { if(!nativeValid)return false;output=native;return true; }
    void Reset() { nativeValid=lastValid=referenceValid=false; }
    void Recenter() { referenceValid=false; }
    bool Apply(const Matrix4& candidate,const Matrix4& eye,const Matrix4& center,Matrix4& output,float unitsPerMeter=1.f) {
        if(!std::isfinite(unitsPerMeter)||unitsPerMeter<.5f||unitsPerMeter>2.f)return false;
        if(!ValidRigid(candidate)||!ValidRigid(eye)||!ValidRigid(center))return false;
        if(!lastValid || !SameRigid(candidate,last)) { native=candidate;nativeValid=true; }
        if(!nativeValid)return false;
        if(!referenceValid) { referenceInverse=InverseRigid(center);referenceValid=true; }
        auto relative=ComposeRigid(referenceInverse,eye);
        relative.o.x*=unitsPerMeter;relative.o.y*=unitsPerMeter;relative.o.z*=unitsPerMeter;
        output=ComposeRigid(native,relative);
        last=output;lastValid=true;return true;
    }
};
}
