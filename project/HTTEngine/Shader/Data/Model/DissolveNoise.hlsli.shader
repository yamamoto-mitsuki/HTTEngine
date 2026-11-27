#META
stage: Include
#META_END

#HLSL
// ディゾルブの画像を読む・しきい値。C++ の DissolveNoise.cpp と同じ読み方（片方を変えたら、もう片方も直す）
// gTextures・gSampler（Resources/Texture.hlsli）を使うので、その後に include する

// --- リニア → sRGB（1つの値）---
// テクスチャは sRGB の色として読み込まれ、GPU が読むときにリニアへ直す。ノイズは色ではなく数なので、ファイルの数に戻す
float DissolveLinearToSrgb(float value)
{
    return (value <= 0.0031308f) ? value * 12.92f : 1.055f * pow(value, 1.0f / 2.4f) - 0.055f;
}

// --- 画像の値（0〜1。ファイルの数そのまま）---
// ミップの 0 段目だけを読む（CPU は元の大きさの画素で計算するので、遠くでぼけた値と比べないように）
// gSampler は WRAP なので、uv × scale が 1 を超えたら反対側から読む（Scale が整数なら、UV の端で模様がつながる）
float DissolveNoise(uint textureIndex, float2 uv, uint scale)
{
    float value = gTextures[textureIndex].SampleLevel(gSampler, uv * (float)max(scale, 1u), 0.0f).r;
    return DissolveLinearToSrgb(saturate(value));
}

// --- しきい値（これより小さい値の所を捨てる）。amount 0 で −幅（何も捨てず光らない）、1 で 1 ---
float DissolveCut(float amount, float edgeWidth)
{
    return amount * (1.0f + edgeWidth) - edgeWidth;
}
#HLSL_END
