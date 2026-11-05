#META
stage:   VS
profile: vs_6_0
#META_END

#HLSL
struct Camera
{
    float4x4 viewProj;
};
ConstantBuffer<Camera> gCamera : register(b0);

static const float3 kCorners[8] =
{
    float3(-1, -1, -1), float3(1, -1, -1), float3(1, 1, -1), float3(-1, 1, -1),
    float3(-1, -1, 1), float3(1, -1, 1), float3(1, 1, 1), float3(-1, 1, 1),
};
// 6面×2三角形×3 = 36（Cull=NONEなので巻き順は不問）
static const uint kIndices[36] =
{
    0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6,
    4, 0, 3, 4, 3, 7, 1, 5, 6, 1, 6, 2,
    4, 5, 1, 4, 1, 0, 3, 2, 6, 3, 6, 7,
};

struct VertexOutput
{
    float4 pos : SV_Position;
    float3 dir : TEXCOORD0;
};


VertexOutput main(uint vid : SV_VertexID)
{
    float3 p = kCorners[kIndices[vid]];
    VertexOutput output;
    output.dir = p;
    output.pos = mul(float4(p, 1.0f), gCamera.viewProj);
    return output;
}
#HLSL_END