#META
stage: PS
profile : ps_6_0
#META_END

#HLSL
// ===== 被写界深度 3：元の絵と、ぼかした絵を混ぜる（元の大きさ）=====
// ぼけの大きさは元の大きさの深度で計算し直す（半分の大きさのままだと、ピントの合った物の縁までぼける）
#include "Post/DepthOfField.hlsli"

struct VSOutput
{
    float32_t4 position : SV_Position;
    float32_t2 texcoord : TEXCOORD0;
};

float32_t4 main(VSOutput input) : SV_TARGET
{
    int32_t3 pixel = int32_t3(input.position.xy, 0);
    float32_t3 sharp = gTextures[gDof.colorIndex].Load(pixel).rgb;
    float coc = abs(CalcCoc(gTextures[gDof.depthIndex].Load(pixel).r));
    // ぼかした絵は半分の大きさなので、線形補間で引き伸ばして読む（端は WRAP で反対側を読まないように止める）
    float32_t2 uv = clamp(input.texcoord, gDof.halfTexelSize * 0.5f, 1.0f - gDof.halfTexelSize * 0.5f);
    float32_t4 blur = gTextures[gDof.blurIndex].SampleLevel(gSamplerClamp, uv, 0.0f);
    // 自分のぼけと、手前のぼけのかぶり（blur.a）の大きい方。半径（元の大きさのピクセル）が 0.5 から 2 の間で、ぼかした絵に切り替える
    float radius = max(coc, blur.a) * gDof.maxRadius * 2.0f;
    float t = smoothstep(0.5f, 2.0f, radius);
    return float32_t4(lerp(sharp, blur.rgb, t), 1.0f);
}
#HLSL_END