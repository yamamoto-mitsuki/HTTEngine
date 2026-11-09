#META
stage: Include
#META_END

#HLSL
// ディザ：画面（8 ビット。0〜255 の 256 段）へ書く前に、1 段より小さい粒を足して、なめらかな明るさの変化が帯（等高線）に見えないようにする
// ビネット・空・霧のような、広い所でゆっくり変わる明るさは、256 段に丸めると「同じ値の広い帯」と「1 段の境目」になり、境目が線に見える（白黒だと特に目立つ）
// 粒を足すと、境目の近くで上の段・下の段が細かく混ざるので、目には間の明るさに見える（Unreal のトーンマップの Grain Quantization と同じ考え）

// ===== リニア ⇔ sRGB（画面の _SRGB の形と同じ式）=====
// 丸めは sRGB の値（画面に書く値）で起きるので、粒は sRGB の値で 1 段分にそろえて足す
float32_t3 LinearToSrgb(float32_t3 color)
{
    float32_t3 low = color * 12.92f;
    float32_t3 high = 1.055f * pow(max(color, 0.0f), 1.0f / 2.4f) - 0.055f;
    return lerp(high, low, step(color, 0.0031308f)); // step(a, b) は b >= a なら 1（しきい値以下の所は low）
}

float32_t3 SrgbToLinear(float32_t3 color)
{
    float32_t3 low = color / 12.92f;
    float32_t3 high = pow(max((color + 0.055f) / 1.055f, 0.0f), 2.4f);
    return lerp(high, low, step(color, 0.04045f));
}

// ===== 画素ごとの 0〜1 の数（Interleaved Gradient Noise。テクスチャを使わず、隣どうしが似ない並びになる）=====
float InterleavedGradientNoise(float32_t2 pixel)
{
    return frac(52.9829189f * frac(dot(pixel, float32_t2(0.06711056f, 0.00583715f))));
}

// ===== 粒を足す。color はリニアの 0〜1、pixel は画面の画素の位置（SV_Position の xy）=====
// 粒は 2 つの数を足して 1 を引いた -1〜1（三角形の分布。真ん中が多いので、1 つの数より粒が目立たない）× 1 段（1/255）
// R・G・B に同じ粒を足す（灰色は灰色のまま。色ごとに違う粒だと、白黒の絵に色の点が出る）
float32_t3 ApplyDither(float32_t3 color, float32_t2 pixel)
{
    float noise = InterleavedGradientNoise(pixel) + InterleavedGradientNoise(pixel + float32_t2(113.0f, 71.0f)) - 1.0f;
    float32_t3 srgb = LinearToSrgb(saturate(color)) + noise / 255.0f;
    return SrgbToLinear(saturate(srgb));
}
#HLSL_END
