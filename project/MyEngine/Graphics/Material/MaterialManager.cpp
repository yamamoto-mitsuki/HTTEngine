#include "MaterialManager.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <externals/nlohmann/json.hpp>

#include "MyEngine/Core/AssetMoveRegistry.h"
#include "MyEngine/Curve/CurveManager.h"
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/ECS/ComponentSerializer.h"
#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/Graphics/Pipeline/ShaderPackageLoader.h"
#include "MyEngine/Graphics/Texture/TextureManager.h"
#include "MyEngine/Math/Color.h"
#include "MyEngine/Math/MathUtility.h"
#include "MyEngine/Scene/SceneSerializer.h"
#include "MyEngine/String/ConvertString.h"

namespace {
constexpr std::string_view kTypeName = "Material"; // 警告・Inspector の見出しに出す名前

/// <summary>
/// 読み込んだファイル1つ
/// </summary>
struct MaterialAsset {
	std::string path;  // ファイル（ToPathKey の形）
	Material material; // 中身
};

/// <summary>
/// MaterialManager が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct MaterialState {
	std::vector<MaterialAsset> assets; // 番号 - 1 番目のファイル
};
std::unique_ptr<MaterialState> state;

const std::string kEmptyPath; // 知らない番号のパス

// 番号のファイル（知らない番号なら nullptr）
MaterialAsset* Find(uint32_t handle) {
	if (state == nullptr || handle == 0 || handle > state->assets.size()) {
		return nullptr;
	}
	return &state->assets[handle - 1];
}

// Shader の番号の描画プログラム（未選択・知らない番号なら nullptr）
const ProgramEntry* FindProgram(uint32_t shader) {
	if (shader == 0 || shader > ShaderPackageLoader::GetProgramCount()) {
		return nullptr;
	}
	return &ShaderPackageLoader::GetProgramAt(shader - 1);
}

// 中身を JSON にする（見せ方 Describe 1つで、Inspector と同じ並び）
SceneJson ToJson(Material material) {
	SceneJson json;
	std::set<std::string> warnings;
	ComponentSerializer::WriteFields(kTypeName, [&material](ComponentUI& ui) { MaterialManager::Describe(ui, material); }, json, warnings);
	return json;
}

// ファイルに書く（前のファイルは .bak として1つ残る。Project ウィンドウには出ない）
bool WriteFile(const std::string& path, const Material& material) {
	if (!SceneSerializer::WriteFile(path, ToJson(material))) {
		LogManager::Error(std::format("Material を保存できませんでした: {}", path));
		return false;
	}
	return true;
}

//=============================================================================
// 見せ方の部品
//=============================================================================
// ===== 描くときの状態1つ（チェックを入れると、右の値で Shader の既定を上書きする）=====
// 外しているときは Shader の既定を灰色で見せる（値は変えない。保存には今の値を書く）
template<class E>
void DescribeState(ComponentUI& ui, const char* name, const char* key, bool& isOverride, E& value, E shaderDefault) {
	ui.Field(std::format("###{}Override", key).c_str(), isOverride, Tip("チェックを入れると、右の値で Shader の既定（#PROGRAM）を上書きする"));
	ui.SameLineAfterCheck();
	ui.BeginDisabled(!isOverride);
	E shown = isOverride ? value : shaderDefault;
	ui.Field(std::format("{}###{}", name, key).c_str(), shown);
	if (isOverride) {
		value = shown;
	}
	ui.EndDisabled();
}

