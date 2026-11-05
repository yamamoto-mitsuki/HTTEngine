#META
stage:   PS
profile: ps_6_0
#META_END

#HLSL
Texture2D<float4> gEquirect : register(t0);
SamplerState gSampler : register(s0);

struct VertexOutput
{
    float4 pos : SV_Position;
    float3 dir : TEXCOORD0;
};


static const float2 invAtan = float2(0.1591549f, 0.3183099f);
float2 DirToEquirectUV(float3 d)
{
    float2 uv = float2(atan2(d.z, d.x), -asin(d.y));
    uv *= invAtan;
    uv += 0.5f;
    return uv;
}

float4 main(VertexOutput output) : SV_TARGET
{
    float3 dir = normalize(output.dir);
    float2 uv = DirToEquirectUV(dir);
    return float4(gEquirect.Sample(gSampler, uv).rgb, 1.0f);
}
#HLSL_END