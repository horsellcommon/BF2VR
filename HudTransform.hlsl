Texture2D image : register(t0); SamplerState linearClamp : register(s0);
struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
float4 PS(Vertex v) : SV_Target {
    float2 uv=(v.uv-0.5)/0.25+0.5;
    if(any(uv<0)||any(uv>1)) return float4(0,0,0,0);
    return image.Sample(linearClamp,uv);
}