// ===== 種類に合わせた欄1つ（値・終わりの値で共通。成分の数で形を変える）=====
void DescribeValueField(ComponentUI& ui, const std::string& label, Vector4& value, const ShaderProperty& property, FieldStyle style) {
	const ShaderPropertyDef& def = property.def;
	switch (def.type) {
	case ShaderPropertyType::Float:
		ui.Field(label.c_str(), value.x, style);
		break;
	case ShaderPropertyType::Range: {
		FieldStyle range = Range(def.min, def.max);
		range.tooltip = style.tooltip;
		ui.Field(label.c_str(), value.x, range);
		break;
	}
	case ShaderPropertyType::Toggle: {
		bool isOn = (value.x != 0.0f);
		ui.Field(label.c_str(), isOn, style);
		value.x = isOn ? 1.0f : 0.0f;
		break;
	}
	case ShaderPropertyType::Vector:
		// 成分の数に合わせた欄（使わない成分は 0 のまま）
		if (property.components == 1) {
			ui.Field(label.c_str(), value.x, style);
		} else if (property.components == 2) {
			Vector2 v = {value.x, value.y};
			ui.Field(label.c_str(), v, style);
			value.x = v.x;
			value.y = v.y;
		} else if (property.components == 3) {
			Vector3 v = {value.x, value.y, value.z};
			ui.Field(label.c_str(), v, style);
			value = {v.x, v.y, v.z, value.w};
		} else {
			ui.Field(label.c_str(), value, style);
		}
		break;
	case ShaderPropertyType::Color:
		// float3 の色は A を出さない
		if (property.components == 3) {
			Vector3 rgb = {value.x, value.y, value.z};
			ui.ColorField(label.c_str(), rgb, style);
			value = {rgb.x, rgb.y, rgb.z, 1.0f};
		} else {
			ui.ColorField(label.c_str(), value, style);
		}
		break;
	case ShaderPropertyType::Texture:
		break; // テクスチャは値ではなく番号（DescribeValue で欄を出す）
	}
}

// ===== 値1つ（値・Curve・終わりの値）=====
void DescribeValue(ComponentUI& ui, MaterialValue& value, const ShaderProperty& property) {
	const ShaderPropertyDef& def = property.def;
	const FieldStyle style = Tip(def.tooltip.empty() ? nullptr : def.tooltip.c_str());
	const std::string label = std::format("{}###{}", def.displayName, def.name); // 保存の名前は変数名
	// --- テクスチャ（Curve は付けられない）---
	if (def.type == ShaderPropertyType::Texture) {
		ui.AssetField(label.c_str(), value.texture, AssetType::Texture, style);
		return;
	}
	// --- 値（Curve を付けたら、始まりの値）---
	DescribeValueField(ui, label, value.value, property, style);
	// --- Curve と終わりの値（Curve を付けたときだけ、終わりの値を出す）---
	ui.AssetField(std::format("{} Curve###{}Curve", def.displayName, def.name).c_str(), value.curve, AssetType::Curve,
	    Tip("付けると、描き手が渡す進み具合（0〜1）を横軸にして、上の値（Curve の出力 0）から End（出力 1）へ動かす。(none) なら動かさない"));
	if (value.curve != 0) {
		DescribeValueField(ui, std::format("{} End###{}End", def.displayName, def.name), value.endValue, property, Tip("Curve の出力が 1 のときの値"));
	}
}

// ===== 送る形に詰める（float を1つずつ・uint を1つ）=====
void WriteFloats(std::vector<std::byte>& constants, uint32_t offset, const Vector4& value, uint32_t components) {
	std::memcpy(constants.data() + offset, &value.x, sizeof(float) * components); // Vector4 は x・y・z・w が並んでいる
}
void WriteUint(std::vector<std::byte>& constants, uint32_t offset, uint32_t value) { std::memcpy(constants.data() + offset, &value, sizeof(uint32_t)); }

// ===== 名前を変えた・動かしたファイル：覚えているパスを付け替える（番号はそのまま。中のテクスチャ・Curve は番号で持つので、それぞれの Manager が付け替える）=====
void OnAssetMoved(const MovedPath& moved) {
	if (state == nullptr) {
		return;
	}
	for (MaterialAsset& asset : state->assets) {
		moved.Apply(asset.path);
	}
}
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
void MaterialManager::Initialize() {
	MY_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<MaterialState>();
	AssetMoveRegistry::AddListener(&OnAssetMoved);
	AssetMoveRegistry::AddReferenceFileType(kFileExtension); // Shader・テクスチャ・Curve のパスを書く
}

void MaterialManager::Release() { state.reset(); }


