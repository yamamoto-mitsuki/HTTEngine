#META
stage: CS
profile : cs_6_0
#META_END

#HLSL
// ===== 被写界深度 2：ぼかす（1スレッド = 半分の大きさの1ピクセル）=====
// まわりの点を、うずまき（黄金角ずつ回して、外へ広げる）の順に読んで混ぜる
// 読んだ点のぼけの大きさが「その点までの距離」より大きければ、その点の色はここまでにじんでくる、と考える
// （ぼけた点は、自分のぼけの半径の円に色を広げる。それを、広げられる側から集める書き方）
#include "Post/DepthOfField.hlsli"

RWTexture2D<float32_t4> gOutput : register(u0); // ぼかした色＋手前のぼけのはみ出し（DepthOfFieldPass の blur_）

static const float kGoldenAngle = 2.39996323f; // 黄金角（ラジアン。約 137.5 度）。この角度ずつ回すと、点が偏らずに円に散らばる

[numthreads(8, 8, 1)]
void main(uint32_t3 dispatchThreadId : SV_DispatchThreadID)
{
    uint32_t2 pixel = dispatchThreadId.xy;
    if (any(pixel >= gDof.halfSize))
    {
        return;
    }
    float32_t4 center = gTextures[gDof.halfIndex].Load(int32_t3(pixel, 0));
    float centerSize = abs(center.a) * gDof.maxRadius; // 自分のぼけの半径（ピクセル）
    float32_t3 sum = center.rgb; // 混ぜた色の合計
    float count = 1.0f; // 混ぜた数
    float nearSpill = max(-center.a, 0.0f); // 手前のぼけが、ここにどれだけかぶっているか（合成で使う）

    float angle = 0.0f;
    [loop]
    for (float radius = gDof.radiusStep; radius < gDof.maxRadius; radius += gDof.radiusStep / radius)
    {
        // --- 次の点（radius += 間隔 / radius：外ほど円周が長いので、1 周に置く点を増やす。円の中に点がほぼ同じ濃さで並ぶ）---
        angle += kGoldenAngle;
        float32_t2 uv = (float32_t2(pixel) + 0.5f + float32_t2(cos(angle), sin(angle)) * radius) * gDof.halfTexelSize;
        uv = clamp(uv, gDof.halfTexelSize * 0.5f, 1.0f - gDof.halfTexelSize * 0.5f); // 絵の外を読まない（サンプラーが WRAP なので、反対の端が混ざる）
        // sample は HLSL の予約語（補間の指定）なので、変数名に使えない
        float32_t4 neighbor = gTextures[gDof.halfIndex].SampleLevel(gSamplerClamp, uv, 0.0f);

        // --- その点のぼけの半径 ---
        float neighborSize = abs(neighbor.a) * gDof.maxRadius;
        // 自分より奥の点は、自分の 2 倍より大きくにじませない
        // （ピントの合った物に、後ろのぼけた背景がかぶらない。背景はピントの合った物の後ろにあるので、前へ出てこない）
        if (neighbor.a > center.a)
        {
            neighborSize = min(neighborSize, centerSize * 2.0f);
        }
        // --- ぼけの半径が、ここまでの距離より大きいほど混ぜる（境目の 1 ピクセルはなめらかに）---
        float weight = smoothstep(radius - 0.5f, radius + 0.5f, neighborSize);
        sum += lerp(sum / count, neighbor.rgb, weight); // 混ぜない点は、今までの平均を足す（平均を変えない）
        count += 1.0f;
        nearSpill = max(nearSpill, max(-neighbor.a, 0.0f) * weight);
    }
    gOutput[pixel] = float32_t4(sum / count, nearSpill);
}
#HLSL_END