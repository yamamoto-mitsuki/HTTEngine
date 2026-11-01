#include "TimelineSelfTest.h"

#include <set>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <memory>
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
#include <filesystem>

#include <externals/nlohmann/json.hpp>

#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Components/Core/TransformComponent.h"
#include "MyEngine/Components/UI/ImageComponent.h"
#include "MyEngine/Systems/Animation/TimelineSystem.h"
#include "MyEngine/Timeline/TimelineEdit.h"
#include "MyEngine/Timeline/TimelineEventTrack.h"
#include "MyEngine/Timeline/TimelineManager.h"
#include "MyEngine/Timeline/TimelineTimeView.h"
#include "MyEngine/Timeline/TimelineValueTrack.h"

namespace {
constexpr float kExact = 0.001f;  // 式で答えが決まる値の許容
constexpr int kMaxErrorLogs = 10; // 合わなかった項目をログに出す数（全部出すと読めない）
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

/// <summary>
/// 確かめた数と、合わなかった数を数える
/// </summary>
struct Tester {
	int passed = 0;
	int failed = 0;

	void Check(bool isOk, const std::string& name) {
		if (isOk) {
			++passed;
			return;
		}
		if (failed < kMaxErrorLogs) {
			LogManager::Error(std::format("[timeline] self test: {}", name));
		}
		++failed;
	}
	void Near(float actual, float expected, const std::string& name) { Check(std::abs(actual - expected) <= kExact, std::format("{}: {} （正しい値 {}）", name, actual, expected)); }
};

// イベントのトラックを作る（times の時間に、"e0", "e1", ... という名前で置く）
std::unique_ptr<TimelineEventTrack> MakeEventTrack(const std::vector<float>& times) {
	auto track = std::make_unique<TimelineEventTrack>();
	for (size_t i = 0; i < times.size(); ++i) {
		TimelineEventClip clip;
		clip.start = times[i];
		clip.name = std::format("e{}", i);
		track->clips.push_back(clip);
	}
	return track;
}

// 時間を deltas の分だけ進めて、呼ばれたイベントの名前を順につないで返す（シークを挟めるように、playhead は外で持つ）
std::string RunEvents(TimelinePlayhead& playhead, const TimelineEventTrack& track, Handle<Entity> owner, const std::vector<float>& deltas, float duration, bool isLoop) {
	std::vector<TimelineRange> ranges;
	std::vector<TimelineEvent> events;
	TimelineTrackState state;
	for (const float delta : deltas) {
		playhead.Advance(delta, duration, isLoop, ranges);
		track.Evaluate({owner, playhead.GetTime(), ranges, &events}, state);
	}
	std::string result;
	for (const TimelineEvent& event : events) {
		result += event.name + " ";
	}
	return result;
}

// 値を書く
ComponentValueResult SetChannel(Handle<Entity> entity, std::string_view component, std::string_view key, size_t channel, float value) {
	ComponentValue componentValue;
	componentValue.channels[channel] = value;
	componentValue.isUsed[channel] = true;
	return ComponentSerializer::SetValue(entity, component, key, componentValue);
}

//=============================================================================
// 確かめる項目
//=============================================================================
// ===== 時間の進み方 =====
void TestPlayhead(Tester& t) {
	std::vector<TimelineRange> ranges;
	TimelinePlayhead playhead;
	t.Check(playhead.GetState() == TimelinePlayState::Stopped, "最初は止まっている");
	playhead.Advance(0.5f, 1.0f, false, ranges);
	t.Check(ranges.empty() && playhead.GetTime() == 0.0f, "止まっている間は進まない");

	playhead.Play();
	playhead.Advance(0.25f, 1.0f, false, ranges);
	t.Check(ranges.size() == 1 && ranges[0].includesStart && playhead.GetTime() == 0.25f, "最初の幅は 0 ちょうどを含む");
	playhead.Advance(0.0f, 1.0f, false, ranges);
	t.Check(ranges.empty(), "0 秒なら進まない");
	playhead.Advance(kNaN, 1.0f, false, ranges);
	t.Check(ranges.empty() && playhead.GetTime() == 0.25f, "NaN なら進まない");
	playhead.Advance(0.25f, 1.0f, false, ranges);
	t.Check(ranges.size() == 1 && !ranges[0].includesStart, "2回目の幅は始まりを含まない");

	playhead.Pause();
	t.Check(playhead.GetState() == TimelinePlayState::Paused, "一時停止");
	playhead.Advance(0.25f, 1.0f, false, ranges);
	t.Check(ranges.empty() && playhead.GetTime() == 0.5f, "一時停止中は進まない");
	playhead.Play();
	t.Check(playhead.GetState() == TimelinePlayState::Playing && playhead.GetTime() == 0.5f, "一時停止の続きから");

	playhead.Advance(2.0f, 1.0f, false, ranges);
	t.Check(playhead.GetState() == TimelinePlayState::Finished && playhead.GetTime() == 1.0f, "最後まで行くと終わる");
	playhead.Play();
	t.Check(playhead.GetState() == TimelinePlayState::Playing && playhead.GetTime() == 0.0f, "終わった後の Play は最初から");

	playhead.Advance(0.5f, 1.0f, false, ranges);
	playhead.Stop();
	t.Check(playhead.GetState() == TimelinePlayState::Stopped && playhead.GetTime() == 0.0f, "Stop で 0 に戻る");

	// --- シーク ---
	playhead.Seek(0.5f, 1.0f);
	t.Check(playhead.GetState() == TimelinePlayState::Paused && playhead.GetTime() == 0.5f, "止まっている間のシークは一時停止になる");
	playhead.Seek(5.0f, 1.0f);
	t.Check(playhead.GetTime() == 1.0f, "シークは長さの中に収まる");
	playhead.Seek(-1.0f, 1.0f);
	t.Check(playhead.GetTime() == 0.0f, "シークは 0 より前へ行かない");
	playhead.Seek(kNaN, 1.0f);
	t.Check(playhead.GetTime() == 0.0f, "NaN のシークは 0");
	playhead.Play();
	playhead.Seek(0.5f, 1.0f);
	t.Check(playhead.GetState() == TimelinePlayState::Playing, "再生中のシークは再生を続ける");

	// --- ループ ---
	TimelinePlayhead loop;
	loop.Play();
	loop.Advance(1.25f, 1.0f, true, ranges);
	t.Near(loop.GetTime(), 0.25f, "ループの余り");
	t.Check(loop.GetLoopCount() == 1 && loop.GetState() == TimelinePlayState::Playing, "ループしても再生を続ける");
	loop.Advance(1000000.0f, 1.0f, true, ranges);
	t.Check(ranges.size() <= TimelinePlayhead::kMaxLoopsPerFrame + 2, "とても大きな秒でも周の数は上限まで");
	t.Check(std::isfinite(loop.GetTime()) && loop.GetTime() < 1.0f, "とても大きな秒でも長さの中に収まる");

	// --- 長さ 0 ---
	TimelinePlayhead zero;
	zero.Play();
	zero.Advance(0.1f, 0.0f, true, ranges);
	t.Check(ranges.size() == 1 && zero.GetState() == TimelinePlayState::Finished, "長さ 0 は1回で終わる（Loop でも止まる）");
}

// ===== イベント =====
void TestEvents(Tester& t, Handle<Entity> owner) {
	const std::unique_ptr<TimelineEventTrack> track = MakeEventTrack({0.0f, 0.5f, 1.0f});

	TimelinePlayhead play;
	play.Play();
	t.Check(RunEvents(play, *track, owner, {0.25f, 0.25f, 0.25f, 0.25f}, 1.0f, false) == "e0 e1 e2 ", "全部のイベントが1回ずつ（端と、ちょうど止まる時間も）");

	TimelinePlayhead big;
	big.Play();
	t.Check(RunEvents(big, *track, owner, {5.0f}, 1.0f, false) == "e0 e1 e2 ", "1フレームで全部通っても1回ずつ");

	TimelinePlayhead loop;
	loop.Play();
	t.Check(RunEvents(loop, *track, owner, {0.75f, 0.75f, 0.75f}, 1.0f, true) == "e0 e1 e2 e0 e1 e2 e0 ", "ループは周ごとに1回ずつ");

	TimelinePlayhead wraps;
	wraps.Play();
	t.Check(RunEvents(wraps, *track, owner, {2.25f}, 1.0f, true) == "e0 e1 e2 e0 e1 e2 e0 ", "1フレームに2周しても周ごとに1回ずつ");

	TimelinePlayhead seek;
	seek.Play();
	t.Check(RunEvents(seek, *track, owner, {0.25f}, 1.0f, false) == "e0 ", "シークの前");
	seek.Seek(0.75f, 1.0f);
	t.Check(RunEvents(seek, *track, owner, {0.5f}, 1.0f, false) == "e2 ", "シークで飛ばしたイベントは呼ばない");

	TimelinePlayhead exact;
	exact.Play();
	exact.Seek(0.5f, 1.0f);
	t.Check(RunEvents(exact, *track, owner, {0.25f}, 1.0f, false).empty(), "シークした先ちょうどのイベントは呼ばない");

	TimelinePlayhead back;
	back.Play();
	RunEvents(back, *track, owner, {0.75f}, 1.0f, false);
	back.Seek(0.25f, 1.0f);
	t.Check(RunEvents(back, *track, owner, {0.5f}, 1.0f, false) == "e1 ", "前へ戻ると、もう一度通ったイベントは呼ぶ");

	TimelinePlayhead again;
	again.Play();
	RunEvents(again, *track, owner, {0.25f}, 1.0f, false);
	again.Stop();
	again.Play();
	t.Check(RunEvents(again, *track, owner, {0.25f}, 1.0f, false) == "e0 ", "Stop の後の Play は 0 のイベントをもう一度呼ぶ");

	TimelinePlayhead paused;
	paused.Play();
	RunEvents(paused, *track, owner, {0.25f}, 1.0f, false);
	paused.Pause();
	t.Check(RunEvents(paused, *track, owner, {0.5f}, 1.0f, false).empty(), "一時停止中は呼ばない");
	paused.Play();
	t.Check(RunEvents(paused, *track, owner, {0.5f}, 1.0f, false) == "e1 ", "一時停止の続きは1回だけ呼ぶ");

	const std::unique_ptr<TimelineEventTrack> same = MakeEventTrack({0.5f, 0.5f});
	TimelinePlayhead sameTime;
	sameTime.Play();
	t.Check(RunEvents(sameTime, *same, owner, {1.0f}, 1.0f, false) == "e0 e1 ", "同じ時間のイベントはファイルの順");

	const std::unique_ptr<TimelineEventTrack> zero = MakeEventTrack({0.0f});
	TimelinePlayhead zeroLength;
	zeroLength.Play();
	t.Check(RunEvents(zeroLength, *zero, owner, {0.1f, 0.1f}, 0.0f, true) == "e0 ", "長さ 0 の Timeline のイベントは1回");
}

// ===== パス =====
void TestPath(Tester& t, Handle<Entity> root, Handle<Entity> panel, Handle<Entity> title) {
	t.Check(EntityManager::FindByPath(root, "") == root, "空のパスは自分");
	t.Check(EntityManager::FindByPath(root, "Panel") == panel, "子");
	t.Check(EntityManager::FindByPath(root, "Panel/Title") == title, "子の子");
	t.Check(!EntityManager::FindByPath(root, "Panel/Nope").IsValid(), "無い子");
	t.Check(!EntityManager::FindByPath(root, "Title").IsValid(), "飛ばした段は見つからない");
	t.Check(!EntityManager::FindByPath(Handle<Entity>{}, "Panel").IsValid(), "無効な Entity から探す");
}

// ===== 値の書き込み =====
void TestSetValue(Tester& t, Handle<Entity> entity) {
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	const ImageComponent* image = EntityManager::Get<ImageComponent>(entity);

	t.Check(SetChannel(entity, "Transform", "translation", 0, 5.0f) == ComponentValueResult::Written, "translation を書ける");
	t.Check(transform->translation.x == 5.0f && transform->translation.y == 0.0f, "使わない成分は変わらない");
	t.Check(SetChannel(entity, "Transform", "scale", 1, 2.0f) == ComponentValueResult::Written && transform->scale.y == 2.0f && transform->scale.x == 1.0f, "scale の y だけ");
	t.Check(SetChannel(entity, "Transform", "rotation", 2, 90.0f) == ComponentValueResult::Written, "rotation を書ける（中はクォータニオンでも、度で書く）");
	t.Near(GetEulerAngles(*transform).z, 90.0f, "rotation の z");
	t.Check(transform->rotation.w != 1.0f, "クォータニオンも変わる");

	t.Check(SetChannel(entity, "Image", "color", 3, 0.25f) == ComponentValueResult::Written, "色を書ける");
	t.Check(image->color.w == 0.25f && image->color.x == 1.0f, "色の a だけ");
	t.Check(SetChannel(entity, "Image", "size", 0, 40.0f) == ComponentValueResult::Written && image->size.x == 40.0f, "Vector2");
	t.Check(SetChannel(entity, "Image", "fill", 0, 0.5f) == ComponentValueResult::Written && image->fill == 0.5f, "float");
	t.Check(SetChannel(entity, "Image", "frame", 0, 2.6f) == ComponentValueResult::Written && image->frame == 3, "int は四捨五入");
	t.Check(SetChannel(entity, "Image", "enabled", 0, 0.0f) == ComponentValueResult::Written && !image->enabled, "bool");

	t.Check(SetChannel(entity, "Nope", "color", 0, 1.0f) == ComponentValueResult::NoComponent, "知らない Component");
	t.Check(SetChannel(entity, "Text", "color", 0, 1.0f) == ComponentValueResult::NoComponent, "付いていない Component");
	t.Check(SetChannel(entity, "Image", "nope", 0, 1.0f) == ComponentValueResult::NoField, "無い項目");
	t.Check(SetChannel(entity, "Image", "anchor", 0, 1.0f) == ComponentValueResult::NoField, "数でない項目（enum）");
	t.Check(SetChannel(Handle<Entity>{}, "Image", "color", 0, 1.0f) == ComponentValueResult::NoComponent, "無効な Entity");
}

// ===== 値のトラック =====
void TestValueTrack(Tester& t, Handle<Entity> rootA, Handle<Entity> titleA, Handle<Entity> rootB, Handle<Entity> titleB) {
	TimelineValueTrack track;
	track.target = "Panel/Title";
	track.component = "Image";
	track.property = "color";
	TimelineValueClip clip;
	clip.start = 1.0f;
	clip.duration = 1.0f;
	clip.curves[3] = Curve({
	    CurveKey{.time = 0.0f, .value = 0.0f, .interp = CurveInterp::Linear},
        CurveKey{.time = 1.0f, .value = 1.0f}
    });
	track.clips.push_back(clip);

	const std::unique_ptr<TimelineTrackState> stateA = track.CreateState();
	const std::unique_ptr<TimelineTrackState> stateB = track.CreateState();
	const ImageComponent* imageA = EntityManager::Get<ImageComponent>(titleA);
	const ImageComponent* imageB = EntityManager::Get<ImageComponent>(titleB);

	// 同じトラックを2つの Entity で、別の時間に動かす
	track.Evaluate({rootA, 0.5f, {}, nullptr}, *stateA);
	t.Near(imageA->color.w, 0.0f, "最初のクリップの前は、そのクリップの始まりの値");
	track.Evaluate({rootA, 1.5f, {}, nullptr}, *stateA);
	track.Evaluate({rootB, 1.25f, {}, nullptr}, *stateB);
	t.Near(imageA->color.w, 0.5f, "クリップの途中");
	t.Near(imageB->color.w, 0.25f, "同じトラックを別の Entity で別の時間に");
	t.Check(stateA->target == titleA && stateB->target == titleB, "対象は Entity ごとに見つかる");
	track.Evaluate({rootA, 5.0f, {}, nullptr}, *stateA);
	t.Near(imageA->color.w, 1.0f, "クリップの後は終わりの値のまま");

	// 2つのクリップの間は、前のクリップの終わりの値のまま
	TimelineValueClip second = clip;
	second.start = 4.0f;
	second.curves[3] = Curve({
	    CurveKey{.time = 0.0f, .value = 0.25f}
    });
	track.clips.push_back(second);
	track.Evaluate({rootA, 3.0f, {}, nullptr}, *stateA);
	t.Near(imageA->color.w, 1.0f, "クリップの間は前のクリップの終わりの値");
	track.Evaluate({rootA, 4.5f, {}, nullptr}, *stateA);
	t.Near(imageA->color.w, 0.25f, "次のクリップに入る");

	// キーの無いトラックは何も書かない
	TimelineValueTrack empty;
	empty.target = "Panel/Title";
	empty.component = "Image";
	empty.property = "color";
	const std::unique_ptr<TimelineTrackState> stateEmpty = empty.CreateState();
	empty.Evaluate({rootA, 0.0f, {}, nullptr}, *stateEmpty);
	t.Near(imageA->color.w, 0.25f, "クリップの無いトラックは書かない");
}

// ===== ファイル =====
void TestJson(Tester& t) {
	const char* text = R"({
		"settings": {"length": 2.0},
		"tracks": [
			{"type": "TimelineValueTrack", "settings": {"name": "Fade", "target": "Panel", "component": "Image", "property": "color"},
			 "clips": [
				{"start": 3.0, "duration": 1.0, "curves": {"w": {"keys": [{"time": 0.0, "value": 1.0}, {"time": 1.0, "value": 0.0}]}}},
				{"start": 0.5, "duration": 1.0, "curves": {"w": {"keys": [{"time": 0.0, "value": 0.0}, {"time": 1.0, "value": 1.0}]}}}
			 ]},
			{"type": "TimelineEventTrack", "settings": {"name": "Calls"}, "clips": [{"start": 1.0, "name": "Hit", "value": "a"}]},
			{"type": "NotRegistered", "settings": {"name": "Later"}, "clips": []}
		]
	})";
	std::set<std::string> warnings;
	const Timeline timeline = TimelineManager::FromJson(SceneJson::parse(text), warnings);
	t.Check(timeline.tracks.size() == 2 && timeline.unknownTracks.size() == 1, "知らない種類は動かさず覚えておく");
	t.Check(warnings.size() == 1, "警告は知らない種類の1つだけ");
	t.Near(timeline.GetDuration(), 4.0f, "長さはクリップの終わりまで伸びる");
	const auto* value = dynamic_cast<const TimelineValueTrack*>(timeline.tracks[0].get());
	t.Check(value != nullptr && value->component == "Image" && value->clips.size() == 2, "値のトラックを読める");
	t.Check(value != nullptr && value->clips[0].start == 0.5f, "クリップは時間の順に並ぶ");
	t.Check(value != nullptr && value->clips[0].curves[3].GetKeys().size() == 2 && value->clips[0].curves[0].GetKeys().empty(), "使う成分だけカーブがある");

	// --- 書いて読むと同じ文字になる ---
	const std::string first = TimelineManager::ToJson(timeline).dump();
	std::set<std::string> ignored;
	const std::string second = TimelineManager::ToJson(TimelineManager::FromJson(SceneJson::parse(first), ignored)).dump();
	t.Check(first == second, "書いて読むと同じ中身になる");
	t.Check(first.find("NotRegistered") != std::string::npos, "知らない種類は保存しても消えない");

	// --- 壊れた形でも止まらない ---
	std::set<std::string> broken;
	t.Check(TimelineManager::FromJson(SceneJson::parse("{}"), broken).tracks.empty(), "空のファイル");
	t.Check(TimelineManager::FromJson(SceneJson::parse(R"({"tracks": 5})"), broken).tracks.empty(), "tracks が配列でない");
	t.Check(TimelineManager::FromJson(SceneJson::parse(R"({"tracks": [1, {"name": "x"}]})"), broken).tracks.empty(), "type の無いトラック");
	const Timeline odd = TimelineManager::FromJson(SceneJson::parse(R"({"tracks": [{"type": "TimelineEventTrack", "clips": [1, {"start": "x"}, {"start": -3.0}]}]})"), broken);
	t.Check(odd.tracks.size() == 1 && odd.tracks[0]->GetClipCount() == 2, "壊れたクリップは飛ばす");
	t.Check(!broken.empty(), "壊れた物は警告を出す");

	t.Check(TimelineTrackRegistry::Create("TimelineValueTrack") != nullptr && TimelineTrackRegistry::Create("TimelineEventTrack") != nullptr, "エンジンのトラックが登録されている");
	t.Check(TimelineTrackRegistry::Create("NotRegistered") == nullptr, "知らない名前は作らない");
}

