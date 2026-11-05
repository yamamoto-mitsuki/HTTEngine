#META
stage: Include
#META_END

#HLSL
static const float PI = 3.14159265359f;

float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f;
}

float2 Hammersley(uint i, uint n)
{
    return float2(float(i) / float(n), RadicalInverse_VdC(i));
}

float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness)
{
    float a = roughness * roughness;
    float phi = 2.0f * PI * Xi.x;
    float cosT = sqrt((1.0f - Xi.y) / (1.0f + (a * a - 1.0f) * Xi.y));
    float sinT = sqrt(1.0f - cosT * cosT);
    float3 H = float3(cos(phi) * sinT, sin(phi) * sinT, cosT);
    float3 up = abs(N.z) < 0.999f ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, N));
    float3 bitan = cross(N, tangent);
    return normalize(tangent * H.x + bitan * H.y + N * H.z);
}
#HLSL_END