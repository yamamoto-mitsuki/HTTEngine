#META
stage:   PS
profile: ps_6_0
#META_END

#HLSL
#include "IBLCommon.hlsli"

TextureCube<float4> gEnv : register(t0);
SamplerState gSampler : register(s0);

struct Params
{
    float4x4 viewProj; // VSが使う
    float roughness;   // PSが使う
    float3 pad;
};
ConstantBuffer<Params> gParams : register(b0);

struct VSOut
{
    float4 pos : SV_Position;
    float3 dir : TEXCOORD0;
};


float4 main(VSOut input) : SV_TARGET
{
    float3 N = normalize(input.dir);
    float3 R = N;
    float3 V = N;

    const uint SAMPLE_COUNT = 1024u;
    float3 color = float3(0, 0, 0);
    float totalWeight = 0.0f;

    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H = ImportanceSampleGGX(Xi, N, gParams.roughness);
        float3 L = normalize(2.0f * dot(V, H) * H - V);
        float NdotL = max(dot(N, L), 0.0f);
        if (NdotL > 0.0f)
        {
            color += gEnv.SampleLevel(gSampler, L, 0).rgb * NdotL;
            totalWeight += NdotL;
        }
    }
    return float4(color / totalWeight, 1.0f);
}
#HLSL_END