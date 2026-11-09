#include "TimelineManager.h"

#include <algorithm>
#include <format>
#include <fstream>

#include <externals/nlohmann/json.hpp>

#include "HTTEngine/Core/AssetMoveRegistry.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/Scene/SceneSerializer.h"
#include "HTTEngine/String/ConvertString.h"

namespace {
constexpr std::string_view kTypeName = "Timeline"; // 警告に出す名前

/// <summary>
/// 読み込んだファイル1つ
/// </summary>
struct TimelineAsset {
	std::string path;      // ファイル（ToPathKey の形）
	Timeline timeline;     // 中身
	uint32_t revision = 0; // 中身の版（MarkChanged のたびに増える）
};

/// <summary>
/// TimelineManager が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct TimelineStorage {
	// 番号 - 1 番目のファイル。unique_ptr で1つずつ持つのは、読んでいる途中に別のファイルを読んでも（Preset のクリップ）、中身の場所が動かないように
	std::vector<std::unique_ptr<TimelineAsset>> assets;
	std::vector<std::string> loadingPaths; // 読んでいる途中のファイル（Preset のクリップが自分を指していても、読み続けないように）
};
std::unique_ptr<TimelineStorage> state;

const std::string kEmptyPath; // 知らない番号のパス

// 番号のファイル（知らない番号なら nullptr）
TimelineAsset* Find(uint32_t handle) {
	if (state == nullptr || handle == 0 || handle > state->assets.size()) {
		return nullptr;
	}
	return state->assets[handle - 1].get();
}

// ファイルに書く（前のファイルは .bak として1つ残る）
bool WriteFile(const std::string& path, const Timeline& timeline) {
	if (!SceneSerializer::WriteFile(path, TimelineManager::ToJson(timeline))) {
		LogManager::Error(std::format("Timeline を保存できませんでした: {}", path));
		return false;
	}
	return true;
}

// ===== トラック1つを JSON にする =====
SceneJson TrackToJson(TimelineTrack& track, std::set<std::string>& warnings) {
	const std::string& typeName = track.typeName;
	SceneJson settings;
	ComponentSerializer::WriteFields(
	    typeName,
	    [&track](ComponentUI& ui) {
		    track.DescribeCommon(ui);
		    track.Fields(ui);
	    },
	    settings, warnings);
	SceneJson clips;
	track.WriteClips(clips, warnings);

	SceneJson json = SceneJson::object();
	json["type"] = typeName;
	json["settings"] = std::move(settings);
	json["clips"] = std::move(clips);
	return json;
}

// ===== トラック1つを読む（知らない種類なら、JSON の文字のまま覚える）=====
void ReadTrack(const SceneJson& json, Timeline& timeline, std::set<std::string>& warnings) {
	const auto type = json.find("type");
	if (type == json.end() || !type->is_string()) {
		warnings.insert("\"type\" の無いトラックを飛ばしました");
		return;
	}
	const std::string& typeName = type->get_ref<const std::string&>();
	std::unique_ptr<TimelineTrack> track = TimelineTrackRegistry::Create(typeName);
	if (track == nullptr) {
		warnings.insert(std::format("知らない種類のトラック \"{}\" は動かしません（TIMELINE_TRACK(...) を確かめてください。ファイルからは消しません）", typeName));
		timeline.unknownTracks.push_back(json.dump());
		return;
	}
	if (const auto settings = json.find("settings"); settings != json.end()) {
		TimelineTrack* pointer = track.get();
		ComponentSerializer::ReadFields(
		    typeName,
		    [pointer](ComponentUI& ui) {
			    pointer->DescribeCommon(ui);
			    pointer->Fields(ui);
		    },
		    *settings, warnings);
	}
	if (const auto clips = json.find("clips"); clips != json.end() && clips->is_array()) {
		track->ReadClips(*clips, warnings);
	}
	timeline.tracks.push_back(std::move(track));
}

// ===== 名前を変えた・動かしたファイル：覚えているパスを付け替える（番号はそのまま。保存は新しいパスへ）=====
// 知らない種類のトラックは JSON の文字のまま持っているので、その中のパスも付け替える（保存でそのまま書き戻すため）
void OnAssetMoved(const MovedPath& moved) {
	if (state == nullptr) {
		return;
	}
	for (const std::unique_ptr<TimelineAsset>& asset : state->assets) {
		moved.Apply(asset->path);
		for (std::string& text : asset->timeline.unknownTracks) {
			SceneJson track = SceneJson::parse(text, nullptr, false);
			if (!track.is_discarded() && SceneSerializer::RemapPaths(track, moved)) {
				text = track.dump();
			}
		}
	}
}
} // namespace

