#include "PhysicMaterialManager.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <externals/nlohmann/json.hpp>

#include "HTTEngine/Core/AssetMoveRegistry.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/Editor/Inspector/AssetInspector.h"
#include "HTTEngine/Scene/SceneSerializer.h"
#include "HTTEngine/String/ConvertString.h"

namespace {
constexpr std::string_view kTypeName = "Physic Material"; // 警告・Inspector の見出しに出す名前

/// <summary>
/// 読み込んだファイル1つ
/// </summary>
struct PhysicMaterialAsset {
	std::string path;        // ファイル（ToPathKey の形）
	PhysicMaterial material; // 中身
};

/// <summary>
/// PhysicMaterialManager が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct PhysicMaterialState {
	std::vector<PhysicMaterialAsset> assets; // 番号 - 1 番目のファイル
};
std::unique_ptr<PhysicMaterialState> state;

const PhysicMaterial kDefaultMaterial{}; // 選んでいないとき・新しく作るファイルの中身（摩擦 kDefaultFriction・跳ね返り 0）
const std::string kEmptyPath;            // 知らない番号のパス

// 番号のファイル（知らない番号なら nullptr）
PhysicMaterialAsset* Find(uint32_t handle) {
	if (state == nullptr || handle == 0 || handle > state->assets.size()) {
		return nullptr;
	}
	return &state->assets[handle - 1];
}

// 混ぜ方に合わせて、2つの値を1つにする
float CombineValue(float a, float b, PhysicMaterialCombine combine) {
	switch (combine) {
	case PhysicMaterialCombine::Average:
		return (a + b) * 0.5f;
	case PhysicMaterialCombine::Minimum:
		return (std::min)(a, b);
	case PhysicMaterialCombine::Multiply:
		return a * b;
	case PhysicMaterialCombine::Maximum:
		return (std::max)(a, b);
	}
	return (a + b) * 0.5f;
}

// 中身を JSON にする・JSON から読む（見せ方 Describe 1つで、Inspector と同じ並び）
SceneJson ToJson(PhysicMaterial material) {
	SceneJson json;
	std::set<std::string> warnings;
	ComponentSerializer::WriteFields(kTypeName, [&material](ComponentUI& ui) { PhysicMaterialManager::Describe(ui, material); }, json, warnings);
	return json;
}

// ファイルに書く（前のファイルは .bak として1つ残る。Project ウィンドウには出ない）
bool WriteFile(const std::string& path, const PhysicMaterial& material) {
	if (!SceneSerializer::WriteFile(path, ToJson(material))) {
		LogManager::Error(std::format("Physic Material を保存できませんでした: {}", path));
		return false;
	}
	return true;
}

//=============================================================================
// Inspector（Project ウィンドウでファイルを選んだとき）
//=============================================================================
/// <summary>
/// .physmat のファイルを Inspector で直す係（AssetInspector に登録する）
/// </summary>
class PhysicMaterialEditor final : public AssetEditor {
public:
	std::string_view GetExtension() const override { return PhysicMaterialManager::kFileExtension; }
	std::string_view GetTypeName() const override { return kTypeName; }
	bool CanLoad(const std::filesystem::path& filePath) override { return PhysicMaterialManager::Load(filePath) != 0; }

	bool Draw(const std::filesystem::path& filePath, ComponentUI& ui) override {
		const uint32_t handle = PhysicMaterialManager::Load(filePath);
		if (handle == 0) {
			return false;
		}
		// 写しを直して、変わっていたら書き戻す（実行中なら、次のフレームの物理からすぐ効く）
		PhysicMaterial material = PhysicMaterialManager::Get(handle);
		PhysicMaterialManager::Describe(ui, material);
		ui.Space();
		ui.Label("このファイルを選んだ Collider みんなに効く（つまむ・打つのが終わると保存。Undo は無い）");
		if (material == PhysicMaterialManager::Get(handle)) {
			return false;
		}
		PhysicMaterialManager::Set(handle, material);
		return true;
	}

	void Save(const std::filesystem::path& filePath) override { PhysicMaterialManager::Save(PhysicMaterialManager::Load(filePath)); }
};

// ===== 名前を変えた・動かしたファイル：覚えているパスを付け替える（番号はそのまま。保存は新しいパスへ）=====
void OnAssetMoved(const MovedPath& moved) {
	if (state == nullptr) {
		return;
	}
	for (PhysicMaterialAsset& asset : state->assets) {
		moved.Apply(asset.path);
	}
}
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
void PhysicMaterialManager::Initialize() {
	HTT_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<PhysicMaterialState>();
	// Project ウィンドウで .physmat を選んだら、Inspector で直せるようにする
	AssetInspector::Register(std::make_unique<PhysicMaterialEditor>());
	AssetMoveRegistry::AddListener(&OnAssetMoved);
	AssetMoveRegistry::AddReferenceFileType(kFileExtension); // 今は中にパスを書かないが、エンジンが書く JSON のファイルなので入れておく（項目が増えても忘れないように）
}

