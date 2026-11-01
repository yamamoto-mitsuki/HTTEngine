#include "TimelineSelfTest.h"

#include <cmath>
#include <format>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <externals/nlohmann/json.hpp>

#include "MyEngine/Components/Core/TransformComponent.h"
#include "MyEngine/Components/UI/ImageComponent.h"
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/Timeline/TimelineEventTrack.h"
#include "MyEngine/Timeline/TimelineManager.h"
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

	// 作った Entity をすぐ消す（シーンを作る前なので、残さない）
	EntityManager::Destroy(rootA);
	EntityManager::Destroy(rootB);
	EntityManager::FlushDestroy();

	MY_ASSERT_MSG(tester.failed == 0, std::format("Timeline の確認が合いません（{} 項目。ログを見てください）", tester.failed));
	LogManager::Log(std::format("[timeline] self test OK ({} / {})", tester.passed, tester.passed + tester.failed));
}