#META
stage: PS
profile : ps_6_0
#META_END

#HLSL
#include "Post/Bloom.hlsli"

float32_t4 main(VSOutput input) : SV_TARGET
{
    // 小さい段をぼかして拡大し、同じ大きさの縮小結果を足し戻す
    float32_t3 blurred = Tent(gBloom.srcIndex, input.texcoord, gBloom.texelSize);
    float32_t3 previous = gTextures[gBloom.addIndex].Sample(gSamplerClamp, input.texcoord).rgb;
    return float32_t4(blurred + previous, 1.0f);
}
#HLSL_END