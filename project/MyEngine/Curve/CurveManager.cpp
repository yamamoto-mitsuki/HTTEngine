#include "CurveManager.h"

#include <format>
#include <memory>
#include <vector>
#include <fstream>
#include <algorithm>

#include <externals/nlohmann/json.hpp>

#include "MyEngine/Core/AssetMoveRegistry.h"
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/Scene/SceneSerializer.h"
#include "MyEngine/String/ConvertString.h"

namespace {
constexpr std::string_view kTypeName = "Curve"; // 警告に出す名前

/// <summary>
/// 読み込んだファイル1つ
/// </summary>
struct CurveAsset {
	std::string path; // ファイル（ToPathKey の形）
	Curve curve;      // 中身
};

/// <summary>
/// CurveManager が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct CurveStorage {
	std::vector<CurveAsset> assets; // 番号 - 1 番目のファイル
};
std::unique_ptr<CurveStorage> state;

const std::string kEmptyPath; // 知らない番号のパス

// 番号のファイル（知らない番号なら nullptr）
CurveAsset* Find(uint32_t handle) {
	if (state == nullptr || handle == 0 || handle > state->assets.size()) {
		return nullptr;
	}
	return &state->assets[handle - 1];
}

// ファイルに書く（前のファイルは .bak として1つ残る）
bool WriteFile(const std::string& path, const Curve& curve) {
	if (!SceneSerializer::WriteFile(path, CurveManager::ToJson(curve))) {
		LogManager::Error(std::format("カーブを保存できませんでした: {}", path));
		return false;
	}
	return true;
}

// ===== 名前を変えた・動かしたファイル：覚えているパスを付け替える（番号はそのまま。保存は新しいパスへ）=====
void OnAssetMoved(const MovedPath& moved) {
	if (state == nullptr) {
		return;
	}
	for (CurveAsset& asset : state->assets) {
		moved.Apply(asset.path);
	}
}
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
void CurveManager::Initialize() {
	MY_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<CurveStorage>();
	AssetMoveRegistry::AddListener(&OnAssetMoved);
	AssetMoveRegistry::AddReferenceFileType(kFileExtension); // 今は中にパスを書かないが、エンジンが書く JSON のファイルなので入れておく（項目が増えても忘れないように）
}

void CurveManager::Release() { state.reset(); }

//=============================================================================
// 読み込み
//=============================================================================
uint32_t CurveManager::Load(const std::filesystem::path& filePath) {
	MY_ASSERT_MSG(state != nullptr, "CurveManager::Initialize()を先に呼んでください");
	// --- 読み込み済みなら、同じ番号を返す ---
	const std::string key = ToPathKey(filePath);
	const auto cached = std::ranges::find(state->assets, key, &CurveAsset::path);
	if (cached != state->assets.end()) {
		return static_cast<uint32_t>(cached - state->assets.begin()) + 1;
	}
	// --- ファイルから読む（ファイルに無い項目は、既定の値のまま）---
	std::ifstream stream(filePath);
	if (!stream) {
		LogManager::Warning(std::format("カーブのファイルを開けませんでした: {}", key));
		return 0;
	}
	const SceneJson json = SceneJson::parse(stream, nullptr, false); // false: 壊れていても例外を投げず、discarded を返す
	if (json.is_discarded() || !json.is_object()) {
		LogManager::Error(std::format("カーブのファイルが壊れています: {}", key));
		return 0;
	}
	std::set<std::string> warnings;
	Curve curve = FromJson(json, warnings);
	for (const std::string& warning : warnings) {
		LogManager::Warning(std::format("{}: {}", key, warning));
	}
	state->assets.push_back({key, std::move(curve)});
	const uint32_t handle = static_cast<uint32_t>(state->assets.size());
	LogManager::Log(std::format("Loaded curve: {} -> {}", key, handle));
	return handle;
}