//=============================================================================
// 読み込み
//=============================================================================
uint32_t MaterialManager::Load(const std::filesystem::path& filePath) {
	MY_ASSERT_MSG(state != nullptr, "MaterialManager::Initialize()を先に呼んでください");
	// --- 読み込み済みなら、同じ番号を返す ---
	const std::string key = ToPathKey(filePath);
	const auto cached = std::ranges::find(state->assets, key, &MaterialAsset::path);
	if (cached != state->assets.end()) {
		return static_cast<uint32_t>(cached - state->assets.begin()) + 1;
	}
	// --- ファイルから読む（ファイルに無い値は、Shader の #PROPERTIES の既定のまま。Shader に無い値は警告して捨てる）---
	std::ifstream stream(filePath);
	if (!stream) {
		LogManager::Warning(std::format("Material のファイルを開けませんでした: {}", key));
		return 0;
	}
	const SceneJson json = SceneJson::parse(stream, nullptr, false); // false: 壊れていても例外を投げず、discarded を返す
	if (json.is_discarded()) {
		LogManager::Error(std::format("Material のファイルが壊れています: {}", key));
		return 0;
	}
	Material material;
	std::set<std::string> warnings;
	ComponentSerializer::ReadFields(kTypeName, [&material](ComponentUI& ui) { Describe(ui, material); }, json, warnings);
	for (const std::string& warning : warnings) {
		LogManager::Warning(std::format("{}: {}", key, warning));
	}
	state->assets.push_back({key, std::move(material)});
	const uint32_t handle = static_cast<uint32_t>(state->assets.size());
	LogManager::Log(std::format("Loaded material: {} -> {}", key, handle));
	return handle;
}

const std::string& MaterialManager::GetPath(uint32_t handle) {
	const MaterialAsset* asset = Find(handle);
	return (asset != nullptr) ? asset->path : kEmptyPath;
}

const Material* MaterialManager::Get(uint32_t handle) {
	const MaterialAsset* asset = Find(handle);
	return (asset != nullptr) ? &asset->material : nullptr;
}


//=============================================================================
// 書き換え・保存
//=============================================================================
void MaterialManager::Set(uint32_t handle, const Material& material) {
	if (MaterialAsset* asset = Find(handle)) {
		asset->material = material;
	}
}

bool MaterialManager::Save(uint32_t handle) {
	const MaterialAsset* asset = Find(handle);
	return (asset != nullptr) && WriteFile(asset->path, asset->material);
}

bool MaterialManager::CreateNewFile(const std::filesystem::path& filePath) {
	std::error_code error;
	if (std::filesystem::exists(filePath, error)) {
		return false; // 上書きしない
	}
	return WriteFile(ToUtf8(filePath), Material{});
}