//=============================================================================
// Timeline
//=============================================================================
float Timeline::GetDuration() const {
	float duration = length;
	for (const std::unique_ptr<TimelineTrack>& track : tracks) {
		duration = (std::max)(duration, track->GetEndTime());
	}
	return duration;
}

void Timeline::Evaluate(const TimelineFrame& frame, TimelineStates& states, bool isApplying, bool isStopped) const {
	for (size_t i = 0; i < tracks.size() && i < states.tracks.size(); ++i) {
		const TimelineTrack& track = *tracks[i];
		const bool isEvaluated = frame.isPreview ? track.CanPreview() : (track.EvaluatesEveryFrame() ? !isStopped : isApplying);
		if (isEvaluated && !track.isMuted) {
			track.Evaluate(frame, *states.tracks[i]);
		}
	}
}

//=============================================================================
// トラックの Entity ごとの値
//=============================================================================
bool TimelineStates::Prepare(uint32_t handle) {
	const Timeline* timeline = TimelineManager::Get(handle);
	const uint32_t current = TimelineManager::GetRevision(handle);
	if (timeline == nullptr) {
		*this = {};
		return false;
	}
	if (handle == this->timeline && current == revision && tracks.size() == timeline->tracks.size()) {
		return false;
	}
	this->timeline = handle;
	revision = current;
	tracks.clear();
	for (const std::unique_ptr<TimelineTrack>& track : timeline->tracks) {
		tracks.push_back(track->CreateState());
	}
	return true;
}

//=============================================================================
// 初期化 / 解放
//=============================================================================
void TimelineManager::Initialize() {
	HTT_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<TimelineStorage>();
	AssetMoveRegistry::AddListener(&OnAssetMoved);
	AssetMoveRegistry::AddReferenceFileType(kFileExtension); // クリップの Curve・Preset のパスを書く
}

void TimelineManager::Release() { state.reset(); }

//=============================================================================
// 読み込み
//=============================================================================
uint32_t TimelineManager::Load(const std::filesystem::path& filePath) {
	HTT_ASSERT_MSG(state != nullptr, "TimelineManager::Initialize()を先に呼んでください");
	// --- 読み込み済みなら、同じ番号を返す ---
	const std::string key = ToPathKey(filePath);
	const auto cached = std::ranges::find(state->assets, key, [](const std::unique_ptr<TimelineAsset>& asset) -> const std::string& { return asset->path; });
	if (cached != state->assets.end()) {
		return static_cast<uint32_t>(cached - state->assets.begin()) + 1;
	}
	// --- 読んでいる途中のファイル（Preset のクリップが自分を指している）は読まない（読むと、また同じファイルを読み続ける）---
	if (std::ranges::find(state->loadingPaths, key) != state->loadingPaths.end()) {
		LogManager::Warning(std::format("Timeline が自分をプリセットにしています（そのクリップは空にします）: {}", key));
		return 0;
	}
	// --- ファイルから読む（ファイルに無い項目は、既定の値のまま）---
	std::ifstream stream(filePath);
	if (!stream) {
		LogManager::Warning(std::format("Timeline のファイルを開けませんでした: {}", key));
		return 0;
	}
	const SceneJson json = SceneJson::parse(stream, nullptr, false); // false: 壊れていても例外を投げず、discarded を返す
	if (json.is_discarded() || !json.is_object()) {
		LogManager::Error(std::format("Timeline のファイルが壊れています: {}", key));
		return 0;
	}
	std::set<std::string> warnings;
	state->loadingPaths.push_back(key);
	Timeline timeline = FromJson(json, warnings); // Preset のクリップの中のファイルも、ここで読む
	std::erase(state->loadingPaths, key);
	for (const std::string& warning : warnings) {
		LogManager::Warning(std::format("{}: {}", key, warning));
	}
	state->assets.push_back(std::make_unique<TimelineAsset>(key, std::move(timeline)));
	const uint32_t handle = static_cast<uint32_t>(state->assets.size());
	LogManager::Log(std::format("Loaded timeline: {} -> {}", key, handle));
	return handle;
}

