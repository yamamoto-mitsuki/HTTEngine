#META
stage: Include
#META_END

#HLSL
// モデルの面の色（光を当てる前の、物そのものの色）を作る所。Model の PS はどれもここを通す
// 物の色 = tint（物ごとの色）× baseColor（マテリアルの色）× テクスチャ（glTF の baseColorFactor × baseColorTexture と同じ形）
// 光の当て方（Lambert・PBR・トゥーンなど）は、それぞれの PS が書く
#include "Resources/Texture.hlsli"

// ===== マテリアル =====
// C++ の Material3dData（ShaderConstants.h）と同じ並び。大きさは C++ の static_assert で 160 バイトに止めている
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
    float2 pad;
};
ConstantBuffer<ModelMaterial> gMaterial : register(b20);

// ===== 面の色 =====
struct ModelSurface
{
    float32_t3 baseColor; // tint × マテリアルの色 × テクスチャ（リニア）。光を当てる前の色
    float alpha;          // tint の A × マテリアルの A × テクスチャの A
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
    ModelSurface surface;
    surface.baseColor = gMaterial.tint.rgb * gMaterial.baseColor.rgb * texColor.rgb;
    surface.alpha = gMaterial.tint.a * materialAlpha;
    return surface;
}
#HLSL_END
