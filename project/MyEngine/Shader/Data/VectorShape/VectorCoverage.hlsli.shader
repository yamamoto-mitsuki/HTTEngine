#META
stage: Include
path : VectorShape/VectorCoverage.hlsli
#META_END

#HLSL
// Vector Shape と Polyline が共通で使う、縁のなめらかさと重ね方の直し

// C++ の BlendMode と同じ順の番号
static const uint kBlendNone = 0;
static const uint kBlendMultiply = 4;
static const uint kBlendScreen = 5;

// ===== 輪郭のなめらかさ =====
// 距離 d から「このピクセルのうち、図形に入っている割合」を出す（0〜1）
// ddx・ddy：隣のピクセルとの d の差。どんな大きさ・回転で描いても、1ピクセル分の d の変化が分かるので、ちょうど1ピクセルの幅でなめらかになる
float32_t Coverage(float32_t d)
{
    float32_t pixelWidth = max(length(float32_t2(ddx(d), ddy(d))), 1e-5f);
    return saturate(0.5f - d / pixelWidth);
}

// ===== 重ね方に合わせる =====
// 板・メッシュは図形より広いので、図形の外（alpha が 0）が背景を変えないようにする
void ApplyBlendFix(uint blendMode, inout float32_t3 rgb, float32_t alpha)
{
    switch (blendMode)
    {
        case kBlendNone:
            clip(alpha - 0.5f); // 混ぜないので、半分より外を捨てる
            break;
        case kBlendMultiply:
            rgb = lerp(float32_t3(1.0f, 1.0f, 1.0f), rgb, alpha); // 乗算は alpha を見ないので、外を白（掛けても変わらない色）にする
            break;
        case kBlendScreen:
            rgb *= alpha; // スクリーンも alpha を見ないので、外を黒（足しても変わらない色）にする
            break;
    }
}
#HLSL_END