// ===== 値を読む（SetValue の逆）=====
void TestGetValue(Tester& t, Handle<Entity> root, Handle<Entity> panel, Handle<Entity> title) {
	SetChannel(title, "Image", "color", 3, 0.75f);
	ComponentValue value;
	t.Check(ComponentSerializer::GetValue(title, "Image", "color", value) == ComponentValueResult::Written, "読める");
	t.Near(value.channels[3], 0.75f, "色の a を読む");
	t.Check(value.isUsed[3], "読んだ成分に印が付く");
	ComponentValue none;
	t.Check(ComponentSerializer::GetValue(title, "Unknown", "color", none) == ComponentValueResult::NoComponent, "知らない Component");
	t.Check(ComponentSerializer::GetValue(title, "Image", "unknown", none) == ComponentValueResult::NoField, "無い項目");

	const std::vector<std::string> names = ComponentSerializer::GetComponentNames(title);
	t.Check(std::ranges::find(names, std::string("Image")) != names.end() && std::ranges::find(names, std::string("Transform")) != names.end(), "持っている Component の名前");
	t.Check(std::ranges::find(names, std::string("Text")) == names.end(), "持っていない Component は出ない");

	const std::vector<ComponentValueField> fields = ComponentSerializer::GetValueFields(title, "Image");
	const auto color = std::ranges::find(fields, std::string("color"), &ComponentValueField::key);
	t.Check(color != fields.end() && color->channelCount == 4 && color->isColor, "色の項目は 4 成分の色");
	t.Check(ComponentSerializer::GetValueFields(title, "Text").empty(), "持っていない Component の項目は空");

	t.Check(EntityManager::MakePath(root, title) == "Panel/Title", "孫までのパス");
	t.Check(EntityManager::MakePath(panel, title) == "Title", "子までのパス");
	t.Check(EntityManager::MakePath(root, root) == "", "自分は空のパス");
	t.Check(!EntityManager::MakePath(title, root).has_value(), "子孫でなければ作らない");
	t.Check(EntityManager::FindByPath(root, *EntityManager::MakePath(root, title)) == title, "FindByPath で同じ Entity に戻る");
}

