#META
stage: Include
#META_END

#HLSL
// モデルの面の色（光を当てる前の、物そのものの色）を作る所。Model の PS はどれもここを通す
// 物の色 = tint（物ごとの色）× baseColor（マテリアルの色）× テクスチャ（glTF の baseColorFactor × baseColorTexture と同じ形）
// 自己発光 = emissive × emissiveIntensity × 自己発光のテクスチャ（glTF の emissiveFactor × emissive_strength × emissiveTexture と同じ形）＋ ディゾルブの境目の光
// 光の当て方（Lambert・PBR・トゥーンなど）は、それぞれの PS が書く
#include "Resources/Texture.hlsli"
#include "DissolveNoise.hlsli"

// ===== マテリアル =====
// C++ の Material3dData（ShaderConstants.h）と同じ並び。大きさは C++ の static_assert で 192 バイトに止めている
struct ModelMaterial
{
    float32_t4 tint;         // 物ごとの色（Model Renderer の Color × Intensity。リニア）
    float32_t4 baseColor;    // マテリアルの色（リニア。A はマテリアルの不透明度）
    float4x4 uvTransform;    // UV をずらす・回す行列
    float32_t3 ambient;      // 影の所の明るさ（PBR で、環境光（IBL）が無いときだけ使う）
    float alphaCutoff;       // これ以下のアルファ（マテリアルの A × テクスチャの A）のピクセルを捨てる
    float32_t3 specular;     // ハイライトの色（Phong・BlinnPhong）
    float shininess;         // ハイライトの鋭さ（Phong・BlinnPhong）
    float32_t3 emissive;     // 自己発光の色
    uint textureIndex;       // 物の色のテクスチャ（gTextures の番号。無いときは白）
    float metallic;          // 金属っぽさ（PBR）
    float roughness;         // 表面の粗さ（PBR）
    uint emissiveTextureIndex; // 自己発光のテクスチャ（gTextures の番号。無いときは白）
    float emissiveIntensity;   // 自己発光の強さ（emissive に掛ける。1 より大きいと HDR の明るさ）
    // --- ディゾルブ（C++ の Material3dData と同じ並び）---
    float32_t3 dissolveEdgeColor; // 境目の光（リニア。Edge Color × Edge Intensity）
    float dissolveAmount;         // 消えた割合（0 なら画像を読まない）
    float dissolveEdgeWidth;      // 光る境目の幅
    uint dissolveScale;           // 画像を並べる回数
    uint dissolveTextureIndex;    // 模様の画像（gTextures の番号）
    float dissolvePad;            // 16 バイトにそろえる余り
};
ConstantBuffer<ModelMaterial> gMaterial : register(b20);

// ===== 面の色 =====
struct ModelSurface
{
    float32_t3 baseColor;    // tint × マテリアルの色 × テクスチャ（リニア）。光を当てる前の色
    float alpha;             // tint の A × マテリアルの A × テクスチャの A
    float32_t3 emissive;     // 自己発光（emissive × 強さ × 自己発光のテクスチャ ＋ ディゾルブの境目の光。リニア）。光の計算の後に足す（ライトが無くても光る）
    float32_t3 dissolveGlow; // ディゾルブの境目の光（リニア。emissive にも足してある。自己発光を足さない Unlit が、これだけを足す）
};

// --- テクスチャを読み、色を掛けて、切り抜く（切り抜いたピクセルは、ここで捨てる）---
// 切り抜きはマテリアルの A（baseColor の A × テクスチャの A）で比べ、tint の A は入れない
// （Color の A で物をフェードさせたとき、穴が開いていく見た目にならないように。glTF・Unity も同じ）
ModelSurface GetModelSurface(float2 texcoord)
{
    float2 uv = mul(float32_t4(texcoord, 0.0f, 1.0f), gMaterial.uvTransform).xy;
    float32_t4 texColor = gTextures[gMaterial.textureIndex].Sample(gSampler, uv);
    float materialAlpha = gMaterial.baseColor.a * texColor.a;
    if (materialAlpha <= gMaterial.alphaCutoff) // alphaCutoff が 0 なら、ちょうど 0 のピクセルだけ捨てる
    {
        discard;
    }
    // --- ディゾルブ：UV の上で画像を読み、しきい値より小さい所を捨て、境目を光らせる ---
    // 元の texcoord で読む（uvTransform を掛けない。テクスチャを流しても、消える所は動かない）
    // 深度のパス（ModelDepth）もここを通るので、消えた所は深度・影も消える
    float dissolveEdge = 0.0f; // 境目の光の強さ（境目ちょうどで 1、幅だけ離れて 0）
    if (gMaterial.dissolveAmount > 0.0f) // 0 なら画像を読まない（物ごとに同じ値なので、分かれても遅くならない）
    {
        float noise = DissolveNoise(gMaterial.dissolveTextureIndex, texcoord, gMaterial.dissolveScale);
        float cut = DissolveCut(gMaterial.dissolveAmount, gMaterial.dissolveEdgeWidth);
        if (noise < cut || gMaterial.dissolveAmount >= 1.0f) // 1 なら真っ白の所も残さない
        {
            discard;
        }
        if (gMaterial.dissolveEdgeWidth > 0.0f)
        {
            dissolveEdge = 1.0f - saturate((noise - cut) / gMaterial.dissolveEdgeWidth);
        }
    }
    ModelSurface surface;
    surface.baseColor = gMaterial.tint.rgb * gMaterial.baseColor.rgb * texColor.rgb;
    surface.alpha = gMaterial.tint.a * materialAlpha;
    // 自己発光のテクスチャは物の色と同じ UV で読む（glTF の emissiveTexture も、ふつうは同じ UV）。tint は掛けない（今までと同じ）
    float32_t3 emissiveTex = gTextures[gMaterial.emissiveTextureIndex].Sample(gSampler, uv).rgb;
    surface.dissolveGlow = gMaterial.dissolveEdgeColor * dissolveEdge;
    surface.emissive = gMaterial.emissive * gMaterial.emissiveIntensity * emissiveTex + surface.dissolveGlow; // 境目の光は、光の計算の後に足す（影の所でも光る）
    return surface;
}
#HLSL_END
