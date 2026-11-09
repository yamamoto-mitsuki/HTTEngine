#META
stage: PS
profile : ps_6_0
#META_END

#HLSL
#include "Post/Bloom.hlsli"

float32_t4 main(VSOutput input) : SV_TARGET
{
    float32_t3 scene = gTextures[gBloom.srcIndex].Sample(gSamplerClamp, input.texcoord).rgb;
    float32_t3 bloom = gTextures[gBloom.addIndex].Sample(gSamplerClamp, input.texcoord).rgb;
    return float32_t4(scene + bloom * gBloom.intensity, 1.0f);
}
#HLSL_END