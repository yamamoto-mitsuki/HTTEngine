#META
stage: PS
profile : ps_6_0
#META_END

#HLSL
#include "Post/Bloom.hlsli"

// 川瀬式（MGF）の合成：シーン ＋ 強さ × (w1·段1 ＋ w2·段2 ＋ w3·段3 ＋ w4·段4)
// 段は大きさが違う（1/4〜1/32）が、UV（0〜1）で読むので、そのまま重ねられる（小さい段は拡大して読まれ、広く・柔らかく広がる）
float32_t4 main(VSOutput input) : SV_TARGET
{
    float32_t3 scene = gTextures[gBloom.srcIndex].Sample(gSamplerClamp, input.texcoord).rgb;
    float32_t3 bloom = float32_t3(0.0f, 0.0f, 0.0f);
    for (int i = 0; i < 4; ++i)
    {
        bloom += gTextures[gBloom.levelIndices[i]].Sample(gSamplerClamp, input.texcoord).rgb * gBloom.levelWeights[i];
    }
    return float32_t4(scene + bloom * gBloom.intensity, 1.0f);
}
#HLSL_END