#META
stage:   PS
profile: ps_6_0
#META_END

#PROGRAM
category: Particle
shading:  Unlit
vs:       ParticleVS
#PROGRAM_END

#HLSL
#include "Particle.hlsli"
#include "Buffers/Camera.hlsli"
#include "Fog/Fog.hlsli"
#include "Resources/Texture.hlsli"

struct Material
{
    float32_t4 color;
    float4x4 uvTransform;
    uint32_t textureIndex;
    uint32_t isSoftCircle; // 1：テクスチャの代わりに丸いぼかし（Particle System の Texture が空のとき）
    uint32_t useFog; // 1：霧を混ぜる（0：霧の中でも、そのままの色）
    float padC;
};
ConstantBuffer<Material> gMaterial : register(b20);


PixelShaderOutput main(VertexShaderOutput input)
{
    float32_t4 texColor;
    if (gMaterial.isSoftCircle != 0) {
        // 丸いぼかし：板の真ん中からの距離 d（真ん中 0・辺の真ん中 1）で、真ん中 1 → 縁 0 へ薄くする。2乗で芯を濃く
        float32_t2 d = input.texcoord * 2.0f - 1.0f;
        float fade = saturate(1.0f - dot(d, d));
        texColor = float32_t4(1.0f, 1.0f, 1.0f, fade * fade);
    } else {
        float32_t4 transformedUV = mul(float32_t4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
        texColor = gTextures[gMaterial.textureIndex].Sample(gSampler, transformedUV.xy);
    }
    if (texColor.a == 0.0) {
        discard;
    }

    float32_t3 finalColor = input.color.rgb * gMaterial.color.rgb * texColor.rgb;
    float finalAlpha = input.color.a * gMaterial.color.a * texColor.a;
    // 霧
    if (gMaterial.useFog != 0) {
        finalColor = ApplyFog(finalColor, input.worldPosition);
    }
    
    PixelShaderOutput output;
    output.color = float32_t4(finalColor, finalAlpha);
    if (output.color.a == 0.0) { discard; }
    return output;
}
#HLSL_END