// ===== プレビュー：書いた値が1ビットも変わらず戻る =====
void TestCapture(Tester& t, Handle<Entity> title) {
	// 度とクォータニオンを行き来するとずれる回転を入れておく
	SetChannel(title, "Transform", "rotation", 0, 33.3f);
	SetChannel(title, "Transform", "rotation", 1, 71.7f);
	SetChannel(title, "Transform", "rotation", 2, -123.4f);
	SetChannel(title, "Image", "color", 3, 1.0f);
	const TransformComponent transformBefore = *EntityManager::Get<TransformComponent>(title);
	bool isSame = true;
	for (int i = 0; i < 1000; ++i) { // 毎フレーム繰り返すので、たまったずれも見る
		ComponentSerializer::BeginValueCapture();
		SetChannel(title, "Transform", "rotation", 1, 10.0f);
		SetChannel(title, "Transform", "translation", 0, 5.0f);
		SetChannel(title, "Image", "color", 3, 0.25f);
		isSame = isSame && EntityManager::Get<ImageComponent>(title)->color.w == 0.25f; // 書いている間は書いた値
		ComponentSerializer::RestoreCapturedValues();
		const TransformComponent& now = *EntityManager::Get<TransformComponent>(title);
		isSame = isSame && std::memcmp(&now.rotation, &transformBefore.rotation, sizeof(now.rotation)) == 0 && now.translation.x == transformBefore.translation.x;
		isSame = isSame && EntityManager::Get<ImageComponent>(title)->color.w == 1.0f;
	}
	t.Check(isSame, "1000 回書いて戻しても、1ビットも変わらない");
	SetChannel(title, "Image", "color", 3, 0.5f);
	t.Near(EntityManager::Get<ImageComponent>(title)->color.w, 0.5f, "戻した後の SetValue はそのまま残る");
	SetChannel(title, "Image", "color", 3, 1.0f);
}

