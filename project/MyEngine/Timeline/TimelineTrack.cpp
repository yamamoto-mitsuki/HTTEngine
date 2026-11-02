#include "TimelineTrack.h"

#include <algorithm>
#include <format>

#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/ECS/EntityManager.h"

namespace {
/// <summary>
/// トラックの種類1つ
/// </summary>
struct TrackType {
	std::string name;
	TimelineTrackRegistry::CreateFunction create = nullptr;
	std::string description;
};

// 関数の中の static なので、ゲームの TIMELINE_TRACK（main より前に作られる）との初期化の順番に左右されない
std::vector<TrackType>& Types() {
	static std::vector<TrackType> types;
	return types;
}

const std::string kEmptyDescription;                 // 知らない名前の説明
constexpr std::string_view kOmittedPrefix = "Timeline"; // ウィンドウに出すときに省く名前の頭（エンジンのトラックは Timeline〇〇Track）
} // namespace

//=============================================================================
// トラック
//=============================================================================
// ===== 見せ方 =====
void TimelineTrack::DescribeCommon(ComponentUI& ui) {
	ui.Field("Name###name", name, Tip("トラックの名前（警告に出す）"));
	ui.Field("Target###target", target, Tip("動かす相手。再生する Entity から、子の名前を / でつないで書く（Panel/Title）。空なら再生する Entity 自身。先頭が / ならシーンの一番上から（/Cameras/Close）"));
	ui.Field("Muted###isMuted", isMuted, Tip("このトラックを動かさない"));
}

// ===== 対象を探す =====
Handle<Entity> TimelineTrack::FindTarget(const TimelineFrame& frame, TimelineTrackState& state) const {
	// 実行中に差し替えた対象があれば、それを使う（差し替えた先の Timeline のトラックだけ。Preset の中のパスは別の Entity から数えるので使わない）
	if (frame.overrides != nullptr && frame.overrides->owner == frame.owner) {
		if (const Handle<Entity>* replaced = frame.overrides->FindTarget(target)) {
			return EntityManager::IsAlive(*replaced) ? *replaced : Handle<Entity>{};
		}
	}
	// 覚えている相手が生きていれば、それを使う（探すのは Entity を全部調べるので、毎フレームはしない）
	if (EntityManager::IsAlive(state.target)) {
		return state.target;
	}
	// 見つからなかった後は、Entity の数が変わるまで探し直さない。覚えていた相手が消えたときは、すぐ探す
	const size_t entityCount = EntityManager::GetCount();
	if (state.isSearched && !state.target.IsValid() && state.searchedEntities == entityCount) {
		return {};
	}
	state.isSearched = true;
	state.searchedEntities = entityCount;
	state.target = EntityManager::FindByPath(frame.owner, target);
	if (!state.target.IsValid()) {
		WarnOnce(state, std::format("対象が見つかりません: \"{}\"", target));
	}
	return state.target;
}

// ===== 警告を1回だけ出す =====
void TimelineTrack::WarnOnce(TimelineTrackState& state, std::string_view message) const {
	if (!state.isWarned) {
		state.isWarned = true;
		LogManager::Warning(std::format("Timeline のトラック \"{}\"：{}", name, message));
	}
}

//=============================================================================
// 実行中に渡した値・差し替えた対象
//=============================================================================
const Vector4* TimelineOverrides::FindParameter(std::string_view name) const {
	const auto found = std::ranges::find(parameters, name, &TimelineParameter::name);
	return (found != parameters.end()) ? &found->value : nullptr;
}

const Handle<Entity>* TimelineOverrides::FindTarget(std::string_view path) const {
	const auto found = std::ranges::find(targets, path, &TimelineTargetOverride::path);
	return (found != targets.end()) ? &found->target : nullptr;
}

//=============================================================================
// Component の項目の指し方
//=============================================================================
void TimelinePropertyBinding::Fields(ComponentUI& ui) {
	ui.Field("Component###component", component, Tip("動かす Component の名前（Inspector の見出しと同じ。Image・Vector Shape など）"));
	ui.Field("Property###property", property, Tip("動かす項目の名前（Transform は translation・rotation・scale。ほかはシーンファイルに書かれている名前）"));
}

//=============================================================================
// クリップ
//=============================================================================
void TimelineClip::Fields(ComponentUI& ui) {
	ui.Field("Start###start", start, AtLeast(0.0f).Tip("始まる時間（秒）"));
	ui.Field("Duration###duration", duration, AtLeast(0.0f).Tip("長さ（秒）"));
}

//=============================================================================
// 種類の表
//=============================================================================
void TimelineTrackRegistry::Register(const char* name, CreateFunction create, const char* description) {
	std::vector<TrackType>& types = Types();
	if (std::ranges::find(types, std::string_view(name), &TrackType::name) != types.end()) {
		return; // 同じ名前は1回だけ（エンジンを作り直しても、登録が重ならないように）
	}
	types.push_back({name, create, description});
}

std::unique_ptr<TimelineTrack> TimelineTrackRegistry::Create(std::string_view name) {
	const std::vector<TrackType>& types = Types();
	const auto found = std::ranges::find(types, name, &TrackType::name);
	if (found == types.end()) {
		return nullptr;
	}
	std::unique_ptr<TimelineTrack> track = found->create();
	track->typeName = found->name;
	return track;
}

std::vector<std::string> TimelineTrackRegistry::GetNames() {
	std::vector<std::string> names;
	for (const TrackType& type : Types()) {
		names.push_back(type.name);
	}
	std::ranges::sort(names, {}, [](const std::string& name) { return GetDisplayName(name); }); // ウィンドウに出す名前の順に並べる
	return names;
}

const std::string& TimelineTrackRegistry::GetDescription(std::string_view name) {
	const std::vector<TrackType>& types = Types();
	const auto found = std::ranges::find(types, name, &TrackType::name);
	return (found != types.end()) ? found->description : kEmptyDescription;
}

std::string_view TimelineTrackRegistry::GetDisplayName(std::string_view name) {
	// "Timeline" そのものは、省くと空になるのでそのまま
	return (name.starts_with(kOmittedPrefix) && name.size() > kOmittedPrefix.size()) ? name.substr(kOmittedPrefix.size()) : name;
}