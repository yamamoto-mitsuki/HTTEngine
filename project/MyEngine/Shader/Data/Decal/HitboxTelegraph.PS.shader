#META
stage:   PS
profile: ps_6_0
#META_END

#PROGRAM
category:   Decal
vs:         Object3dVS
blend:      Normal
rasterizer: SolidFront
depth:      Disable
#PROGRAM_END

#PROPERTIES
fillColor:     Color   "Fill Color"     "形の中の色（A の分だけ透ける）" 1, 0.25, 0.15, 0.2
progressColor: Color   "Progress Color" "Warning の進み具合で、根元から先へ広がる所の色（Warning が終わると、先まで届く）" 1, 0.3, 0.15, 0.45
edgeColor:     Color   "Edge Color"     "縁の色" 1, 0.45, 0.25, 0.9
edgeWidth:     Range   "Edge Width"     "縁の太さ（m）" 0.12, 0, 1
activeColor:   Color   "Active Color"   "Active の間の色（Hitbox Telegraph の Show While Active を入れたときだけ）" 1, 0.15, 0.1, 0.6
texture:       Texture "Texture"        "形の中の色に掛ける画像（形を囲む四角に貼る。選ばなければ白）"
#PROPERTIES_END

#HLSL
// 床の予告の既定の Shader（Hitbox Telegraph の既定のマテリアル）
// 当たりと同じ符号付き距離で、形の中を塗り、縁を引き、Warning の進み具合で根元から先へ色を広げる
#include "Model/Object3d.hlsli"
#include "Decal/Decal.hlsli"
#include "Decal/HitShape.hlsli"
#include "Decal/HitboxTelegraph.hlsli"
#include "Fog/Fog.hlsli"

struct Material
{
    float32_t4 fillColor;     // 形の中の色（リニア）
    float32_t4 progressColor; // 進み具合で広がる所の色
    float32_t4 edgeColor;     // 縁の色
    float32_t4 activeColor;   // Active の間の色
    float edgeWidth;          // 縁の太さ（m）
    uint32_t texture;         // 形の中の色に掛ける画像
};
ConstantBuffer<Material> gMaterial : register(b20);

PixelShaderOutput main(VertexShaderOutput input)
{
    // --- 床の位置（箱は形を囲む大きさで、高さは形の高さ）---
    DecalSurface surface = GetDecalSurface(input.position);
    float32_t3 local = ToHitShapeLocal(surface.world);

    // --- 形の縁までの距離と、進み具合の境目までの距離（m）---
    // fwidth：隣のピクセルとの差＝1 ピクセルの長さ。縁を 1 ピクセルぶんなめらかにするのに使う（clip で捨てる前に求める）
    // 段差の所は隣のピクセルが別の面なので差が大きくなる。ぼけすぎないように kMaxSoftness で止める
    static const float kMaxSoftness = 0.1f; // なめらかにする幅の上限（m）
    float distance = CalcTelegraphDistance(local);
    float pixel = clamp(fwidth(distance), 1.0e-4f, kMaxSoftness);
    float reachDistance = (CalcTelegraphReach(local) - gDrawData.progress) * GetTelegraphReachLength();
    float reachPixel = clamp(fwidth(reachDistance), 1.0e-4f, kMaxSoftness);

    // --- 形の中か（中で 1、外で 0）---
    ClipOutsideDecal(surface.local); // 箱の外は捨てる（fwidth の後。捨てたピクセルとの差は使えないので）
    float coverage = saturate(0.5f - distance / pixel);
    clip(coverage - 1.0e-3f);

    // --- 色：中の色 → 進み具合の所 → 縁（Active の間は Active の色）---
    float32_t4 color = gMaterial.fillColor;
    if (gDrawData.phase == kHitboxPhaseActive)
    {
        color = gMaterial.activeColor;
    }
    else
    {
        // 根元からの長さが、進み具合の所まで届いているか（進み具合が 0 なら届かない）
        float reached = saturate(0.5f - reachDistance / reachPixel);
        color = lerp(color, gMaterial.progressColor, reached * step(1.0e-4f, gDrawData.progress));
    }
    // 縁から edgeWidth の内側で 1（縁の太さが 0 なら縁なし）
    float edge = saturate(0.5f + (distance + gMaterial.edgeWidth) / pixel);
    color = lerp(color, gMaterial.edgeColor, edge * step(1.0e-4f, gMaterial.edgeWidth));

    // --- 画像（形を囲む四角＝箱の XZ に貼る）・形の縁のなめらかさ・霧 ---
    float32_t2 uv = float32_t2(surface.local.x + 0.5f, 0.5f - surface.local.z);
    color *= gTextures[gMaterial.texture].SampleLevel(gSamplerClamp, uv, 0.0f);
    color.a *= coverage;
    color.rgb = ApplyFog(color.rgb, surface.world); // 下の物と同じだけ霧を掛ける

    PixelShaderOutput output;
    output.color = color;
    return output;
}
#HLSL_END