// ===== プレビュー：TimelineSystem を通す =====
void TestPreview(Tester& t, Handle<Entity> root, Handle<Entity> title) {
	const std::filesystem::path path = std::filesystem::temp_directory_path() / "MyEngineTimelineSelfTest.timeline";
	std::filesystem::remove(path);
	t.Check(TimelineManager::CreateNewFile(path), "確認用のファイルを作る");
	const uint32_t handle = TimelineManager::Load(path);
	TimelineManager::ApplyText(handle, "{}"); // 読み込み済みの中身が残っていても、空から始める
	Timeline* timeline = TimelineManager::GetForEdit(handle);
	auto* track = static_cast<TimelineValueTrack*>(timeline->tracks[TimelineEdit::AddTrack(*timeline, "TimelineValueTrack")].get());
	track->target = "Panel/Title";
	track->component = "Image";
	track->property = "color";
	const size_t clip = track->AddClip(0.0f, 1.0f);
	TimelineEdit::FillFlat(track->GetClipCurves(clip)[3], 0.25f, 1.0f);
	TimelineManager::MarkChanged(handle);

	const auto alpha = [title] { return EntityManager::Get<ImageComponent>(title)->color.w; };
	TimelineSystem::RequestPreview(root, handle, 0.5f);
	TimelineSystem::Update(false);
	t.Near(alpha(), 0.25f, "Update の後はプレビューの値");
	TimelineSystem::FinishPreview();
	t.Near(alpha(), 1.0f, "FinishPreview で元の値");

	TimelineSystem::Update(false);
	t.Near(alpha(), 1.0f, "頼まなければ書かない（1回の要求は1フレームだけ）");
	TimelineSystem::FinishPreview();

	track->isMuted = true;
	TimelineSystem::RequestPreview(root, handle, 0.5f);
	TimelineSystem::Update(false);
	t.Near(alpha(), 1.0f, "Mute のトラックは書かない");
	TimelineSystem::FinishPreview();
	track->isMuted = false;

	TimelineSystem::RequestPreview(root, handle, 0.5f);
	TimelineSystem::Update(true);
	t.Near(alpha(), 1.0f, "Play 中はプレビューしない");
	TimelineSystem::Update(false);

	TimelineSystem::RequestPreview({}, handle, 0.5f);
	TimelineSystem::Update(false);
	TimelineSystem::FinishPreview();
	t.Near(alpha(), 1.0f, "無効な Entity のプレビューは何もしない");

	std::filesystem::remove(path);
}

