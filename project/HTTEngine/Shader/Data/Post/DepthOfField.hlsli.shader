#META
stage: Include
#META_END

#HLSL
#include "Resources/Texture.hlsli"

// ===== 被写界深度の値（C++ の DepthOfFieldPass::Param と同じ並び）=====
struct DofParam
{
    uint32_t2 fullSize; // 元の絵の大きさ（ピクセル）
    uint32_t2 halfSize; // 半分の大きさ（ピクセル。CS のはみ出したスレッドを止める）
    float32_t2 halfTexelSize; // 1 / 半分の大きさ（UV の計算用）
    float projectionA; // 投影行列の m[2][2]
    float projectionB; // 投影行列の m[3][2]
    float focusDistance; // ピントの合う奥行き（m）
    float focusRange; // 前後この奥行きまではぼけない（m）
    float farTransition; // 奥：一番ぼけるまでの奥行き（m）
    float nearTransition; // 手前：一番ぼけるまでの奥行き（m。0 なら手前はぼかさない）
    float maxRadius; // 一番ぼけた所の半径（半分の大きさのピクセル）
    float radiusStep; // ぼかすときに読む点の間隔（半分の大きさのピクセル）
    uint32_t colorIndex; // 元の絵の色（SRV の番号）
    uint32_t depthIndex; // 元の絵の深度（SRV の番号）
    uint32_t halfIndex; // 縮めた色＋ぼけの大きさ（SRV の番号）
    uint32_t blurIndex; // ぼかした色（SRV の番号）
    float32_t2 padding;
};
ConstantBuffer<DofParam> gDof : register(b0);

// ===== 深度バッファの値（0〜1）→ カメラからの奥行き（m）=====
// 透視投影の深度は 深度 = A + B / 奥行き なので、奥行き = B / (深度 − A)
// 深度は手前ほど細かく、奥ほど粗い（0.99 と 1.0 の間に、何百 m も入っている）ので、そのままでは「何 m 奥か」に使えない
float ToViewDepth(float depth)
{
    return gDof.projectionB / (depth - gDof.projectionA);
}

// ===== ぼけの大きさ（-1〜1。0：ピントが合っている / プラス：奥にぼける / マイナス：手前にぼける）=====
// ピントの前後 focusRange まではぼけず、そこから Transition の奥行きで一番ぼける（1）まで増える
float CalcCoc(float depth)
{
    float distance = ToViewDepth(depth) - gDof.focusDistance; // ピントの所から、どれだけ奥か（マイナスは手前）
    float farCoc = saturate((distance - gDof.focusRange) / gDof.farTransition);
    float nearCoc = (gDof.nearTransition > 0.0f) ? saturate((-distance - gDof.focusRange) / gDof.nearTransition) : 0.0f;
    return farCoc - nearCoc;
}
#HLSL_END