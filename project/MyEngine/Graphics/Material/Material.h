#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "MyEngine/Graphics/Pipeline/RenderStates.h"
#include "MyEngine/Graphics/Pipeline/ShaderProperty.h"
#include "MyEngine/Math/Vector4.h"

// マテリアル（.material の中身）＝ 使う Shader ＋ その Shader の #PROPERTIES の値 ＋ 描くときの状態の上書き
// 同じファイルを選んだ物はみんな同じ見た目になる（物ごとに変えたいときは、ファイルを分ける）
// Shader はパスで指すので、手で書いた .shader でも、後でグラフが作った .shader でも同じに扱える


/// <summary>
/// マテリアルの値1つ（Shader の #PROPERTIES の1行に1つ）
/// </summary>
struct MaterialValue {
	std::string name;                                 // 変数名（#PROPERTIES の名前。.material に保存するときの名前）
	ShaderPropertyType type = ShaderPropertyType::Float; // 種類（#PROPERTIES と同じ）
	Vector4 value = {0.0f, 0.0f, 0.0f, 0.0f};         // 値（Float・Range・Toggle は x だけ。Color は sRGB）。Curve を付けたら「始まりの値」
	uint32_t texture = 0;                             // Texture の番号（TextureManager::Load。0 なら白）
	uint32_t curve = 0;                               // 付けた Curve（CurveManager::Load の番号。0 なら動かさない）。Texture には付けられない
	Vector4 endValue = {0.0f, 0.0f, 0.0f, 0.0f};      // 終わりの値（Curve の出力が 0 なら value、1 なら endValue。間は補間、外ははみ出す）

	bool operator==(const MaterialValue&) const = default;
};


/// <summary>
/// マテリアル（.material の中身）
/// </summary>
struct Material {
	uint32_t shader = 0; // 使う Shader（AssetType::Shader の番号＝描画プログラムの番号 + 1。0 は未選択）

	// --- 描くときの状態（入れた物だけ、Shader の #PROGRAM の既定を上書きする）---
	bool overrideBlend = false;                            // true：blend を使う
	BlendMode blend = BlendMode::Normal;                   // 重ね方
	bool overrideRasterizer = false;                       // true：rasterizer を使う
	RasterizerType rasterizer = RasterizerType::SolidBack; // 裏面
	bool overrideDepth = false;                            // true：depth を使う
	DepthMode depth = DepthMode::TestWrite;                // 深度

	std::vector<MaterialValue> values; // 値（Shader の #PROPERTIES の順）

	bool operator==(const Material&) const = default;
};


/// <summary>
/// 描くときの形（MaterialManager::Resolve が、マテリアルと Curve の横軸から作る）
/// </summary>
struct ResolvedMaterial {
	uint32_t programId = 0;                                // 描画プログラムの番号（PSOKey の shaderProgramID）
	DrawCategory category{};                               // Shader の #PROGRAM の category（描き手が、自分の描き方の Shader か確かめる）
	BlendMode blend = BlendMode::Normal;                   // 重ね方（上書きが無ければ Shader の既定）
	RasterizerType rasterizer = RasterizerType::SolidBack; // 裏面
	DepthMode depth = DepthMode::TestWrite;                // 深度
	std::vector<std::byte> constants;                      // gMaterial に送る中身（gMaterial の並びどおり。空なら送らない）
};
