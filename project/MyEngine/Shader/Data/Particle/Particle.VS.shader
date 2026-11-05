#META
stage: VS
profile : vs_6_0
#META_END

#HLSL
#include "Particle.hlsli"
#include "Buffers/Camera.hlsli"


// C++ の ParticleData（ShaderConstants.h）と同じ並び（96 バイト）
struct Particle
{
    float4x4 world; // 中心（4行目）と大きさ（1行目の長さ＝横・2行目の長さ＝縦）だけを使う
    float32_t4 color;
    float32_t3 direction; // 絵の右（+X）を向ける向き（ワールド。長さ 0 なら向けない）
    float rotation; // 画面の上で回す角度（ラジアン。正で左回り）
};
StructuredBuffer<Particle> gParticles : register(t0);

// これより短い向き（画面に写した長さの2乗）は、向きが決まらないとみなす（カメラへまっすぐ飛ぶ粒・止まった粒）
static const float kMinScreenDirectionSq = 1e-8f;


struct VertexShaderInput
{
    float32_t4 position : POSITION0;
    float32_t2 texcoord : TEXCOORD0;
};


VertexShaderOutput main(VertexShaderInput input, uint32_t instanceId : SV_InstanceID)
{
    VertexShaderOutput output;
    Particle particle = gParticles[instanceId];
    float4x4 world = particle.world;
    // world から中心位置とスケールを取り出す
    float3 center = float3(world._41, world._42, world._43);
    float sx = length(float3(world._11, world._12, world._13));
    float sy = length(float3(world._21, world._22, world._23));

    // --- 回す角度（Align To Velocity なら、進む向きを画面に写した向きの角度を足す）---
    // 画面の右・上（カメラの right・up）に写した長さが、画面の上での X・Y。atan2(Y, X) がその向きの角度（右が 0・左回りが正）
    float angle = particle.rotation;
    float2 screenDirection = float2(dot(particle.direction, gCamera.right), dot(particle.direction, gCamera.up));
    if (dot(screenDirection, screenDirection) > kMinScreenDirectionSq)
    {
        angle += atan2(screenDirection.y, screenDirection.x);
    }

    // --- 板の角を、大きさを掛けてから画面の上で回す（2D の回転：x' = x cos − y sin、y' = x sin + y cos）---
    // 回してから大きさを掛けると、横に伸ばした板（Stretch）が回った向きに伸びなくなるので、この順
    float2 corner = float2(input.position.x * sx, input.position.y * sy);
    float s;
    float c;
    sincos(angle, s, c);
    float2 rotated = float2(corner.x * c - corner.y * s, corner.x * s + corner.y * c);

    // カメラ基底でクアッドを組む（回した角を、カメラの右・上に乗せる）
    float3 worldPos = center + gCamera.right * rotated.x
                             + gCamera.up * rotated.y;
    output.position = mul(float4(worldPos, 1.0f), gCamera.viewProj);
    output.texcoord = input.texcoord;
    output.color = particle.color;
    output.worldPosition = worldPos;
    return output;
}
#HLSL_END