// ===== トラックとクリップを直す =====
void TestEditTracks(Tester& t) {
	Timeline timeline;
	t.Check(TimelineEdit::AddTrack(timeline, "NotRegistered") == -1, "知らない種類は足さない");
	for (const char* name : {"a", "b", "c"}) {
		timeline.tracks[TimelineEdit::AddTrack(timeline, "TimelineEventTrack")]->name = name;
	}
	const auto order = [&timeline] {
		std::string text;
		for (const std::unique_ptr<TimelineTrack>& track : timeline.tracks) {
			text += track->name;
		}
		return text;
	};
	t.Check(TimelineEdit::MoveTrack(timeline, 0, 2) == 2 && order() == "bca", "後ろへ動かす");
	t.Check(TimelineEdit::MoveTrack(timeline, 2, 0) == 0 && order() == "abc", "前へ動かす");
	t.Check(TimelineEdit::MoveTrack(timeline, 1, 99) == 2 && order() == "acb", "範囲の外は端へ");
	TimelineEdit::RemoveTrack(timeline, 1);
	t.Check(order() == "ab", "消す");
	TimelineEdit::RemoveTrack(timeline, 9);
	t.Check(order() == "ab", "知らない番号は何もしない");

	TimelineValueTrack track;
	t.Check(track.AddClip(2.0f, 1.0f) == 0, "最初のクリップ");
	t.Check(track.AddClip(1.0f, 0.5f) == 0, "前に足すと 0 番");
	t.Check(track.AddClip(3.0f, 1.0f) == 2, "後ろに足す");
	t.Check(track.SetClipTime(0, 5.0f, 2.0f) == 2 && track.GetClipStart(2) == 5.0f && track.GetClipDuration(2) == 2.0f, "追い越すと番号が変わる");
	t.Check(track.GetClipStart(0) == 2.0f && track.GetClipStart(1) == 3.0f, "ほかのクリップは時間の順のまま");
	t.Check(track.SetClipTime(0, -3.0f, kNaN) == 0 && track.GetClipStart(0) == 0.0f && track.GetClipDuration(0) == 0.0f, "負の時間と NaN は 0");
	track.RemoveClip(0);
	t.Check(track.GetClipCount() == 2, "クリップを消す");
	t.Check(track.HasClipDuration() && !track.GetClipCurves(0).empty(), "値のクリップは長さとカーブがある");

	TimelineEventTrack events;
	t.Check(!events.HasClipDuration(), "イベントは1点");
	t.Check(events.AddClip(1.0f, 5.0f) == 0 && events.GetClipDuration(0) == 0.0f, "イベントの長さは 0 のまま");
	t.Check(events.GetClipCurves(0).empty(), "イベントにカーブは無い");
	t.Check(!events.CanPreview() && track.CanPreview(), "プレビューするのは値のトラックだけ");

	TimelineValueTrack gaps;
	gaps.AddClip(2.0f, 1.0f);
	t.Near(TimelineEdit::CalcNewClipDuration(gaps, 1.5f), 0.5f, "次のクリップの手前まで");
	t.Near(TimelineEdit::CalcNewClipDuration(gaps, 0.0f), TimelineEdit::kDefaultClipDuration, "離れていれば既定の長さ");
	t.Near(TimelineEdit::CalcNewClipDuration(gaps, 3.0f), TimelineEdit::kDefaultClipDuration, "後ろのクリップが無ければ既定の長さ");
}

