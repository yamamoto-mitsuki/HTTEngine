#META
stage: Include
#META_END

#HLSL
// デカールの PS が使う共通の物（全部のデカールの Shader が include する）
// 箱は Transform の大きさ 1 で 1辺 1。PS は、そのピクセルに描かれた不透明な物の位置を深度から戻し、箱の中なら塗る
// "Buffers/Camera.hlsli" と "Resources/Texture.hlsli" をここで include するので、デカールの Shader では include しない（2回 include すると、同じ名前が2つになって止まる）
#include "Buffers/Camera.hlsli"
#include "Resources/Texture.hlsli"

// 箱（C++ の DecalData と同じ並び）
struct Decal
{
    float4x4 worldToDecal; // ワールド → 箱の中（中なら x・y・z が -0.5〜+0.5）
    uint32_t depthIndex;   // 不透明な物の深度（gTextures[] の番号）
    float3 padA;
};
ConstantBuffer<Decal> gDecal : register(b23);

// そのピクセルに描かれた不透明な物の位置
struct DecalSurface
{
    float32_t3 world; // ワールドの位置
    float32_t3 local; // 箱の中の位置（中なら -0.5〜+0.5。XZ が貼る面、Y が貼る向き）
};

// 深度から、そのピクセルに描かれた物の位置を戻す（svPosition は PS の SV_POSITION。xy がピクセルの真ん中）
DecalSurface GetDecalSurface(float32_t4 svPosition)
{
    // --- そのピクセルの深度（0〜1。一番手前の不透明な物）---
    int32_t2 pixel = int32_t2(svPosition.xy);
    float depth = gTextures[gDecal.depthIndex].Load(int32_t3(pixel, 0)).r;
    // --- 画面の位置 → 正規化座標（x・y は -1〜+1。y は上が +、画面のピクセルは下が +）---
    uint32_t width, height;
    gTextures[gDecal.depthIndex].GetDimensions(width, height);
    float32_t2 uv = svPosition.xy / float32_t2(width, height);
    float32_t4 ndc = float32_t4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    // --- 正規化座標 → ワールド（viewProj の逆行列を掛けて、w で割る）---
    float32_t4 world = mul(ndc, gCamera.inverseViewProj);
    DecalSurface surface;
    surface.world = world.xyz / world.w;
    surface.local = mul(float32_t4(surface.world, 1.0f), gDecal.worldToDecal).xyz;
    return surface;
}

// 箱の外なら捨てる（clip：引数のどれか1つでもマイナスなら、このピクセルを描かない）
void ClipOutsideDecal(float32_t3 local)
{
    clip(0.5f - abs(local));
}
#HLSL_END
