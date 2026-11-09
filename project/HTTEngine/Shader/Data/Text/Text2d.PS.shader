#META
stage: PS
profile : ps_6_0
#META_END

#PROGRAM
category: Text
shading : Unlit
vs:       Text2dVS
#PROGRAM_END


#HLSL
#include "Text2d.hlsli"
#include "Resources/Texture.hlsli"

// 文字のマテリアル（C++ の TextMaterialData と同じ並び。float4 を先に、float は後ろにまとめる）
struct Material
{
    float32_t4 color; // 文字の色
    float32_t4 outlineColor; // 縁取りの色
    float32_t4 glowColor; // 光（グロー）の色
    float32_t4 shineColor; // 斜めに通る光の色
    float32_t outlineWidth; // 縁取りの太さ（SDF の値で。0 なら縁取りなし）
    uint textureIndex; // フォントのアトラス
    float32_t glowWidth; // 光の広さ（SDF の値で。縁取りの外側へ。0 なら無し）
    float32_t shinePosition; // 斜めの光の帯の真ん中（かたまりの左が 0、右が 1。外なら見えない）
    float32_t shineWidth; // 帯の半分の幅（かたまりの幅に対して。0 なら無し）
};
ConstantBuffer<Material> gMaterial : register(b20);

static const float32_t kShineSlant = 0.3f; // 斜めの光の傾き（下へ行くほど左へずれる量＝「/」の向き。かたまりの幅に対して）

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

// SDF（Signed Distance Field）：アトラスの1ピクセルに「文字の縁までの距離」が入っている
//   0.5 ＝ ちょうど縁、0.5 より大きい ＝ 文字の内側、小さい ＝ 外側（縁から離れるほど 0 に近づく）
// 距離なので、しきい値（どこから塗るか）を変えるだけで、縁取り・光を足せる
PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float32_t distance = gTextures[gMaterial.textureIndex].Sample(gSampler, input.texcoord).r;

    // 画面の1ピクセル進むと distance がどれだけ変わるか（fwidth。大きく描くほど小さい）
    // その幅でなめらかに変えると、どの大きさで描いても、縁がちょうど1ピクセルでなめらかになる
    float32_t edgeWidth = max(fwidth(distance), 1e-4f);

    // ===== 1. 文字の中（縁の 0.5 より内側）=====
    float32_t fill = smoothstep(0.5f - edgeWidth, 0.5f + edgeWidth, distance);
    float32_t3 letterColor = gMaterial.color.rgb;

    // ===== 2. 斜めに通る光（文字の中だけ）=====
    // 帯の真ん中からの横の距離（下へ行くほど左へずらして「/」の向きの帯にする）。真ん中は 1、帯の端で 0
    if (gMaterial.shineWidth > 0.0f)
    {
        float32_t across = abs(input.local.x + input.local.y * kShineSlant - gMaterial.shinePosition);
        float32_t shine = 1.0f - smoothstep(0.0f, gMaterial.shineWidth, across);
        letterColor = lerp(letterColor, gMaterial.shineColor.rgb, shine * gMaterial.shineColor.a);
    }

    // ===== 3. 縁取り：縁より outlineWidth だけ外の線まで、縁取りの色。その上に文字を重ねる =====
    float32_t4 text = float32_t4(letterColor, gMaterial.color.a * fill);
    if (gMaterial.outlineWidth > 0.0f)
    {
        float32_t outlineEdge = 0.5f - gMaterial.outlineWidth;
        float32_t outline = smoothstep(outlineEdge - edgeWidth, outlineEdge + edgeWidth, distance);
        text.rgb = lerp(gMaterial.outlineColor.rgb, letterColor, fill);
        text.a = lerp(gMaterial.outlineColor.a * outline, gMaterial.color.a, fill);
    }

    // ===== 4. 光（グロー）：縁取りのさらに外へ、離れるほど薄くなる光を、文字の後ろに敷く =====
    if (gMaterial.glowWidth > 0.0f)
    {
        float32_t glowStart = 0.5f - gMaterial.outlineWidth - gMaterial.glowWidth; // ここで 0、縁取りの外の線で 1
        float32_t glow = smoothstep(glowStart, 0.5f - gMaterial.outlineWidth, distance);
        float32_t glowAlpha = gMaterial.glowColor.a * glow * glow; // 2乗：外へ行くほど早く薄くなる（ふわっとした光）
        // 光の上に文字を重ねる（文字が透けている所ほど、光が見える）
        text.rgb = lerp(gMaterial.glowColor.rgb, text.rgb, text.a);
        text.a = text.a + glowAlpha * (1.0f - text.a);
    }

    output.color = text;
    return output;
}
#HLSL_END