const std::string& CurveManager::GetPath(uint32_t handle) {
	const CurveAsset* asset = Find(handle);
	return (asset != nullptr) ? asset->path : kEmptyPath;
}

const Curve* CurveManager::Get(uint32_t handle) {
	const CurveAsset* asset = Find(handle);
	return (asset != nullptr) ? &asset->curve : nullptr;
}

//=============================================================================
// 書き換え・保存
//=============================================================================
void CurveManager::Set(uint32_t handle, const Curve& curve) {
	if (CurveAsset* asset = Find(handle)) {
		asset->curve = curve;
	}
}

bool CurveManager::Save(uint32_t handle) {
	const CurveAsset* asset = Find(handle);
	return (asset != nullptr) && WriteFile(asset->path, asset->curve);
}

bool CurveManager::CreateNewFile(const std::filesystem::path& filePath) {
	std::error_code error;
	if (std::filesystem::exists(filePath, error)) {
		return false; // 上書きしない
	}
	return WriteFile(ToUtf8(filePath), Curve::FromEase(Ease::Type::Linear));
}

//=============================================================================
// JSON
//=============================================================================
SceneJson CurveManager::ToJson(const Curve& curve) {
	std::set<std::string> warnings;
	SceneJson keys = SceneJson::array();
	for (CurveKey key : curve.GetKeys()) { // 見せ方の関数は書き換えられる参照を受け取るので、写しを渡す
		SceneJson item;
		ComponentSerializer::WriteFields(kTypeName, [&key](ComponentUI& ui) { DescribeKey(ui, key); }, item, warnings);
		keys.push_back(std::move(item));
	}
	for (const std::string& warning : warnings) {
		LogManager::Warning(warning);
	}
	SceneJson root = SceneJson::object();
	root["keys"] = std::move(keys);
	return root;
}

Curve CurveManager::FromJson(const SceneJson& json, std::set<std::string>& warnings) {
	const auto keys = json.find("keys");
	if (keys == json.end() || !keys->is_array()) {
		warnings.insert("\"keys\" の配列がありません（空のカーブにします）");
		return Curve{};
	}
	std::vector<CurveKey> result;
	for (const SceneJson& item : *keys) {
		if (!item.is_object()) {
			warnings.insert("\"keys\" の中に、キーではない物があります（読み飛ばします）");
			continue;
		}
		CurveKey key;
		ComponentSerializer::ReadFields(kTypeName, [&key](ComponentUI& ui) { DescribeKey(ui, key); }, item, warnings);
		result.push_back(key);
	}
	return Curve(std::move(result));
}

//=============================================================================
// 見せ方（Inspector とファイル）
//=============================================================================
void CurveManager::DescribeKey(ComponentUI& ui, CurveKey& value, bool isInUsed, bool isOutUsed) {
	ui.Field("Time###time", value.time, Drag(0.01f).Tip("キーの時間"));
	ui.Field("Value###value", value.value, Drag(0.01f).Tip("キーの値"));
	ui.Field("Interpolation###interp", value.interp, Tip("このキーから次のキーまでのつなぎ方（Constant：次のキーまで今の値のまま / Linear：直線 / Bezier：ハンドルで曲げる）"));
	ui.Field("Handle Mode###handleMode", value.handleMode, Tip("Aligned：入りと出の傾きをそろえる（なめらかにつながる） / Free：別々に動かす（折れ目を作れる）"));
	ui.BeginDisabled(!isInUsed);
	ui.Field("In Handle###inHandle", value.inHandle, Drag(0.01f).Tip("入りのハンドル。キーからの差（x：時間、y：値）。時間は 0 以下で、前のキーまでの長さを超えない"));
	ui.EndDisabled();
	ui.BeginDisabled(!isOutUsed);
	ui.Field("Out Handle###outHandle", value.outHandle, Drag(0.01f).Tip("出のハンドル。キーからの差（x：時間、y：値）。時間は 0 以上で、次のキーまでの長さを超えない"));
	ui.EndDisabled();
}