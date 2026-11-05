#META
stage: Include
#META_END


#HLSL
// 折れ線の VS と PS が共通で使う物

// 折れ線のマテリアル（C++ の PolylineMaterialData と同じ並び。VS は toScreen、PS は残りを読む）
struct Material
{
    float32_t4x4 toScreen; // 頂点の位置（Entity から見たピクセル）→ 画面の正規化座標
    float32_t4 fillColor; // 塗りの色
    float32_t4 strokeColor; // 線の色
    float32_t halfWidth; // 線の太さの半分
    uint blendMode; // 重ね方（BlendMode の順）
    uint drawPart; // 何を描くか（UIDrawPart の順。マスクの形だけを描くときは MaskRegion）
    float32_t padA;
};
ConstantBuffer<Material> gMaterial : register(b20);

struct VertexShaderOutput
{
    float32_t4 position : SV_POSITION;
    float32_t2 local : TEXCOORD0; // その部分から見た位置（三角形の中で直線に補間される）
    nointerpolation float32_t2 param : TEXCOORD1; // 部分ごとの値（三角形の中で同じ値）
    nointerpolation uint piece : TEXCOORD2; // 部分の種類
};
#HLSL_END