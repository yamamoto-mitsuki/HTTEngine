#META
stage: PS
profile : ps_6_0
#META_END

#PROGRAM
category: VectorShape
shading : Unlit
vs:       Sprite2dVS
#PROGRAM_END


#HLSL
#include "Sprite/Sprite2d.hlsli"
#include "VectorShape/VectorCoverage.hlsli"

// 図形のマテリアル（C++ の VectorShapeMaterialData と同じ並び）
struct Material
{
    float32_t4 color; // 塗りの色（グラデーションの始まりの色）
    float32_t4 gradientColor; // グラデーションの終わりの色
    float32_t4 strokeColor; // 縁取りの色
    float32_t4 cornerRadius; // 角の半径（x=左上 y=右上 z=右下 w=左下）
    float32_t2 shapeHalf; // 図形の半分の大きさ（平行四辺形は、角を丸める前の形の真ん中の幅の半分と、高さの半分）
    float32_t2 shapeParam; // 円：x=半径 / リング：x=太さの真ん中の半径 y=太さの半分 / 平行四辺形：x=上の辺の横のずれ y=角の半径
    float32_t2 gradientAxis; // グラデーションの軸（直線：dot(位置, 軸) + 0.5 / 放射：length(位置 × 軸)）
    float32_t gradientStart; // 色が変わり始める場所
    float32_t gradientScale; // 1 ÷ 色が変わる範囲
    float32_t strokeInner; // 輪郭からの距離がこれより内側が塗り
    float32_t strokeOuter; // 輪郭からの距離がこれより内側が図形全体（縁取りを含む）
    float32_t fillOpacity; // 塗りを出すか（0 か 1）
    float32_t gaugeOffset; // 直線のゲージの切り口の場所
    float32_t2 gaugeAxisA; // ゲージの線の法線 A
    float32_t2 gaugeAxisB; // ゲージの線の法線 B（扇形の終わりの線）
    uint shape; // 図形の種類（VectorShapeType の順）
    uint gradientType; // グラデーションの種類（VectorShapeGradient の順）
    uint gaugeMode; // 0=切らない 1=直線 2=180 度より狭い扇形 3=180 度以上の扇形
    uint blendMode; // 重ね方（BlendMode の順）
    uint strokeUsesGradient; // 1：縁取りにも塗りのグラデーションを使う
    uint drawPart; // 何を描くか（UIDrawPart の順）
    float32_t2 padA;
};
ConstantBuffer<Material> gMaterial : register(b20);

// C++ の enum と同じ順の番号
static const uint kShapeRectangle = 0;
static const uint kShapeRoundedRectangle = 1;
static const uint kShapeCircle = 2;
static const uint kShapeEllipse = 3;
static const uint kShapeRing = 4;
static const uint kShapeParallelogram = 5;
static const uint kGradientLinear = 1;
static const uint kGradientRadial = 2;
static const uint kGaugeLinear = 1;
static const uint kGaugeNarrowWedge = 2;
static const uint kPartFill = 1;
static const uint kPartStroke = 2;
static const uint kPartMaskRegion = 3;

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

// ===== 距離の関数（SDF）=====
// どの関数も「位置 p から図形の輪郭までの距離」を返す。外がプラス、内がマイナス、輪郭がちょうど 0
// 単位は描かれるピクセルなので、距離に足し引きするだけで、太らせる・細らせる・縁取りができる

// --- 角丸四角形（4つの角で半径が違ってよい）---
float32_t SdRoundedBox(float32_t2 p, float32_t2 halfSize, float32_t4 radius)
{
    // p のある側の角の半径を選ぶ（右上 y・左上 x・右下 z・左下 w）
    float32_t r = (p.x > 0.0f) ? ((p.y > 0.0f) ? radius.y : radius.z) : ((p.y > 0.0f) ? radius.x : radius.w);
    // 右上の角の丸の中心から測る。q.x と q.y の両方が＋の所が「角の丸」、片方だけ＋の所が「まっすぐな辺」
    float32_t2 q = abs(p) - halfSize + r;
    return min(max(q.x, q.y), 0.0f) + length(max(q, 0.0f)) - r;
}

// --- 楕円：一番近い楕円の上の点を、くり返しで探す ---
// t は楕円の上の点の向き（cos, sin）。曲率の中心から見た p の向きへ t を動かすと、4回で近い点に落ち着く
float32_t SdEllipse(float32_t2 p, float32_t2 radius)
{
    float32_t2 q = abs(p); // 右上の 1/4 だけ考える
    float32_t2 t = float32_t2(0.70710678f, 0.70710678f);
    float32_t k = radius.x * radius.x - radius.y * radius.y;
    for (int i = 0; i < 4; ++i)
    {
        float32_t2 onEllipse = radius * t;
        float32_t2 evolute = float32_t2(k * t.x * t.x * t.x / radius.x, -k * t.y * t.y * t.y / radius.y); // 曲率の中心
        float32_t2 toEllipse = onEllipse - evolute;
        float32_t2 toPoint = q - evolute;
        t = saturate((toPoint * length(toEllipse) / max(length(toPoint), 1e-5f) + evolute) / radius);
        t = normalize(max(t, 1e-5f));
    }
    float32_t distance = length(q - radius * t);
    float32_t2 normalized = q / radius;
    return (dot(normalized, normalized) < 1.0f) ? -distance : distance;
}

