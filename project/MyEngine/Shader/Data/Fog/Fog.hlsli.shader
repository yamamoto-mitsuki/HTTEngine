#META
stage: Include
#META_END

#HLSL
// 霧を混ぜる。3D のシェーダーの最後（色が決まった後）に呼ぶ
// 値はカメラの定数バッファ（gCamera）にあるので、"Buffers/Camera.hlsli" の後に include する（こちらでは include しない。2回 include すると、同じ名前が2つになって止まる）
// 加算（Additive）の物にも同じように混ぜるので、霧の濃い所の加算は、霧の色の分だけ明るくなる（遊ぶ面の近くなら、霧はほとんど掛からない）
float32_t3 ApplyFog(float32_t3 color, float32_t3 worldPosition)
{
    // ===== 距離の霧：カメラから遠いほど、霧の色に近づく =====
    // 1 − exp(−濃さ × 距離)：近いと 0、遠いほど 1 に近づく（霧の中を光が進むと、1m ごとに同じ割合だけ減る、という形）
    // 始まりの距離（Start Distance）までは 0。遊ぶ面がはっきり見えるように、ピントの面より少し奥から効かせる
    float distance = length(worldPosition - gCamera.worldPosition);
    float distanceFog = 1.0f - exp(-gCamera.fogDensity * max(distance - gCamera.fogStartDistance, 0.0f));
    color = lerp(color, gCamera.fogColor, min(distanceFog, gCamera.fogMaxOpacity));

    // ===== 高さの霧：低いほど、霧の色（暗い色）に近づく。穴の底を見えなくする =====
    // exp(−(高さ − 基準の高さ) × 減り方)：基準の高さで 1、上へ行くほど 0 に近づく。基準より下は 1 を超えるので、saturate で 1 にする
    float heightFog = saturate(exp(-(worldPosition.y - gCamera.heightFogBase) * gCamera.heightFogFalloff));
    color = lerp(color, gCamera.heightFogColor, heightFog * gCamera.heightFogMaxOpacity);
    return color;
}

// 下を向いた空が、どれだけ急に高さの霧の色になるか（向きの Y が −1 ÷ これ より下なら、一番濃い）
static const float kSkyHeightFogSharpness = 8.0f;

// 空（Skybox）に霧を混ぜる。direction は空を見る向き（長さ1）
// 空は無限に遠いので、距離の霧はいつも一番濃い所（Max Opacity）。遠くの背景の板と同じ色になじむ
// 下を向いた空は、無限に下まで霧の中を見るので、高さの霧が一番濃い。地平線（Y が 0）より上は掛けない
// 背景の板の下から空がのぞいても（穴の底など）、白っぽい空ではなく、暗い色になる
float32_t3 ApplySkyFog(float32_t3 color, float32_t3 direction)
{
    color = lerp(color, gCamera.fogColor, gCamera.fogMaxOpacity);
    float heightFog = saturate(-direction.y * kSkyHeightFogSharpness);
    color = lerp(color, gCamera.heightFogColor, heightFog * gCamera.heightFogMaxOpacity);
    return color;
}
#HLSL_END