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
baseColor: Color "Base Color" "形の中の色" 0.2, 0.5, 1, 0.15
ringColor: Color "Ring Color" "輪の色" 0.3, 0.8, 1, 0.7
ringCount: Float "Ring Count" "根元から先までの輪の数" 4
speed:     Float "Speed"      "輪が先から根元へ進む速さ（1 秒に輪 1 つぶん × これ）" 1.5
#PROPERTIES_END

#HLSL
// Sandbox の見本：ゲームの resources に置いた予告の Shader（エンジンの .hlsli を include して、当たりと同じ形で塗る）
// 輪が、形の先から根元へ向かって進む
#include "Model/Object3d.hlsli"
#include "Decal/Decal.hlsli"
#include "Decal/HitShape.hlsli"
#include "Decal/HitboxTelegraph.hlsli"
#include "Fog/Fog.hlsli"

struct Material
{
    float32_t4 baseColor; // 形の中の色（リニア）
    float32_t4 ringColor; // 輪の色
    float ringCount;      // 輪の数
    float speed;          // 輪の進む速さ
};
ConstantBuffer<Material> gMaterial : register(b20);

PixelShaderOutput main(VertexShaderOutput input)
{
    DecalSurface surface = GetDecalSurface(input.position);
    float32_t3 local = ToHitShapeLocal(surface.world);
    float distance = CalcTelegraphDistance(local);
    float pixel = clamp(fwidth(distance), 1.0e-4f, 0.1f);
    ClipOutsideDecal(surface.local);
    float coverage = saturate(0.5f - distance / pixel);
    clip(coverage - 1.0e-3f);

    // 根元からの位置 × 輪の数 ＋ 時間 の小数の所が 0.3 より小さい所が輪（時間が進むと、輪が根元へ動く）
    float stripe = frac(CalcTelegraphReach(local) * gMaterial.ringCount + gDrawData.phaseTime * gMaterial.speed);
    float32_t4 color = lerp(gMaterial.baseColor, gMaterial.ringColor, step(stripe, 0.3f));
    color.a *= coverage;
    color.rgb = ApplyFog(color.rgb, surface.world);

    PixelShaderOutput output;
    output.color = color;
    return output;
}
#HLSL_END