// ===== キーを直す =====
void TestEditKeys(Tester& t) {
	std::array<Curve, 2> curves;
	TimelineEdit::FillFlat(curves[0], 2.0f, 1.0f);
	t.Check(curves[0].GetKeys().size() == 2 && curves[0].GetKeys()[1].time == 1.0f && curves[0].GetKeys()[1].value == 2.0f, "平らな線は2つのキー");
	Curve single;
	TimelineEdit::FillFlat(single, 3.0f, 0.0f);
	t.Check(single.GetKeys().size() == 1, "長さ 0 ならキーは1つ");

	curves[1].AddKey(0.5f, 7.0f);
	const std::vector<float> times = TimelineEdit::CollectKeyTimes(curves);
	t.Check(times.size() == 3 && times[1] == 0.5f, "別の成分のキーも時間の順に集める");
	curves[1].AddKey(1.0f + TimelineEdit::kSameKeyTime * 0.5f, 7.0f);
	t.Check(TimelineEdit::CollectKeyTimes(curves).size() == 3, "ほとんど同じ時間のキーは1つに見せる");

	Curve shape;
	shape.AddKey(0.0f, 0.0f);
	shape.AddKey(1.0f, 1.0f);
	const float middle = shape.Evaluate(0.3f);
	TimelineEdit::AddKeys(std::span(&shape, 1), 0.5f);
	t.Check(shape.GetKeys().size() == 3, "キーが増える");
	t.Near(shape.Evaluate(0.3f), middle, "足しても形は変わらない");
	Curve empty;
	TimelineEdit::AddKeys(std::span(&empty, 1), 0.5f);
	t.Check(empty.GetKeys().empty(), "キーの無い成分には足さない");

	TimelineEdit::MoveKeys(curves, 1.0f, 0.75f);
	t.Check(curves[0].GetKeys()[1].time == 0.75f, "1つの成分のキーを動かす");
	t.Check(curves[1].GetKeys()[1].time == 0.75f, "同じ時間の別の成分のキーも動く");
	TimelineEdit::MoveKeys(curves, 0.75f, 0.25f);
	t.Check(curves[1].GetKeys()[0].time == 0.25f && curves[1].GetKeys()[1].time == 0.5f, "追い越すと並びが直る");

	TimelineEdit::RemoveKeys(curves, 0.25f);
	t.Check(curves[0].GetKeys().size() == 1 && curves[1].GetKeys().size() == 1, "同じ時間のキーを全部の成分で消す");
}

