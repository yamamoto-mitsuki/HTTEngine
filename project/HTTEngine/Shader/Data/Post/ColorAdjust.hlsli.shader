#META
stage: Include
#META_END

#HLSL
// 色の調整（Unity の Color Adjustments の小さい版）
// どれもリニアの値を受け取って、リニアの値を返す。トーンマップの前（HDR のまま）に掛ける（Unity と同じ順）

// 明るさの重み（Rec.709。人の目は緑を一番明るく、青を一番暗く感じる。リニアの値に掛ける）
static const float32_t3 kLuminanceWeight = float32_t3(0.2126f, 0.7152f, 0.0722f);

// ===== 明るさ（色を抜いた灰色の明るさ）=====
float Luminance(float32_t3 color)
{
    return dot(color, kLuminanceWeight);
}

// ===== 彩度：1 でそのまま・0 で白黒（同じ明るさの灰色）・1 より大きいと色が濃くなる =====
// 灰色と元の色の間を saturation で混ぜる。1 を超えると灰色から離れる向きへ行き過ぎるので、マイナスにならないように 0 で止める
float32_t3 ApplySaturation(float32_t3 color, float saturation)
{
    float32_t3 gray = Luminance(color).xxx;
    return max(lerp(gray, color, saturation), 0.0f);
}
#HLSL_END