// --- 平行四辺形：halfWidth は真ん中の高さの幅の半分、skew は上の辺の真ん中の横のずれ ---
float32_t SdParallelogram(float32_t2 p, float32_t halfWidth, float32_t halfHeight, float32_t skew)
{
    float32_t2 edge = float32_t2(skew, halfHeight); // 真ん中から上の辺の真ん中への向き（斜めの辺の向き）
    p = (p.y < 0.0f) ? -p : p; // 点対称なので上半分だけ考える
    // 上の辺までの距離（2乗）と、内外の符号の材料
    float32_t2 w = p - edge;
    w.x -= clamp(w.x, -halfWidth, halfWidth);
    float32_t2 d = float32_t2(dot(w, w), -w.y);
    // 斜めの辺までの距離（2乗）
    float32_t s = p.x * edge.y - p.y * edge.x;
    p = (s < 0.0f) ? -p : p;
    float32_t2 v = p - float32_t2(halfWidth, 0.0f);
    v -= edge * clamp(dot(v, edge) / max(dot(edge, edge), 1e-5f), -1.0f, 1.0f); // 角を丸め切ると edge が 0 になる（カプセルの形）
    d = min(d, float32_t2(dot(v, v), halfWidth * halfHeight - abs(s)));
    return sqrt(d.x) * sign(-d.y);
}

// --- 今の図形の距離 ---
float32_t CalcDistance(float32_t2 p)
{
    switch (gMaterial.shape)
    {
        case kShapeRectangle:
        case kShapeRoundedRectangle:
            return SdRoundedBox(p, gMaterial.shapeHalf, gMaterial.cornerRadius);
        case kShapeCircle:
            return length(p) - gMaterial.shapeParam.x;
        case kShapeEllipse:
            return SdEllipse(p, max(gMaterial.shapeHalf, 1e-3f));
        case kShapeRing:
        // 太さの真ん中の円からの距離 − 太さの半分
            return abs(length(p) - gMaterial.shapeParam.x) - gMaterial.shapeParam.y;
        case kShapeParallelogram:
        // 形を内側へ縮めた物の距離から半径を引くと、角が丸くなる
            return SdParallelogram(p, gMaterial.shapeHalf.x, gMaterial.shapeHalf.y, gMaterial.shapeParam.x) - gMaterial.shapeParam.y;
    }
    return 1.0f;
}

// ===== 塗りの色（グラデーション）=====
float32_t4 CalcPaintColor(float32_t2 p)
{
    float32_t t = 0.0f;
    switch (gMaterial.gradientType)
    {
        case kGradientLinear:
            t = dot(p, gMaterial.gradientAxis) + 0.5f;
            break;
        case kGradientRadial:
            t = length(p * gMaterial.gradientAxis);
            break;
        default:
            return gMaterial.color;
    }
    return lerp(gMaterial.color, gMaterial.gradientColor, saturate((t - gMaterial.gradientStart) * gMaterial.gradientScale));
}

// ===== ゲージ =====
// 見せる所がマイナス、切る所がプラスの距離（図形の距離と同じ向き）
float32_t CalcGaugeDistance(float32_t2 p)
{
    float32_t a = dot(p, gMaterial.gaugeAxisA);
    if (gMaterial.gaugeMode == kGaugeLinear)
    {
        return a - gMaterial.gaugeOffset;
    }
    float32_t b = dot(p, gMaterial.gaugeAxisB);
    // 2本の線の両方の内側（重なり）か、どちらかの内側（合わせた所）か
    return (gMaterial.gaugeMode == kGaugeNarrowWedge) ? max(a, b) : min(a, b);
}


PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float32_t2 p = input.texcoord; // 図形の真ん中を原点にした、描かれるピクセルの場所（Y は上が＋）
    float32_t distance = CalcDistance(p);
    
    // ===== 1. マスクの形（色は出さず、輪郭とゲージの内側で、ピクセルの半分より中だけを残す。ステンシルに書く・消す）=====
    if (gMaterial.drawPart == kPartMaskRegion)
    {
        float32_t region = Coverage(distance);
        if (gMaterial.gaugeMode != 0)
        {
            region *= Coverage(CalcGaugeDistance(p));
        }
        clip(region - 0.5f);
        output.color = float32_t4(0.0f, 0.0f, 0.0f, 0.0f); // 透明（Normal の重ね方なので、色は変わらない）
        return output;
    }

    // ===== 2. 塗りと縁取りの色 =====
    float32_t4 paint = CalcPaintColor(p);
    float32_t4 stroke = (gMaterial.strokeUsesGradient != 0) ? paint : gMaterial.strokeColor;

    // ===== 3. 塗りと縁取りの間（strokeInner の線）で、色を切り替える =====
    // 透明度の違う色を混ぜるので、色に透明度を掛けた形（premultiplied）で足す
    float32_t fillMask = Coverage(distance - gMaterial.strokeInner);
    float32_t fillAlpha = paint.a * gMaterial.fillOpacity * fillMask;
    float32_t strokeAlpha = stroke.a * (1.0f - fillMask);
    // マスクの図形は、塗り（子より奥）と縁取り（子より手前）を分けて描く
    if (gMaterial.drawPart == kPartFill)
    {
        strokeAlpha = 0.0f;
    }
    else if (gMaterial.drawPart == kPartStroke)
    {
        fillAlpha = 0.0f;
    }
    float32_t alpha = fillAlpha + strokeAlpha;
    float32_t3 rgb = (paint.rgb * fillAlpha + stroke.rgb * strokeAlpha) / max(alpha, 1e-5f);

    // ===== 4. 図形全体の輪郭（strokeOuter の線）とゲージの切り口で、透明にする =====
    alpha *= Coverage(distance - gMaterial.strokeOuter);
    if (gMaterial.gaugeMode != 0)
    {
        alpha *= Coverage(CalcGaugeDistance(p));
    }

    // ===== 5. 重ね方に合わせる（板は図形より広いので、図形の外が背景を変えないように）=====
    ApplyBlendFix(gMaterial.blendMode, rgb, alpha);
    
    output.color = float32_t4(rgb, alpha);
    return output;
}
#HLSL_END