// ===== Undo の文字 =====
void TestEditText(Tester& t) {
	const std::filesystem::path path = std::filesystem::temp_directory_path() / "MyEngineTimelineSelfTest2.timeline";
	std::filesystem::remove(path);
	TimelineManager::CreateNewFile(path);
	const uint32_t handle = TimelineManager::Load(path);
	TimelineManager::ApplyText(handle, "{}"); // 読み込み済みの中身が残っていても、空から始める
	Timeline* timeline = TimelineManager::GetForEdit(handle);
	const std::string empty = TimelineManager::ToText(handle);

	const int index = TimelineEdit::AddTrack(*timeline, "TimelineValueTrack");
	TimelineTrack& track = *timeline->tracks[index];
	track.name = "Edited";
	const size_t clip = track.AddClip(0.5f, 2.0f);
	TimelineEdit::FillFlat(track.GetClipCurves(clip)[1], 3.0f, 2.0f);
	const std::string edited = TimelineManager::ToText(handle);
	t.Check(edited != empty, "直すと文字が変わる");

	const uint32_t revision = TimelineManager::GetRevision(handle);
	t.Check(TimelineManager::ApplyText(handle, empty), "前の文字へ戻す");
	t.Check(TimelineManager::GetRevision(handle) == revision + 1, "戻すと版が増える");
	t.Check(TimelineManager::Get(handle)->tracks.empty() && TimelineManager::ToText(handle) == empty, "戻した中身");
	t.Check(TimelineManager::ApplyText(handle, edited), "やり直す");
	t.Check(TimelineManager::ToText(handle) == edited, "文字で往復しても同じ");
	const TimelineTrack& restored = *TimelineManager::Get(handle)->tracks[0];
	t.Check(restored.name == "Edited" && restored.GetClipCount() == 1, "トラックとクリップが戻る");

	t.Check(!TimelineManager::ApplyText(handle, "{ broken"), "壊れた文字は読まない");
	t.Check(TimelineManager::ToText(handle) == edited, "読めなければ中身は変わらない");
	t.Check(!TimelineManager::ApplyText(0, empty), "知らない番号は読まない");
	std::filesystem::remove(path);
}

