#include "TimelineCameraTrack.h"

#include <algorithm>
#include <format>
#include <vector>

#include <externals/nlohmann/json.hpp>

#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/Systems/Camera/CameraSystem.h"

namespace {
constexpr const char* kCameraKey = "camera"; // ファイルのカメラの項目の名前
constexpr const char* kSelfLabel = "(self)"; // 再生する Entity 自身を指すときの表示

/// <summary>
/// カメラのトラックの、Entity ごとの値
/// </summary>
class TimelineCameraState : public TimelineTrackState {
public:
	std::vector<Handle<Entity>> cameras; // [クリップの番号] 見つけたカメラ（見つからなければ無効）
};

// ===== パスの先がカメラなら Handle を返す =====
Handle<Entity> FindCamera(Handle<Entity> owner, const std::string& path) {
	const Handle<Entity> entity = EntityManager::FindByPath(owner, path);
	return (EntityManager::Get<CameraComponent>(entity) != nullptr) ? entity : Handle<Entity>{};
}

// ===== クリップの番号のカメラ（通常のカメラなら無効な Handle）=====
Handle<Entity> CameraOf(const TimelineCameraState& state, int clip) { return (clip == TimelineCameraSelection::kNormalCamera) ? Handle<Entity>{} : state.cameras[clip]; }
} // namespace

//=============================================================================
// クリップ
//=============================================================================
// ===== 見せ方 =====
void TimelineCameraClip::Fields(ComponentUI& ui) {
	TimelineClip::Fields(ui);
	ui.Separator("Blend");
	ui.Field("Blend In###blendIn", blendIn, AtLeast(0.0f).Tip("前のクリップと重なっていないとき、この秒をかけて補間する。0 ならすぐ切り替える"));
	ui.Field("Blend Ease###blendEase", blendEase, Tip("始まりの補間と、前のクリップとの重なりの補間の速さの変え方"));
	ui.Separator("Exit");
	ui.Field("Exit###exit", exit, Tip("終わった後の戻し方。Cut：すぐ通常のカメラへ / Ease：Exit Time の秒で補間する / Hold：次のクリップか TimelineSystem::Stop まで、このカメラのまま"));
	ui.BeginDisabled(exit == TimelineCameraExit::Cut);
	ui.Field("Exit Time###exitTime", exitTime, AtLeast(0.0f).Tip("戻す補間の秒。Ease のときは Timeline の長さに入る。Hold は TimelineSystem::Stop で止めたときに使う"));
	ui.Field("Exit Ease###exitEase", exitEase, Tip("戻す補間の速さの変え方"));
	ui.EndDisabled();
}

// ===== ファイル（カメラは Timeline ウィンドウで一覧から選ぶので、Fields には並べない）=====
void TimelineCameraClip::WriteExtra(SceneJson& out) const { out[kCameraKey] = camera; }

void TimelineCameraClip::ReadExtra(SceneJson& in, std::set<std::string>& warnings) {
	const auto found = in.find(kCameraKey);
	if (found == in.end()) {
		return;
	}
	if (found->is_string()) {
		camera = found->get<std::string>();
	} else {
		warnings.insert("\"camera\" が文字ではないので、再生する Entity 自身を使います");
	}
	in.erase(found);
}

//=============================================================================
// カメラの選び方
//=============================================================================
TimelineCameraSelection TimelineCameraTrack::Select(float time) const {
	// --- 今の時間に掛かっているクリップを、遅く始まった方から2つ。終わったクリップは、一番遅く終わった物 ---
	int newest = -1;
	int older = -1;
	int ended = -1;
	for (int i = 0; i < static_cast<int>(clips.size()) && clips[i].start <= time; ++i) {
		if (time < clips[i].GetEnd()) {
			older = newest;
			newest = i;
		} else if (ended < 0 || clips[i].GetEnd() >= clips[ended].GetEnd()) {
			ended = i;
		}
	}
	TimelineCameraSelection selection;
	// --- クリップの中 ---
	if (newest >= 0) {
		const TimelineCameraClip& clip = clips[newest];
		const float elapsed = time - clip.start;
		selection.isActive = true;
		selection.to = newest;
		selection.exitTime = (clip.exit == TimelineCameraExit::Cut) ? 0.0f : clip.exitTime;
		selection.exitEase = clip.exitEase;
		if (older >= 0) {
			// 重なり：後のクリップの始まりから、前のクリップの終わりまでで混ぜ終わる
			selection.from = older;
			selection.weight = Ease::Apply(elapsed / (clips[older].GetEnd() - clip.start), clip.blendEase);
		} else if (elapsed < clip.blendIn) {
			// Hold で残っているカメラがあれば、そこから補間する
			selection.from = (ended >= 0 && clips[ended].exit == TimelineCameraExit::Hold) ? ended : TimelineCameraSelection::kNormalCamera;
			selection.weight = Ease::Apply(elapsed / clip.blendIn, clip.blendEase);
		}
		return selection;
	}
	// --- 最初のクリップの前は何も頼まない ---
	if (ended < 0) {
		return selection;
	}
	// --- 終わった後 ---
	const TimelineCameraClip& clip = clips[ended];
	const float remaining = clip.GetEnd() + clip.exitTime - time;
	switch (clip.exit) {
	case TimelineCameraExit::Cut:
		break;
	case TimelineCameraExit::Hold:
		selection.isActive = true;
		selection.to = ended;
		selection.exitTime = clip.exitTime;
		selection.exitEase = clip.exitEase;
		break;
	case TimelineCameraExit::Ease:
		if (remaining > 0.0f) {
			selection.isActive = true;
			selection.from = ended;
			selection.weight = Ease::Apply(1.0f - remaining / clip.exitTime, clip.exitEase);
			selection.exitTime = remaining; // 途中で止められても、残りの秒で戻す
			selection.exitEase = clip.exitEase;
		}
		break;
	}
	return selection;
}

