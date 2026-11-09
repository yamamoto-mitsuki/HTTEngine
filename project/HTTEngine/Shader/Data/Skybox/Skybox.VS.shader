#META
stage:   VS
profile: vs_6_0
#META_END

#HLSL
struct Skybox
{
    float4x4 world;     // 天球の回転
    float4x4 viewProj;  // カメラの回転のみ
    float    intensity;
    float3   pad;
};
ConstantBuffer<Skybox> gSkybox : register(b20);

static const float3 kCorners[8] =
{
    float3(-1,-1,-1), float3(1,-1,-1), float3(1,1,-1), float3(-1,1,-1),
    float3(-1,-1, 1), float3(1,-1, 1), float3(1,1, 1), float3(-1,1, 1),
};
static const uint kIndices[36] =
{
    0,1,2, 0,2,3, 4,6,5, 4,7,6,
    4,0,3, 4,3,7, 1,5,6, 1,6,2,
    4,5,1, 4,1,0, 3,2,6, 3,6,7,
};

struct VSOutput
{
    float4 pos : SV_Position;
    float3 dir : TEXCOORD0;
};


VSOutput main(uint vid : SV_VertexID)
{
    float3 p  = kCorners[kIndices[vid]];
    float3 wp = mul(float4(p, 1.0f), gSkybox.world).xyz; // 回転を反映
    VSOutput o;
    o.dir = wp;                                           // Cubeのサンプル方向
    o.pos = mul(float4(wp, 1.0f), gSkybox.viewProj).xyww; // z=w → 深度1.0(最遠)
    return o;
}
#HLSL_END