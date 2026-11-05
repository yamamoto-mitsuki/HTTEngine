#META
stage: PS
profile : ps_6_0
#META_END

#HLSL
#include "Post/Bloom.hlsli"

// 抜き出す明るさの上限。シーンの絵は HDR なので、1点だけ極端に明るい所（鏡のような面のハイライトなど）があると、
// ぼかしたときに大きな光の玉になって、フレームごとにちらつく。抜き出す前にこの値で切る
static const float kMaxBrightness = 64.0f;


float32_t4 main(VSOutput input) : SV_TARGET
{
    // max(…, 0) は負の値と NaN（0 で割った所など）も 0 にする（float の絵は NaN をそのまま持つので、ぼかすと黒い四角が広がる）
    float32_t3 color = min(max(gTextures[gBloom.srcIndex].Sample(gSamplerClamp, input.texcoord).rgb, 0.0f), kMaxBrightness);
    float brightness = max(color.r, max(color.g, color.b));
    // しきい値でバッサリ切るとフレーム間でチラつくので、knee の幅だけ滑らかに繋ぐ
    float soft = clamp(brightness - gBloom.threshold + gBloom.knee, 0.0f, 2.0f * gBloom.knee);
    soft = soft * soft / (4.0f * gBloom.knee + 0.0001f);
    float contribution = max(soft, brightness - gBloom.threshold) / max(brightness, 0.0001f);
    return float32_t4(color * contribution, 1.0f);
}
#HLSL_END