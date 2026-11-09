#META
stage: PS
profile : ps_6_0
#META_END

#PROGRAM
category: Polyline
shading : Unlit
vs:       PolylineVS
#PROGRAM_END


#HLSL
#include "VectorShape/Polyline.hlsli"
#include "VectorShape/VectorCoverage.hlsli"

// C++ の PolylinePiece と同じ順の番号
static const uint kPieceSegment = 0;
static const uint kPieceCorner = 1;
static const uint kPieceCap = 2;
static const uint kPieceFillInside = 3;
static const uint kPieceFillEdge = 4;
static const uint kPieceFillCorner = 5;
// C++ の PolylineCap と同じ順の番号
static const uint kCapButt = 0;
static const uint kCapRound = 1;
// C++ の UIDrawPart と同じ順の番号
static const uint kPartMaskRegion = 3;

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

// ===== 端の距離 =====
// local：x は端から外へ（線の中はマイナス）、y は線の真ん中から横へ
float32_t CapDistance(float32_t2 local, uint cap)
{
    float32_t side = abs(local.y) - gMaterial.halfWidth;
    switch (cap)
    {
        case kCapButt:
            return max(side, local.x); // 端でまっすぐ切る
        case kCapRound:
            return length(float32_t2(max(local.x, 0.0f), local.y)) - gMaterial.halfWidth; // 端の点からの距離（半円）
        default:
            return max(side, local.x - gMaterial.halfWidth); // 太さの半分だけ伸ばして切る（四角）
    }
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    // ===== 1. 部分ごとに、輪郭までの距離を出す（外がプラス）=====
    // 直線の部分は線からの距離、角の部分は円からの距離。メッシュが部分を分けてあるので、ピクセルごとに1つ測るだけでよい
    float32_t d = 0.0f;
    bool isStroke = true;
    switch (input.piece)
    {
        case kPieceSegment:
            d = abs(input.local.y) - gMaterial.halfWidth;
            break;
        case kPieceCorner:
            d = abs(length(input.local) - input.param.x) - gMaterial.halfWidth;
            break;
        case kPieceCap:
            d = CapDistance(input.local, (uint) input.param.x);
            break;
        case kPieceFillInside:
            d = -1.0e5f; // 内側は全部塗る
            isStroke = false;
            break;
        case kPieceFillEdge:
            d = input.local.y * input.param.y; // param.y で、塗りの外が＋になる向きにそろえる
            isStroke = false;
            break;
        case kPieceFillCorner:
            d = (length(input.local) - input.param.x) * input.param.y;
            isStroke = false;
            break;
    }

    float32_t coverage = Coverage(d);

    // ===== 2. マスクの形（色は出さず、ピクセルの半分より中だけを残す。ステンシルに書く・消す）=====
    // 描く三角形（塗りの部分か線の部分か）は PolylineSystem が選ぶので、ここは形の内側だけを残せばよい
    if (gMaterial.drawPart == kPartMaskRegion)
    {
        clip(coverage - 0.5f);
        output.color = float32_t4(0.0f, 0.0f, 0.0f, 0.0f); // 透明（Normal の重ね方なので、色は変わらない）
        return output;
    }

    // ===== 3. 色と、輪郭のなめらかさ =====
    float32_t4 color = isStroke ? gMaterial.strokeColor : gMaterial.fillColor;
    float32_t alpha = color.a * coverage;
    float32_t3 rgb = color.rgb;

    // ===== 3. 重ね方に合わせる（メッシュは輪郭より少し広いので、外が背景を変えないように）=====
    ApplyBlendFix(gMaterial.blendMode, rgb, alpha);
    output.color = float32_t4(rgb, alpha);
    return output;
}
#HLSL_END