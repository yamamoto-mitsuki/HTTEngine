#META
stage:   PS
profile: ps_6_0
#META_END


#HLSL
#include "IBLCommon.hlsli"

// IBL用のGeometry項（kが直接光と違う）
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float k = (roughness * roughness) / 2.0f;
    return NdotV / (NdotV * (1.0f - k) + k);
}
float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

float2 IntegrateBRDF(float NdotV, float roughness)
{
    float3 V = float3(sqrt(1.0f - NdotV * NdotV), 0.0f, NdotV);
    float3 N = float3(0, 0, 1);
    float A = 0.0f, B = 0.0f;
    const uint SAMPLE_COUNT = 1024u;
    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H = ImportanceSampleGGX(Xi, N, roughness);
        float3 L = normalize(2.0f * dot(V, H) * H - V);
        float NdotL = max(L.z, 0.0f);
        float NdotH = max(H.z, 0.0f);
        float VdotH = max(dot(V, H), 0.0f);
        if (NdotL > 0.0f)
        {
            float G = GeometrySmith(NdotV, NdotL, roughness);
            float G_Vis = (G * VdotH) / (NdotH * NdotV);
            float Fc = pow(1.0f - VdotH, 5.0f);
            A += (1.0f - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    return float2(A, B) / SAMPLE_COUNT;
}

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};


float2 main(VSOut input) : SV_TARGET
{
    return IntegrateBRDF(input.uv.x, input.uv.y); // x=NdotV, y=roughness
}
#HLSL_END