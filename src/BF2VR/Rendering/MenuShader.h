#pragma once
#include <string>
namespace BF2VR {
inline std::string BuildMenuShader(float factor) {
    return R"(Texture2D image:register(t0);SamplerState linearClamp:register(s0);
cbuffer MenuCursor:register(b0){float4 pointer;};
struct V{float4 position:SV_Position;float2 uv:TEXCOORD0;};
float4 PS(V v):SV_Target {
    float2 p=(v.uv-.5)/)"+std::to_string(factor)+R"(+.5;
    float4 colour=0;
    if(all(p>=0)&&all(p<=1))colour=image.Sample(linearClamp,p);
    if(pointer.x>=0 && pointer.y>=0 && pointer.z>0 && pointer.w>0) {
        float2 q=(v.uv-pointer.xy)/pointer.zw;
        if(q.x>=0 && q.y>=0 && q.y<=1 && q.x<=.7*q.y) {
            bool border=q.x<.07 || q.y>.93 || q.x>.7*q.y-.07;
            return border?float4(0,0,0,1):float4(1,1,1,1);
        }
    }
    return colour;
})";
}
}
