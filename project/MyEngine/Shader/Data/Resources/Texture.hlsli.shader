#META
stage: Include
#META_END

#HLSL
// バインドレス2Dテクスチャ（Material.textureIndex で引く）
Texture2D<float32_t4> gTextures[] : register(t0);

// サンプラー。UV がはみ出したときの読み方は、RootSignatureManager::MakeStaticSampler が変数名で決める
SamplerState gSampler : register(s0); // 繰り返す（WRAP）。モデル・スプライトのテクスチャ用
SamplerState gSamplerClamp : register(s1); // 端の色で止める（CLAMP）。画面の絵を読むポストエフェクト用
#HLSL_END