void PhysicMaterialManager::Release() { state.reset(); }

//=============================================================================
// 読み込み
//=============================================================================
uint32_t PhysicMaterialManager::Load(const std::filesystem::path& filePath) {
	HTT_ASSERT_MSG(state != nullptr, "PhysicMaterialManager::Initialize()を先に呼んでください");
	// --- 読み込み済みなら、同じ番号を返す ---
	const std::string key = ToPathKey(filePath);
	const auto cached = std::ranges::find(state->assets, key, &PhysicMaterialAsset::path);
	if (cached != state->assets.end()) {
		return static_cast<uint32_t>(cached - state->assets.begin()) + 1;
	}
	// --- ファイルから読む（ファイルに無い項目は、既定の値のまま）---
	std::ifstream stream(filePath);
	if (!stream) {
		LogManager::Warning(std::format("Physic Material のファイルを開けませんでした: {}", key));
		return 0;
	}
	const SceneJson json = SceneJson::parse(stream, nullptr, false); // false: 壊れていても例外を投げず、discarded を返す
	if (json.is_discarded()) {
		LogManager::Error(std::format("Physic Material のファイルが壊れています: {}", key));
		return 0;
	}
	PhysicMaterial material;
	std::set<std::string> warnings;
	ComponentSerializer::ReadFields(kTypeName, [&material](ComponentUI& ui) { Describe(ui, material); }, json, warnings);
	for (const std::string& warning : warnings) {
		LogManager::Warning(std::format("{}: {}", key, warning));
	}
	state->assets.push_back({key, material});
	const uint32_t handle = static_cast<uint32_t>(state->assets.size());
	LogManager::Log(std::format("Loaded physic material: {} -> {}", key, handle));
	return handle;
}

const std::string& PhysicMaterialManager::GetPath(uint32_t handle) {
	const PhysicMaterialAsset* asset = Find(handle);
	return (asset != nullptr) ? asset->path : kEmptyPath;
}

const PhysicMaterial& PhysicMaterialManager::Get(uint32_t handle) {
	const PhysicMaterialAsset* asset = Find(handle);
	return (asset != nullptr) ? asset->material : kDefaultMaterial;
}


//=============================================================================
// 書き換え・保存
//=============================================================================
void PhysicMaterialManager::Set(uint32_t handle, const PhysicMaterial& material) {
	PhysicMaterialAsset* asset = Find(handle);
	if (asset == nullptr) {
		return;
	}
	asset->material = material;
}

bool PhysicMaterialManager::Save(uint32_t handle) {
	const PhysicMaterialAsset* asset = Find(handle);
	return (asset != nullptr) && WriteFile(asset->path, asset->material);
}

bool PhysicMaterialManager::CreateNewFile(const std::filesystem::path& filePath) {
	std::error_code error;
	if (std::filesystem::exists(filePath, error)) {
		return false; // 上書きしない
	}
	return WriteFile(ToUtf8(filePath), kDefaultMaterial);
}

//=============================================================================
// 混ぜる
//=============================================================================
PhysicMaterial PhysicMaterialManager::Combine(const PhysicMaterial& a, const PhysicMaterial& b) {
	// 混ぜ方が違うときは、番号の大きい方（Average < Minimum < Multiply < Maximum）を使う（Unity と同じ）
	const PhysicMaterialCombine frictionCombine = (std::max)(a.frictionCombine, b.frictionCombine);
	const PhysicMaterialCombine bounceCombine = (std::max)(a.bounceCombine, b.bounceCombine);
	PhysicMaterial result;
	result.friction = CombineValue(a.friction, b.friction, frictionCombine);
	result.bounciness = CombineValue(a.bounciness, b.bounciness, bounceCombine);
	result.frictionCombine = frictionCombine;
	result.bounceCombine = bounceCombine;
	return result;
}

//=============================================================================
// 見せ方（Inspector とファイル）
//=============================================================================
void PhysicMaterialManager::Describe(ComponentUI& ui, PhysicMaterial& value) {
	ui.Field("Friction###friction", value.friction, AtLeast(0.0f).Tip("摩擦（滑っているときに止める強さ。0：止まらない。0.5 くらいで普通、1 でよく止まる。新しいファイル・選んでいない Collider は 0.6）"));
	ui.Field("Bounciness###bounciness", value.bounciness, Range(0.0f, 1.0f).Tip("跳ね返り（0：ぶつかった向きの速さが0になる 1：同じ速さで跳ね返る）。ゆっくり（1m/s より遅く）ぶつかったときは跳ねない"));
	ui.Field("Friction Combine###frictionCombine", value.frictionCombine, Tip("相手と摩擦をどう混ぜるか（Average：平均 / Minimum：小さい方 / Multiply：掛け算 / Maximum：大きい方）。2つで違うときは下の方を使う"));
	ui.Field("Bounce Combine###bounceCombine", value.bounceCombine, Tip("相手と跳ね返りをどう混ぜるか（よく跳ねるボールは Maximum にすると、何に当たっても跳ねる）"));
}