// ===== 見ている時間の範囲 =====
void TestTimeView(Tester& t) {
	TimelineTimeView view;
	view.pixelsPerSecond = 100.0f;
	view.scrollTime = 2.0f;
	t.Near(view.ToPixel(3.0f), 100.0f, "時間 → ピクセル");
	t.Near(view.ToTime(100.0f), 3.0f, "ピクセル → 時間");
	view.ZoomAt(100.0f, 2.0f);
	t.Near(view.pixelsPerSecond, 200.0f, "拡大する");
	t.Near(view.ToTime(100.0f), 3.0f, "マウスの下の時間は動かない");
	view.ZoomAt(0.0f, 1000000.0f);
	t.Check(view.pixelsPerSecond == TimelineTimeView::kMaxPixelsPerSecond, "拡大の上限");
	view.ZoomAt(0.0f, 0.000001f);
	t.Check(view.pixelsPerSecond == TimelineTimeView::kMinPixelsPerSecond, "縮小の下限");
	view.scrollTime = 1.0f;
	view.ZoomAt(100000.0f, 2.0f);
	t.Check(view.scrollTime >= 0.0f, "左端は 0 秒より前へ行かない");
	view.Pan(-1000000000.0f);
	t.Check(view.scrollTime == 0.0f, "左へ動かしても 0 秒まで");
	view.Fit(10.0f, 1100.0f);
	t.Near(view.pixelsPerSecond, 100.0f, "全体が入る");
	t.Check(view.scrollTime == 0.0f, "全体を見ると左端は 0 秒");
	view.pixelsPerSecond = 100.0f;
	t.Near(view.CalcTickStep(60.0f), 1.0f, "60 ピクセル以上の切りの良い間隔");
	view.pixelsPerSecond = 400.0f;
	t.Near(view.CalcTickStep(60.0f), 0.2f, "広げると間隔が細かくなる");

	t.Near(SnapTime(0.26f, 0.1f), 0.3f, "目盛りへそろえる");
	t.Near(SnapTime(0.24f, 0.1f), 0.2f, "近い方へそろえる");
	t.Near(SnapTime(1.0f / 30.0f * 7.4f, 1.0f / 30.0f), 7.0f / 30.0f, "1/30 秒へそろえる");
	t.Check(SnapTime(0.26f, 0.0f) == 0.26f, "0 ならそろえない");
}

// ===== root・Panel・Title（Image 付き）の親子を作る =====
Handle<Entity> MakeTestEntities(Handle<Entity>& panel, Handle<Entity>& title) {
	const Handle<Entity> root = EntityManager::Create("TimelineTest");
	panel = EntityManager::Create("Panel", root);
	title = EntityManager::Create("Title", panel);
	EntityManager::RestoreComponents(title, {ComponentSnapshot::Make(ImageComponent{})});
	return root;
}
} // namespace

//=============================================================================
// 確かめる
//=============================================================================
void TimelineSelfTest::Run() {
	Tester tester;
	Handle<Entity> panelA;
	Handle<Entity> titleA;
	Handle<Entity> panelB;
	Handle<Entity> titleB;
	const Handle<Entity> rootA = MakeTestEntities(panelA, titleA);
	const Handle<Entity> rootB = MakeTestEntities(panelB, titleB);

	TestPlayhead(tester);
	TestEvents(tester, rootA);
	TestPath(tester, rootA, panelA, titleA);
	TestValueTrack(tester, rootA, titleA, rootB, titleB);
	TestSetValue(tester, titleA);
	TestJson(tester);
	TestGetValue(tester, rootA, panelA, titleA);
	TestCapture(tester, titleA);
	TestPreview(tester, rootA, titleA);
	TestEditTracks(tester);
	TestEditKeys(tester);
	TestEditText(tester);
	TestTimeView(tester);

	// 作った Entity をすぐ消す（シーンを作る前なので、残さない）
	EntityManager::Destroy(rootA);
	EntityManager::Destroy(rootB);
	EntityManager::FlushDestroy();

	MY_ASSERT_MSG(tester.failed == 0, std::format("Timeline の確認が合いません（{} 項目。ログを見てください）", tester.failed));
	LogManager::Log(std::format("[timeline] self test OK ({} / {})", tester.passed, tester.passed + tester.failed));
}