const std::string& TimelineManager::GetPath(uint32_t handle) {
	const TimelineAsset* asset = Find(handle);
	return (asset != nullptr) ? asset->path : kEmptyPath;
}

const Timeline* TimelineManager::Get(uint32_t handle) {
	const TimelineAsset* asset = Find(handle);
	return (asset != nullptr) ? &asset->timeline : nullptr;
}

//=============================================================================
// 書き換え・保存
//=============================================================================
Timeline* TimelineManager::GetForEdit(uint32_t handle) {
	TimelineAsset* asset = Find(handle);
	return (asset != nullptr) ? &asset->timeline : nullptr;
}

void TimelineManager::MarkChanged(uint32_t handle) {
	if (TimelineAsset* asset = Find(handle)) {
		++asset->revision;
	}
}

std::string TimelineManager::ToText(uint32_t handle) {
	const Timeline* timeline = Get(handle);
	return (timeline != nullptr) ? ToJson(*timeline).dump() : std::string();
}

bool TimelineManager::ApplyText(uint32_t handle, const std::string& text) {
	TimelineAsset* asset = Find(handle);
	const SceneJson json = SceneJson::parse(text, nullptr, false); // false: 壊れていても例外を投げず、discarded を返す
	if (asset == nullptr || json.is_discarded() || !json.is_object()) {
		return false;
	}
	std::set<std::string> warnings;
	asset->timeline = FromJson(json, warnings);
	for (const std::string& warning : warnings) {
		LogManager::Warning(std::format("{}: {}", asset->path, warning));
	}
	++asset->revision;
	return true;
}

uint32_t TimelineManager::GetRevision(uint32_t handle) {
	const TimelineAsset* asset = Find(handle);
	return (asset != nullptr) ? asset->revision : 0;
}

bool TimelineManager::Save(uint32_t handle) {
	const TimelineAsset* asset = Find(handle);
	return (asset != nullptr) && WriteFile(asset->path, asset->timeline);
}

bool TimelineManager::CreateNewFile(const std::filesystem::path& filePath) {
	std::error_code error;
	if (std::filesystem::exists(filePath, error)) {
		return false; // 上書きしない
	}
	return WriteFile(ToUtf8(filePath), Timeline{});
}

//=============================================================================
// JSON
//=============================================================================
SceneJson TimelineManager::ToJson(const Timeline& timeline) {
	std::set<std::string> warnings;
	float length = timeline.length;
	SceneJson settings;
	ComponentSerializer::WriteFields(kTypeName, [&length](ComponentUI& ui) { DescribeLength(ui, length); }, settings, warnings);

	SceneJson tracks = SceneJson::array();
	for (const std::unique_ptr<TimelineTrack>& track : timeline.tracks) {
		tracks.push_back(TrackToJson(*track, warnings));
	}
	for (const std::string& text : timeline.unknownTracks) {
		tracks.push_back(SceneJson::parse(text, nullptr, false));
	}
	for (const std::string& warning : warnings) {
		LogManager::Warning(warning);
	}
	SceneJson root = SceneJson::object();
	root["settings"] = std::move(settings);
	root["tracks"] = std::move(tracks);
	return root;
}

Timeline TimelineManager::FromJson(const SceneJson& json, std::set<std::string>& warnings) {
	Timeline timeline;
	if (const auto settings = json.find("settings"); settings != json.end()) {
		ComponentSerializer::ReadFields(kTypeName, [&timeline](ComponentUI& ui) { DescribeLength(ui, timeline.length); }, *settings, warnings);
		timeline.length = (timeline.length >= 0.0f) ? timeline.length : 0.0f; // 比べ方を逆にしないのは、NaN もここで 0 にするため
	}
	if (const auto tracks = json.find("tracks"); tracks != json.end() && tracks->is_array()) {
		for (const SceneJson& item : *tracks) {
			if (item.is_object()) {
				ReadTrack(item, timeline, warnings);
			} else {
				warnings.insert("\"tracks\" の中に、トラックではない物があります（読み飛ばします）");
			}
		}
	}
	return timeline;
}

//=============================================================================
// 見せ方（Inspector とファイル）
//=============================================================================
void TimelineManager::DescribeLength(ComponentUI& ui, float& length) { ui.Field("Length###length", length, AtLeast(0.0f).Tip("長さ（秒）。クリップがこれより長ければ、クリップの終わりまで再生する")); }