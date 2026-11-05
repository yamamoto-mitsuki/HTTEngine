#META
stage: PS
profile : ps_6_0
#META_END

#HLSL
#include "Post/Bloom.hlsli"

// 9タップの1方向ガウス（σ = 2 ピクセル）。重みは exp(-x² / (2σ²)) を、足して 1 になるように割った物
// 真ん中と、片側4つ（離れた順）。横・縦の2回に分けると、9×9 の2次元のぼかしと同じになる（81回読む所が 18回で済む）
static const float kWeights[5] = { 0.2042f, 0.1802f, 0.1238f, 0.0663f, 0.0276f };

float32_t4 main(VSOutput input) : SV_TARGET
{
    float32_t2 step = gBloom.direction * gBloom.texelSize; // 1ピクセル分、ぼかす向きへ
    float32_t3 sum = gTextures[gBloom.srcIndex].Sample(gSamplerClamp, input.texcoord).rgb * kWeights[0];
    for (int i = 1; i < 5; ++i)
    {
        sum += gTextures[gBloom.srcIndex].Sample(gSamplerClamp, input.texcoord + step * i).rgb * kWeights[i];
        sum += gTextures[gBloom.srcIndex].Sample(gSamplerClamp, input.texcoord - step * i).rgb * kWeights[i];
    }
    return float32_t4(sum, 1.0f);
}
#HLSL_END