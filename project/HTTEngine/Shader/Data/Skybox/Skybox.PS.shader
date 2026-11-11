#META
stage:   PS
profile: ps_6_0
#META_END

#HLSL
#include "Buffers/Camera.hlsli"
#include "Fog/Fog.hlsli"
#include "Resources/Texture.hlsli"
#include "Resources/TextureCube.hlsli"

struct Skybox
{
    float4x4 world;
    float4x4 viewProj;
    float    intensity;
    uint     cubeIndex;
    float2   pad;
    // --- 背景の塗り方（C++ の SceneBackground。色は全部リニア）---
    float3   solidColor;       // SolidColor の色
    uint     backgroundType;   // 下の kBackground〜（C++ の BackgroundType の番号）
    float3   skyColor;         // Gradient の真上の色
    float    horizonWidth;     // Gradient の地平線の色が残る幅（見る向きの y。0.15 なら上下に約 9°）
    float3   horizonColor;     // Gradient の地平線の色
    float    padB;
    float3   groundColor;      // Gradient の真下の色
    float    padC;
};
ConstantBuffer<Skybox> gSkybox : register(b20);

// 背景の塗り方（C++ の BackgroundType と同じ番号）
static const uint kBackgroundSkybox = 0;
static const uint kBackgroundSolidColor = 1;
static const uint kBackgroundGradient = 2;

// 見る向きの上下（direction.y：真上 +1・真横 0・真下 -1）で、3色を混ぜる
// 地平線（y = 0）は Horizon の色。|y| が horizonWidth まで離れる間に、Sky（上）か Ground（下）の色へ移り、その先はずっと Sky・Ground
// 移る割合は smoothstep（0 と horizonWidth の両端で傾きが 0 のなめらかな曲線 3t²-2t³）。地平線の所で急に変わらないので、
// Horizon の色が帯として残り、上下へぼけて溶ける（|y| のべき乗だと、地平線のすぐそばで急に変わって境目の線に見える）
float3 CalcGradientColor(float3 direction)
{
    float  amount = smoothstep(0.0f, max(gSkybox.horizonWidth, 0.0001f), abs(direction.y));
    float3 farColor = (direction.y >= 0.0f) ? gSkybox.skyColor : gSkybox.groundColor;
    return lerp(gSkybox.horizonColor, farColor, amount);
}

struct VSOutput
{
    float4 pos : SV_Position;
    float3 dir : TEXCOORD0;
};

float4 main(VSOutput i) : SV_TARGET
{
    float3 direction = normalize(i.dir);
    // 1色・グラデーションの背景（Camera の Background・Scene ビューの Skybox を切ったとき）。霧は掛けない（Unity と同じ）
    switch (gSkybox.backgroundType)
    {
    case kBackgroundSolidColor:
        return float4(gSkybox.solidColor, 1.0f);
    case kBackgroundGradient:
        return float4(CalcGradientColor(direction), 1.0f);
    default:
        break; // 天球の絵（下）
    }
    float3 c = gTexturesCube[gSkybox.cubeIndex].Sample(gSampler, direction).rgb * gSkybox.intensity;
    c = ApplySkyFog(c, direction); // 霧（板の背景の下から空がのぞいても、霧の色・穴の底の暗い色になる）
    return float4(c, 1.0f);
}
#HLSL_END