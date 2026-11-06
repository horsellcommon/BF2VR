#include <QuestGamepad.h>
#include <FrameCycle.h>
#include <Types.h>
#include <CameraAnchor.h>
#include <AimProjection.h>
#include <WorldScale.h>
#include <limits>
#include <cmath>
#include <iostream>
#include <stdexcept>
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int main() {
    try {
        BF2VR::FrameCycle cycle;
        for (int frame = 0; frame < 100; ++frame) {
            require(cycle.eye == 0 && !cycle.begun, "pair starts at left");
            cycle.begun = cycle.acquired = true;
            require(cycle.eye == 0, "left ownership before release");
            require(!cycle.ReleaseEye() && cycle.eye == 1 && cycle.begun && !cycle.acquired, "left release advances to right");
            cycle.acquired = true;
            require(cycle.eye == 1, "right ownership before release");
            require(cycle.ReleaseEye() && cycle.eye == 0 && !cycle.begun && !cycle.acquired, "right completes pair");
        }
        cycle.eye = 1; cycle.begun = cycle.acquired = true; cycle.Reset();
        require(cycle.eye == 0 && !cycle.begun && !cycle.acquired, "restart resets ownership");
        XrView view{XR_TYPE_VIEW}; view.pose.orientation.w = 1; view.pose.position = {1,2,3};
        auto matrix = fromOpenXR(view);
        require(matrix.x.x == 1 && matrix.y.y == 1 && matrix.z.z == 1 && matrix.o.w == 1, "identity rotation");
        require(matrix.o.x == 1 && matrix.o.y == 2 && matrix.o.z == 3, "position preserved");
        auto projection = fromFOV(-.7f,.7f,.7f,-.7f);
        require(std::isfinite(projection.x.x) && projection.x.x > 0 && projection.z.x == 0, "symmetric projection");
        // Native generator forward depth becomes reversed depth downstream.
        // Verify near/far behavior survives an asymmetric XR FOV replacement.
        Matrix4 native{};
        const float nearPlane = .06f, farPlane = 1000.f;
        native.x.x = .8f; native.y.y = 1.9f;
        native.z.z = -farPlane/(farPlane-nearPlane);
        native.o.z = -farPlane*nearPlane/(farPlane-nearPlane);
        native.z.w = -1.f;
        const float left=-.9f, right=.65f, up=.8f, down=-.7f;
        auto xr = fromFOV(left,right,up,down);
        auto adapted = withXrFov(native, xr);
        require(adapted.z.z == native.z.z && adapted.o.z == native.o.z &&
                adapted.z.w == native.z.w && adapted.o.w == native.o.w,
                "XR FOV preserves native depth and homogeneous W");
        auto reversedDepth = [&](float distance) {
            float z = -distance;
            float clipZ = z*adapted.z.z + adapted.o.z;
            float clipW = z*adapted.z.w + adapted.o.w;
            return (clipW-clipZ)/clipW;
        };
        require(std::abs(reversedDepth(nearPlane)-1.f)<1e-5f &&
                std::abs(reversedDepth(farPlane))<1e-5f,
                "engine conversion maps near to one and far to zero");
        auto horizontal = [&](float angle) { return std::tan(angle)*adapted.x.x-adapted.z.x; };
        auto vertical = [&](float angle) { return std::tan(angle)*adapted.y.y-adapted.z.y; };
        require(std::abs(horizontal(left)+1.f)<1e-5f && std::abs(horizontal(right)-1.f)<1e-5f &&
                std::abs(vertical(down)+1.f)<1e-5f && std::abs(vertical(up)-1.f)<1e-5f,
                "asymmetric headset frustum boundaries map to clip edges");
        XrFovf recovered{};
        require(fovFromProjection(xr,recovered) && std::abs(recovered.angleLeft-left)<1e-5f &&
                std::abs(recovered.angleRight-right)<1e-5f && std::abs(recovered.angleUp-up)<1e-5f &&
                std::abs(recovered.angleDown-down)<1e-5f, "recover asymmetric FOV from projection");
        Matrix4 invalid{}; require(!fovFromProjection(invalid,recovered), "reject zero focal scales");
        auto pose=[](float yaw,float x,float y,float z) {
            XrView v{XR_TYPE_VIEW};v.pose.orientation={0,std::sin(yaw/2),0,std::cos(yaw/2)};
            v.pose.position={x,y,z};return fromOpenXR(v);
        };
        BF2VR::CameraAnchor anchor;
        const auto base=pose(1.5707963f,10,2,8), center=pose(.4f,1,1.7f,3);
        const auto eyeOffset=pose(0,-.032f,0,0);
        Matrix4 followed{};
        require(anchor.Apply(base,BF2VR::ComposeRigid(center,eyeOffset),center,followed), "native camera follow setup");
        require(BF2VR::SameRigid(followed,BF2VR::ComposeRigid(base,eyeOffset)),
                "initial headset height and orientation do not replace native camera");
        const auto relativeTurn=pose(.3f,.1f,0,0);
        const auto eyeNow=BF2VR::ComposeRigid(center,BF2VR::ComposeRigid(relativeTurn,eyeOffset));
        require(anchor.Apply(base,eyeNow,center,followed), "relative head turn");
        const auto expected=BF2VR::ComposeRigid(base,BF2VR::ComposeRigid(relativeTurn,eyeOffset));
        require(BF2VR::SameRigid(followed,expected), "head motion rotates and translates in native camera space");
        for(int i=0;i<20;++i) {
            auto candidate=followed;candidate.x.w=std::numeric_limits<float>::quiet_NaN();
            require(anchor.Apply(candidate,eyeNow,center,followed) && BF2VR::SameRigid(followed,expected),
                    "repeated camera callbacks and native padding do not compound head motion");
        }
        const auto turnedBase=pose(3.1415926f,15,4,-7);
        require(anchor.Apply(turnedBase,eyeNow,center,followed) &&
                BF2VR::SameRigid(followed,BF2VR::ComposeRigid(turnedBase,BF2VR::ComposeRigid(relativeTurn,eyeOffset))),
                "character turns and perspective camera-position changes are inherited");
        anchor.Recenter();
        const auto centerNow=BF2VR::ComposeRigid(center,relativeTurn);
        require(anchor.Apply(followed,eyeNow,centerNow,followed) &&
                BF2VR::SameRigid(followed,BF2VR::ComposeRigid(turnedBase,eyeOffset)),
                "recenter preserves native camera and eye separation");
        auto invalidCamera=turnedBase;invalidCamera.o.x=std::numeric_limits<float>::quiet_NaN();
        require(!anchor.Apply(invalidCamera,eyeNow,centerNow,followed), "invalid camera position rejected");
        BF2VR::CameraAnchor scaledAnchor; Matrix4 scaled{};
        require(scaledAnchor.Apply(base,BF2VR::ComposeRigid(center,eyeOffset),center,scaled), "scale reference setup");
        auto localExpected=BF2VR::ComposeRigid(relativeTurn,eyeOffset);
        localExpected.o.x*=1.05f;localExpected.o.y*=1.05f;localExpected.o.z*=1.05f;
        require(scaledAnchor.Apply(scaled,eyeNow,center,scaled,1.05f) &&
                BF2VR::SameRigid(scaled,BF2VR::ComposeRigid(base,localExpected)),
                "world scale affects relative room motion and eye baseline, preserving native camera");
        require(scaledAnchor.Apply(scaled,eyeNow,center,scaled,1.f) && BF2VR::SameRigid(scaled,expected),
                "live scale reset restores original pose without accumulating offsets");
        require(BF2VR::ParseWorldUnitsPerMeter(" 1.05\n").has_value() && !BF2VR::ParseWorldUnitsPerMeter("1.05 junk") &&
                !BF2VR::ParseWorldUnitsPerMeter("nan") && !BF2VR::ParseWorldUnitsPerMeter("0") && !BF2VR::ParseWorldUnitsPerMeter("3"),
                "world scale config validation");

        XrView aimIdentity{XR_TYPE_VIEW};aimIdentity.pose.orientation.w=1;
        auto aimCamera=fromOpenXR(aimIdentity);auto aimEye=aimCamera;Vec2 aimNdc{};
        auto aimProjection=fromFOV(-.785398163f,.785398163f,.785398163f,-.785398163f);
        require(BF2VR::CharacterAimNdc(aimCamera,aimEye,aimProjection,aimNdc) && std::abs(aimNdc.x)<1e-5f && std::abs(aimNdc.y)<1e-5f,"neutral headset keeps native character aim centered");
        XrView headTurn{XR_TYPE_VIEW};headTurn.pose.orientation={0,std::sin(.175f),0,std::cos(.175f)};aimEye=fromOpenXR(headTurn);
        require(BF2VR::CharacterAimNdc(aimCamera,aimEye,aimProjection,aimNdc) && std::abs(aimNdc.x-std::tan(.35f))<1e-4f,"reticle moves opposite headset view while character aim remains fixed");
        auto zoomProjection=aimProjection;zoomProjection.x.x*=3.23195f;zoomProjection.y.y*=3.23195f;Vec2 zoomAim{};
        require(BF2VR::CharacterAimNdc(aimCamera,aimEye,zoomProjection,zoomAim) && std::abs(zoomAim.x/aimNdc.x-3.23195f)<1e-4f,"aim mapping uses current zoom projection instead of fixed FOV");
        auto aimParent=fromOpenXR(headTurn);auto turnedAimEye=BF2VR::ComposeRigid(aimParent,aimEye);Vec2 turnedAim{};
        require(BF2VR::CharacterAimNdc(aimParent,turnedAimEye,aimProjection,turnedAim) && std::abs(turnedAim.x-aimNdc.x)<1e-5f,"character turns preserve relative headset aim offset");
        float hudMapping[12]{};
        require(BF2VR::CharacterHudReprojection(aimCamera,aimEye,aimProjection,zoomProjection,hudMapping),"character-relative marker mapping accepts current zoom projection");
        const float mappedCenter=hudMapping[2]/hudMapping[10];
        require(std::abs(mappedCenter-zoomAim.x)<1e-4f,"world marker at native aim center agrees with zoomed reticle direction");
        require(BF2VR::CharacterHudReprojection(aimCamera,aimCamera,aimProjection,aimProjection,hudMapping) && std::abs(hudMapping[0]-1)<1e-5f && std::abs(hudMapping[5]-1)<1e-5f && std::abs(hudMapping[10]-1)<1e-5f,"neutral marker reprojection preserves screen coordinates");
        auto headsetLeft=aimProjection,headsetRight=aimProjection;
        headsetLeft.z.x=-.24f;headsetRight.z.x=.24f;
        Vec2 leftAim{},rightAim{};
        require(BF2VR::CharacterAimNdc(aimCamera,aimCamera,
                    BF2VR::HudImageProjection(aimProjection,headsetLeft,true),leftAim) &&
                BF2VR::CharacterAimNdc(aimCamera,aimCamera,
                    BF2VR::HudImageProjection(aimProjection,headsetRight,true),rightAim) &&
                std::abs(leftAim.x)<1e-5f && std::abs(rightAim.x)<1e-5f,
                "symmetric submission does not inherit opposite headset lens offsets");
        require(&BF2VR::HudImageProjection(aimProjection,headsetRight,false)==&headsetRight &&
                &BF2VR::HudImageProjection(Matrix4{},headsetRight,true)==&headsetRight,
                "headset projection is used for headset submission or invalid native FOV");
        auto badAim=aimCamera;badAim.x.x=std::numeric_limits<float>::quiet_NaN();
        require(!BF2VR::CharacterAimNdc(badAim,aimEye,aimProjection,aimNdc),"invalid camera cannot drive aim overlay");
        float q = std::sqrt(.5f);
        require(eulerFromQuat({0,0,q,q}).x > 1.5f && eulerFromQuat({0,0,-q,q}).x < -1.5f, "Euler singularity signs");
        auto offset = rotateAround({.25f,.2f,-1.f}, {}, 0);
        require(offset.x == .25f && offset.y == .2f && offset.z == -1.f, "relative HUD offset");
        using Pad=BF2VR::QuestGamepad;
        using Slot=BF2VR::GrenadeSlot;
        require(Pad::Buttons(true,true,true,true,true,true,false,Slot::Left)==Pad::Mask,
                "Quest jump, grip reload, sprint, perspective and both abilities reach the gamepad");
        require(Pad::Buttons(false,false,false,false,false,false,true,Slot::Left)==Pad::LeftAbility &&
                Pad::Buttons(false,false,false,false,false,false,true,Slot::Right)==Pad::RightAbility &&
                Pad::Buttons(false,false,false,false,false,false,true,Slot::Middle)==(Pad::LeftAbility|Pad::RightAbility),
                "grenade shortcut selects the requested equipped ability slot");
        require(Pad::Buttons(false,false,false,false,false,false,false,Slot::Left)==0 &&
                Pad::Trigger(.6f)==.6f && Pad::Trigger(-1.f)==0.f && Pad::Trigger(2.f)==1.f &&
                Pad::Axis(-.6f)==-.6f && Pad::Axis(std::numeric_limits<float>::quiet_NaN())==0.f,
                "Quest analog controls retain valid values and sanitize invalid input");
        using namespace std::chrono_literals;
        BF2VR::PerspectiveClick viewClick;
        const auto time=std::chrono::steady_clock::now();
        require(viewClick.Update(true,true,time) && viewClick.Update(true,false,time+100ms) &&
                !viewClick.Update(true,false,time+901ms),
                "brief right stick click becomes a bounded native hold");
        require(viewClick.Update(true,true,time+1s) && !viewClick.Update(true,true,time+2s) &&
                !viewClick.Update(true,true,time+3s),
                "holding the physical stick does not repeat perspective changes");
        require(!viewClick.Update(false,false,time+3100ms) &&
                viewClick.Update(true,true,time+3200ms) && !viewClick.Update(false,false,time+3300ms),
                "loss of controller activity cancels the synthetic hold");
        std::cout << "Core regression checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

