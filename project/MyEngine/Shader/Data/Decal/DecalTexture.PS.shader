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
color:   Color   "Color"   "色（テクスチャの色に掛ける。A の分だけ透ける）" 1, 1, 1, 1
texture: Texture "Texture" "貼る画像（箱の XZ が UV。上が箱の +Z。選ばなければ白）"
#PROPERTIES_END

#HLSL
// デカールの既定の Shader：箱の中にある不透明な物に、画像を上から貼る（Decal Renderer の既定のマテリアル）
#include "Model/Object3d.hlsli"
#include "Decal/Decal.hlsli"
#include "Fog/Fog.hlsli"

struct Material
{
    float32_t4 color; // 色（リニア）
    uint32_t texture; // 貼る画像（gTextures[] の番号）
};
ConstantBuffer<Material> gMaterial : register(b20);

PixelShaderOutput main(VertexShaderOutput input)
{
    DecalSurface surface = GetDecalSurface(input.position);
    ClipOutsideDecal(surface.local);

    // 箱の XZ を UV に（左が 0、上（+Z）が 0）
    // 深度の段差の所で UV が飛ぶので、ミップを自動で選ばない（SampleLevel の 0）。縁が端の色で伸びないように CLAMP
    float32_t2 uv = float32_t2(surface.local.x + 0.5f, 0.5f - surface.local.z);
    float32_t4 color = gMaterial.color * gTextures[gMaterial.texture].SampleLevel(gSamplerClamp, uv, 0.0f);
    color.rgb = ApplyFog(color.rgb, surface.world); // 下の物と同じだけ霧を掛ける（掛けないと、霧の中でデカールだけくっきり見える）

    PixelShaderOutput output;
    output.color = color;
    return output;
}
#HLSL_END