//=============================================================================
// 描くときの形にする
//=============================================================================
bool MaterialManager::Resolve(uint32_t handle, float progress, ResolvedMaterial& out) {
	const MaterialAsset* asset = Find(handle);
	if (asset == nullptr) {
		return false;
	}
	const Material& material = asset->material;
	const ProgramEntry* program = FindProgram(material.shader);
	if (program == nullptr) {
		return false; // Shader が未選択
	}
	// --- Shader と、描くときの状態（上書きした物だけマテリアルの値）---
	out.programId = material.shader - 1;
	out.category = program->category;
	out.blend = material.overrideBlend ? material.blend : program->states.blend;
	out.rasterizer = material.overrideRasterizer ? material.rasterizer : program->states.rasterizer;
	out.depth = material.overrideDepth ? material.depth : program->states.depth;

	// --- gMaterial に詰める（#PROPERTIES に書かなかったメンバーは 0）---
	const ShaderPropertyLayout& layout = ShaderPackageLoader::GetPropertyLayout(out.programId);
	out.constants.assign(layout.bufferSize, std::byte{0});
	for (const MaterialValue& value : material.values) {
		const ShaderProperty* property = layout.Find(value.name);
		if (property == nullptr || property->def.type != value.type) {
			continue; // Shader と合わない（MatchValues で合わせ直しているので、ふつうは来ない）
		}
		// Curve を付けた値は、始まりと終わりの間（Curve の出力 0 で始まり、1 で終わり）
		Vector4 current = value.value;
		if (const Curve* curve = (value.curve != 0) ? CurveManager::Get(value.curve) : nullptr) {
			current = Lerp(value.value, value.endValue, curve->Evaluate(progress));
		}
		switch (value.type) {
		case ShaderPropertyType::Float:
		case ShaderPropertyType::Vector:
			WriteFloats(out.constants, property->offset, current, property->components);
			break;
		case ShaderPropertyType::Range:
			current.x = std::clamp(current.x, property->def.min, property->def.max); // Curve ではみ出しても範囲の中
			WriteFloats(out.constants, property->offset, current, 1);
			break;
		case ShaderPropertyType::Toggle:
			WriteUint(out.constants, property->offset, (current.x >= 0.5f) ? 1u : 0u); // Curve で動かしたときは半分で切り替わる
			break;
		case ShaderPropertyType::Color:
			// Inspector の色は sRGB。シェーダーはリニアで計算するので直して送る（A はそのまま）
			WriteFloats(out.constants, property->offset, SrgbToLinear(current), property->components);
			break;
		case ShaderPropertyType::Texture:
			// gTextures[] の番号（選んでいなければ白。テクスチャの番号は SRV の番号）
			WriteUint(out.constants, property->offset, (value.texture != 0) ? value.texture : TextureManager::GetWhiteTextureHandle());
			break;
		}
	}
	return true;
}


//=============================================================================
// 値を Shader の #PROPERTIES に合わせ直す
//=============================================================================
void MaterialManager::MatchValues(Material& material) {
	std::vector<MaterialValue> matched;
	if (const ProgramEntry* program = FindProgram(material.shader)) {
		for (const ShaderPropertyDef& def : program->propertyDefs) {
			// 名前と種類が同じ値があれば、そのまま使う
			const auto same = std::ranges::find_if(material.values, [&def](const MaterialValue& value) { return value.name == def.name && value.type == def.type; });
			if (same != material.values.end()) {
				matched.push_back(*same);
				continue;
			}
			// 無ければ既定の値（終わりの値も同じ値から始める）
			MaterialValue value;
			value.name = def.name;
			value.type = def.type;
			value.value = def.defaultValue;
			value.endValue = def.defaultValue;
			matched.push_back(std::move(value));
		}
	}
	material.values = std::move(matched);
}


//=============================================================================
// 見せ方（Inspector とファイル）
//=============================================================================
void MaterialManager::Describe(ComponentUI& ui, Material& material) {
	// --- Shader（変えたら、値を新しい Shader に合わせ直す。ファイルから読むときも、ここで値の並びができる）---
	const uint32_t before = material.shader;
	ui.AssetField("Shader###shader", material.shader, AssetType::Shader, Tip("使う Shader（#PROGRAM のある .shader）。下の欄は Shader の #PROPERTIES から作る。変えると、名前と種類が同じ値だけ残る"));
	if (material.shader != before) {
		MatchValues(material);
	}
	const ProgramEntry* program = FindProgram(material.shader);
	if (program == nullptr) {
		ui.Label("Shader を選ぶと、値の欄が出る");
		return;
	}

	// --- 描くときの状態 ---
	ui.Separator("Render States");
	DescribeState(ui, "Blend", "blend", material.overrideBlend, material.blend, program->states.blend);
	DescribeState(ui, "Rasterizer", "rasterizer", material.overrideRasterizer, material.rasterizer, program->states.rasterizer);
	DescribeState(ui, "Depth", "depth", material.overrideDepth, material.depth, program->states.depth);

	// --- 値（#PROPERTIES の順）---
	const ShaderPropertyLayout& layout = ShaderPackageLoader::GetPropertyLayout(material.shader - 1);
	if (layout.properties.empty()) {
		return;
	}
	ui.Separator("Properties");
	for (MaterialValue& value : material.values) {
		if (const ShaderProperty* property = layout.Find(value.name)) {
			DescribeValue(ui, value, *property);
		}
	}
}
