#pragma once
#include <cstdint>

#include "HTTEngine/Math/Vector2.h"

/// <summary>
/// モデルのディゾルブの画像を、CPU で読む（粒をディゾルブの境目から出すため）
/// <para>Shader/Data/Model/DissolveNoise.hlsli と同じ読み方（Linear・WRAP・ミップ 0 段目・ファイルの数そのまま）。片方を変えたら、もう片方も直す</para>
/// <para>画素は、画像ごとに初めて使ったときに1回だけファイルから読んで持つ（全部のテクスチャの画素を持つと重いので、使う物だけ）。Clear で捨てる</para>
/// </summary>
namespace DissolveNoise {
/// <summary>
/// UV の点の、画像の値を読む
/// </summary>
/// <param name="textureHandle">模様の画像（DissolveParams::noiseTextureHandle。0 なら既定の画像）</param>
/// <param name="uv">表面の点の UV（uvTransform を掛けない元の物）</param>
/// <param name="scale">画像を並べる回数（1 未満は 1）</param>
/// <param name="value">読んだ値（0〜1）</param>
/// <returns>読めたか（ファイルとして無い画像＝モデルに埋め込みの画像などは読めない。そのときは1回だけ警告）</returns>
bool Evaluate(uint32_t textureHandle, const Vector2& uv, int scale, float& value);

// しきい値（これより小さい値の所が消えている）。amount 0 で −幅、1 で 1（DissolveNoise.hlsli の DissolveCut と同じ式）
float Cut(float amount, float edgeWidth);

// 持っている画素を全部捨てる（ParticleSystem::Release から）
void Clear();
} // namespace DissolveNoise
