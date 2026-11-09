#META
stage: CS
profile : cs_6_0
#META_END

#HLSL
// ===== 被写界深度 1：半分の大きさに縮める（1スレッド = 半分の大きさの1ピクセル = 元の絵の 2×2）=====
// RGB：4つの色の平均 / A：4つのぼけの大きさ（CalcCoc）の平均
#include "Post/DepthOfField.hlsli"

RWTexture2D<float32_t4> gOutput : register(u0); // 縮めた色＋ぼけの大きさ（DepthOfFieldPass の half_）

[numthreads(8, 8, 1)]
void main(uint32_t3 dispatchThreadId : SV_DispatchThreadID)
{
    uint32_t2 pixel = dispatchThreadId.xy;
    // 最後のグループには、絵の外のスレッドがいる。何もせず帰る
    if (any(pixel >= gDof.halfSize))
    {
        return;
    }
    float32_t3 color = float32_t3(0.0f, 0.0f, 0.0f);
    float coc = 0.0f;
    for (uint32_t i = 0; i < 4; ++i)
    {
        // 元の絵の 2×2（i の 1 の位で横、2 の位で縦にずらす）。絵の大きさが奇数のときは、はみ出さないように端で止める
        uint32_t2 source = min(pixel * 2 + uint32_t2(i & 1, i >> 1), gDof.fullSize - 1);
        color += gTextures[gDof.colorIndex].Load(int32_t3(source, 0)).rgb;
        coc += CalcCoc(gTextures[gDof.depthIndex].Load(int32_t3(source, 0)).r);
    }
    gOutput[pixel] = float32_t4(color * 0.25f, coc * 0.25f);
}
#HLSL_END