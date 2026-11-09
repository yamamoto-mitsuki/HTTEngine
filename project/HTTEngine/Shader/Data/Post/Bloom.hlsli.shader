#META
stage: Include
#META_END

#HLSL
#include "Resources/Texture.hlsli"

struct BloomParam
{
    float32_t2 texelSize; // 1 / ソースの解像度
    float threshold;      // これより明るい所だけ光らせる
    float knee;           // しきい値付近の柔らかさ
    float intensity;      // 合成の強さ
    uint32_t srcIndex;    // ソースのSRVスロット
    uint32_t addIndex;    // 足し込む側のSRVスロット
    float padding;
    float32_t2 direction; // ガウスでぼかす向き（横 (1,0)・縦 (0,1)）
    float32_t2 padding2;
    float32_t4 levelWeights; // 川瀬式の合成：段ごとの重み（1/4・1/8・1/16・1/32）
    uint32_t4 levelIndices;  // 川瀬式の合成：段の SRV スロット
};
ConstantBuffer<BloomParam> gBloom : register(b0);

struct VSOutput
{
    float32_t4 position : SV_Position;
    float32_t2 texcoord : TEXCOORD0;
};

// 3x3のテント型フィルタ。1回のパスで滑らかにぼかす
// 画面の端では外側を読むので、CLAMP のサンプラーで読む（WRAP だと反対の端の光が混ざる）
float32_t3 Tent(uint32_t index, float32_t2 uv, float32_t2 texel)
{
    float32_t3 sum = float32_t3(0, 0, 0);
    sum += gTextures[index].Sample(gSamplerClamp, uv + float32_t2(-texel.x, -texel.y)).rgb * 1.0f;
    sum += gTextures[index].Sample(gSamplerClamp, uv + float32_t2(0.0f, -texel.y)).rgb * 2.0f;
    sum += gTextures[index].Sample(gSamplerClamp, uv + float32_t2(texel.x, -texel.y)).rgb * 1.0f;
    sum += gTextures[index].Sample(gSamplerClamp, uv + float32_t2(-texel.x, 0.0f)).rgb * 2.0f;
    sum += gTextures[index].Sample(gSamplerClamp, uv).rgb * 4.0f;
    sum += gTextures[index].Sample(gSamplerClamp, uv + float32_t2(texel.x, 0.0f)).rgb * 2.0f;
    sum += gTextures[index].Sample(gSamplerClamp, uv + float32_t2(-texel.x, texel.y)).rgb * 1.0f;
    sum += gTextures[index].Sample(gSamplerClamp, uv + float32_t2(0.0f, texel.y)).rgb * 2.0f;
    sum += gTextures[index].Sample(gSamplerClamp, uv + float32_t2(texel.x, texel.y)).rgb * 1.0f;
    return sum * (1.0f / 16.0f);
}
#HLSL_END