float TimelineCameraTrack::GetEndTime() const {
	float end = 0.0f;
	for (const TimelineCameraClip& clip : clips) {
		end = (std::max)(end, clip.GetEnd() + ((clip.exit == TimelineCameraExit::Ease) ? clip.exitTime : 0.0f));
	}
	return end;
}

//=============================================================================
// 再生
//=============================================================================
std::unique_ptr<TimelineTrackState> TimelineCameraTrack::CreateState() const { return std::make_unique<TimelineCameraState>(); }

void TimelineCameraTrack::Evaluate(const TimelineFrame& frame, TimelineTrackState& state) const {
	auto& cameraState = static_cast<TimelineCameraState&>(state);
	// --- カメラを探す（Entity の数が変わったときだけ。探すのは Entity を全部調べるので、毎フレームはしない）---
	const size_t entityCount = EntityManager::GetCount();
	if (!state.isSearched || state.searchedEntities != entityCount || cameraState.cameras.size() != clips.size()) {
		state.isSearched = true;
		state.searchedEntities = entityCount;
		cameraState.cameras.clear();
		for (const TimelineCameraClip& clip : clips) {
			cameraState.cameras.push_back(FindCamera(frame.owner, clip.camera));
			if (!cameraState.cameras.back().IsValid()) {
				WarnOnce(state, std::format("カメラが見つかりません: \"{}\"", clip.camera));
			}
		}
	}
	// --- 頼む（頼まなければ、通常のカメラのまま）---
	const TimelineCameraSelection selection = Select(frame.time);
	if (!selection.isActive) {
		return;
	}
	CameraOverride request;
	request.from = CameraOf(cameraState, selection.from);
	request.to = CameraOf(cameraState, selection.to);
	request.weight = selection.weight;
	request.exitTime = selection.exitTime;
	request.exitEase = selection.exitEase;
	request.timeMode = frame.timeMode;
	CameraSystem::RequestOverride(request);
}

//=============================================================================
// Timeline ウィンドウ
//=============================================================================
std::string TimelineCameraTrack::GetClipLabel(size_t index) const {
	const std::string& path = clips[index].camera;
	const size_t slash = path.rfind('/');
	return path.empty() ? kSelfLabel : ((slash == std::string::npos) ? path : path.substr(slash + 1));
}

TimelineClipFade TimelineCameraTrack::GetClipFade(size_t index) const {
	const TimelineCameraClip& clip = clips[index];
	TimelineClipFade fade;
	fade.in = clip.blendIn;
	fade.out = (clip.exit == TimelineCameraExit::Ease) ? clip.exitTime : 0.0f;
	// 前のクリップと重なっていれば、重なった長さで混ぜる
	for (size_t i = 0; i < index; ++i) {
		if (clips[i].GetEnd() > clip.start && clips[i].GetEnd() < clip.GetEnd()) {
			fade.in = clips[i].GetEnd() - clip.start;
		}
	}
	return fade;
}

std::string TimelineCameraTrack::FindClipProblem(Handle<Entity> owner) const {
	for (const TimelineCameraClip& clip : clips) {
		if (!FindCamera(owner, clip.camera).IsValid()) {
			return std::format("カメラが見つかりません: \"{}\"", clip.camera.empty() ? kSelfLabel : clip.camera);
		}
	}
	return {};
}