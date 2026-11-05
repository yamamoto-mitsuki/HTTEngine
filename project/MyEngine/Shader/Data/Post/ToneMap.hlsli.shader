#META
stage: Include
#META_END

#HLSL
// トーンマップ：HDR の明るさ（0〜いくらでも）を、画面に出せる 0〜1 に収める曲線
// どれもリニアの値を受け取って、リニアの 0〜1 を返す（画面の _SRGB の形へ書くときに GPU が sRGB へ直す）

// 種類（C++ の ToneMapMode と同じ並び）
static const uint32_t kToneMapNone = 0;
static const uint32_t kToneMapReinhard = 1;
static const uint32_t kToneMapACES = 2;
static const uint32_t kToneMapGT = 3;

// ===== Reinhard：x / (1 + x) =====
// どこまで明るくても 1 に近づくだけで、白く飛ばない。そのぶん全体が暗く・淡くなる（1 が 0.5 になる）
float32_t3 ToneMapReinhard(float32_t3 color)
{
    return color / (1.0f + color);
}

// ===== ACES（映画の色の標準）の近似：Stephen Hill の式（BakingLab）=====
// 明るい所は白へ柔らかく飽和し、暗い所は締まる。色が少し濃くなる（1 が 0.8 くらいになるので、Exposure を少し上げて使う）
// 1. sRGB の色を ACES の色の空間へ（RRT の彩度の調整も込み）
static const float32_t3x3 kACESInput =
{
    { 0.59719f, 0.35458f, 0.04823f },
    { 0.07600f, 0.90834f, 0.01566f },
    { 0.02840f, 0.13383f, 0.83777f },
};
// 3. ACES の色の空間から sRGB へ戻す（ODT の彩度の調整も込み）
static const float32_t3x3 kACESOutput =
{
    { 1.60475f, -0.53108f, -0.07367f },
    { -0.10208f, 1.10813f, -0.00605f },
    { -0.00327f, -0.07276f, 1.07602f },
};
// 2. RRT（映画のフィルムのような曲線）と ODT（画面へ出す曲線）を合わせた形を、分数の式で近づけた物
float32_t3 RRTAndODTFit(float32_t3 v)
{
    float32_t3 a = v * (v + 0.0245786f) - 0.000090537f;
    float32_t3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    return a / b;
}
float32_t3 ToneMapACES(float32_t3 color)
{
    color = mul(kACESInput, color);
    color = RRTAndODTFit(color);
    color = mul(kACESOutput, color);
    return saturate(color);
}

// ===== GT（グランツーリスモ。内村式）=====
// 暗い所は少し締め（曲線）、真ん中は傾き 1 の直線（色も明るさも変わらない）、明るい所だけ 1 へ柔らかく近づく
// 真ん中が変わらないので、絵の色がずれにくい（アニメ調に向く）
static const float kGTMaxBrightness = 1.0f; // P：一番明るい値（画面の白）
static const float kGTContrast = 1.0f; // a：直線の部分の傾き
static const float kGTLinearStart = 0.22f; // m：直線の部分が始まる明るさ
static const float kGTLinearLength = 0.4f; // l：直線の部分の長さ
static const float kGTBlack = 1.33f; // c：暗い所の締め方（大きいほど暗い所が黒く締まる）
float ToneMapGTChannel(float x)
{
    const float P = kGTMaxBrightness;
    const float a = kGTContrast;
    const float m = kGTLinearStart;
    const float l0 = ((P - m) * kGTLinearLength) / a;
    const float S0 = m + l0; // 明るい所の曲線が始まる明るさ
    const float S1 = m + a * l0; // そのときの出力
    const float C2 = (a * P) / (P - S1);
    const float CP = -C2 / P;
    // どの部分にいるかの重み（暗い所 w0・真ん中 w1・明るい所 w2。足して 1）
    const float w0 = 1.0f - smoothstep(0.0f, m, x);
    const float w2 = step(S0, x);
    const float w1 = 1.0f - w0 - w2;
    // linear は HLSL の予約語（頂点の値の補間の仕方）なので、変数の名前に使えない
    const float toe = m * pow(max(x, 0.0f) / m, kGTBlack); // 暗い所（つま先）
    const float middle = m + a * (x - m); // 真ん中（直線）
    const float shoulder = P - (P - S1) * exp(CP * (x - S0)); // 明るい所（肩）
    return toe * w0 + middle * w1 + shoulder * w2;
}
float32_t3 ToneMapGT(float32_t3 color)
{
    return float32_t3(ToneMapGTChannel(color.r), ToneMapGTChannel(color.g), ToneMapGTChannel(color.b));
}

// ===== 種類を選んで掛ける =====
float32_t3 ApplyToneMap(float32_t3 color, uint32_t mode)
{
    switch (mode)
    {
        case kToneMapReinhard:
            return ToneMapReinhard(color);
        case kToneMapACES:
            return ToneMapACES(color);
        case kToneMapGT:
            return ToneMapGT(color);
        default: // None：1 で切るだけ（今までの見た目）
            return saturate(color);
    }
}
#HLSL_END