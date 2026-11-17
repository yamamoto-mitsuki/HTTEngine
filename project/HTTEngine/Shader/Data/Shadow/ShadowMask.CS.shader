#META
stage: CS
profile: cs_6_0
#META_END


#HLSL
// ===== 影の量の画面を作る（1スレッド = 3Dの絵の1ピクセル）=====
#include "Resources/Texture.hlsli"
#include "Buffers/Camera.hlsli"

struct ShadowMaskParams
{
    float4x4 lightViewProj; // ワールド → ライトから見た画面
    uint depthIndex;        // 3Dの絵の深度
    uint shadowMapIndex;    // 影の深度の絵
    uint shadowMapSize;     // 影の深度の絵の1辺
    float depthBias;        // 比べる前に引く深度（ライトの深度の0~1で）
    uint2 maskSize;         // 影の量の画面の大きさ（ピクセル）
    float strength;         // 影の濃さ
    float distance;         // カメラからこの距離まで影を出す
};
ConstantBuffer<ShadowMaskParams> gShadowMask : register(b1);

RWTexture2D<float> gOutput : register(u0); // 影の量の画面

[numthreads(8, 8, 1)]
void main(uint32_t3 dispatchThreadId : SV_DispatchThreadID)
{
    uint32_t2 pixel = dispatchThreadId.xy;
    // 最後のグループには、絵の外のスレッドがいるので何もせず帰る
    if (any(pixel >= gShadowMask.maskSize))
    {
        return;
    }
    // --- 1. 深度から、ワールドの位置へ戻す ---
    float depth = gTextures[gShadowMask.depthIndex].Load(int32_t3(pixel, 0)).r;
    if (depth >= 1.0f)
    {
        gOutput[pixel] = 1.0f; // 何も描いていないところ
        return;
    }
    float2 uv = (float2(pixel) + 0.5f) / float2(gShadowMask.maskSize); // ピクセルの真ん中
    float4 ndc = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    float4 world = mul(ndc, gCamera.inverseViewProj);
    float3 worldPosition = world.xyz / world.w;
    // --- 2. ライトから見た画面のどこに映るか ---
    float4 lightClip = mul(float4(worldPosition, 1.0f), gShadowMask.lightViewProj);
    float2 shadowUV = float2(lightClip.x * 0.5f + 0.5f, 0.5f - lightClip.y * 0.5f);
    // 箱の外は影の深度が無いので、光が届くことにする
    if (any(shadowUV < 0.0f) || any(shadowUV >= 1.0f) || lightClip.z > 1.0f)
    {
        gOutput[pixel] = 1.0f;
        return;
    }
    // --- 3. 比べる。ライトから見て自分より手前に物があれば影 ---
    uint32_t2 texel = min(uint32_t2(shadowUV * gShadowMask.shadowMapSize), gShadowMask.shadowMapSize - 1);
    float casterDepth = gTextures[gShadowMask.shadowMapIndex].Load(int32_t3(texel, 0)).r;
    float lit = (lightClip.z - gShadowMask.depthBias <= casterDepth) ? 1.0f : 0.0f;
    // --- 4. 濃さと遠くで薄めて消す ---
    float shadow = (1.0f - lit) * gShadowMask.strength;
    float fadeStart = gShadowMask.distance * 0.9f;
    float toCamera = length(worldPosition - gCamera.worldPosition);
    shadow *= 1.0f - saturate((toCamera - fadeStart) / max(gShadowMask.distance - fadeStart, 0.0001f));
    gOutput[pixel] = 1.0f - shadow;
}
#HLSL_END