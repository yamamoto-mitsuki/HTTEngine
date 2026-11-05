#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "MyEngine/Math/Vector4.h"

// .shader の #PROPERTIES（マテリアルの値の意味）と、それを定数バッファの中の場所に結んだ物
// 並び・場所・大きさはリフレクション（ShaderConstantBuffer）から取り、#PROPERTIES には意味（種類・表示名・既定値・範囲）だけを書く


// マテリアルの値が入る定数バッファの変数名（RootSignatureManager::NameToRole の Material と同じ名前）
inline constexpr std::string_view kMaterialConstantBufferName = "gMaterial";
// 描き手（デカールの予告など）が描く物ごとに送る定数バッファの変数名（NameToRole の DrawData と同じ名前）
inline constexpr std::string_view kDrawDataConstantBufferName = "gDrawData";


/// <summary>
/// #PROPERTIES の値の種類（Inspector の欄の形と、定数バッファに詰める形が決まる）
/// </summary>
enum class ShaderPropertyType {
	Float,   // 数1つ（float）
	Range,   // 範囲のある数1つ（float。min〜max の中だけ）
	Toggle,  // 入れる・外す（uint・int・bool。0 か 1）
	Vector,  // 数 1〜4 個（float〜float4）
	Color,   // 色（float3・float4。Inspector と .material は sRGB、送るときにリニアへ直す）
	Texture, // テクスチャ（uint。gTextures[] の番号を送る。選んでいなければ白）
};


/// <summary>
/// #PROPERTIES の1行（.shader に書いた、値1つの意味）
/// </summary>
struct ShaderPropertyDef {
	std::string name;                                // gMaterial の中の変数名（.material に保存するときの名前にもなる）
	ShaderPropertyType type = ShaderPropertyType::Float; // 種類
	std::string displayName;                         // Inspector に出す名前
	std::string tooltip;                             // マウスを乗せたときの説明（書かなければ空）
	Vector4 defaultValue = {0.0f, 0.0f, 0.0f, 0.0f}; // 既定の値（Float・Range・Toggle は x だけ使う）
	float min = 0.0f;                                // Range の下限
	float max = 0.0f;                                // Range の上限

	bool operator==(const ShaderPropertyDef&) const = default;
};


/// <summary>
/// 定数バッファの中の場所を付けた値1つ
/// </summary>
struct ShaderProperty {
	ShaderPropertyDef def;   // .shader に書いた意味
	uint32_t offset = 0;     // gMaterial の先頭からのバイト
	uint32_t components = 1; // 成分の数（float は 1、float3 の色は 3）
};


/// <summary>
/// マテリアルの値の並び（描画プログラム1つに1つ。ShaderPackageLoader::GetPropertyLayout が作る）
/// </summary>
struct ShaderPropertyLayout {
	uint32_t bufferSize = 0;                // gMaterial の大きさ（バイト。0 なら gMaterial を使わない Shader）
	std::vector<ShaderProperty> properties; // #PROPERTIES に書いた順

	// 変数名で探す（無ければ nullptr）
	const ShaderProperty* Find(std::string_view name) const {
		const auto it = std::ranges::find_if(properties, [name](const ShaderProperty& property) { return property.def.name == name; });
		return (it != properties.end()) ? &*it : nullptr;
	}
};
