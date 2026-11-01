#include "TimelineValueTrack.h"

#include <format>
#include <algorithm>

#include <externals/nlohmann/json.hpp>

#include "MyEngine/Curve/CurveManager.h"
#include "MyEngine/ECS/ComponentUI.h"

namespace {
constexpr std::array<const char*, ComponentValue::kChannelCount> kChannelNames = {"x", "y", "z", "w"}; // ファイルの成分の名前
constexpr const char* kCurvesKey = "curves";                                                           // ファイルのカーブの項目の名前
} // namespace

//=============================================================================
// クリップのファイル
//=============================================================================
// ===== 書く（キーのある成分だけ）=====
void TimelineValueClip::WriteExtra(SceneJson& out) const {
	SceneJson channels = SceneJson::object();
	for (size_t i = 0; i < curves.size(); ++i) {
		if (!curves[i].GetKeys().empty()) {
			channels[kChannelNames[i]] = CurveManager::ToJson(curves[i]);
		}
	}
	out[kCurvesKey] = std::move(channels);
}

// ===== 読む =====
void TimelineValueClip::ReadExtra(SceneJson& in, std::set<std::string>& warnings) {
	const auto channels = in.find(kCurvesKey);
	if (channels == in.end()) {
		return;
	}
	for (size_t i = 0; i < curves.size(); ++i) {
		if (const auto channel = channels->find(kChannelNames[i]); channel != channels->end()) {
			curves[i] = CurveManager::FromJson(*channel, warnings);
		}
	}
	in.erase(channels);
}

//=============================================================================
// トラック
//=============================================================================
// ===== 見せ方 =====
void TimelineValueTrack::Fields(ComponentUI& ui) {
	ui.Field("Component###component", component, Tip("動かす Component の名前（Inspector の見出しと同じ。Image・Vector Shape など）"));
	ui.Field("Property###property", property, Tip("動かす項目の名前（Transform は translation・rotation・scale。ほかはシーンファイルに書かれている名前）"));
}

// ===== 動かす =====
void TimelineValueTrack::Evaluate(const TimelineFrame& frame, TimelineTrackState& state) const {
	if (clips.empty()) {
		return;
	}
	// --- 最初のクリップの前は、そのクリップの始まりの値 ---
	const TimelineValueClip* found = FindClip(frame.time);
	const TimelineValueClip* clip = (found != nullptr) ? found : &clips.front();
	// --- 成分ごとにカーブを読む（クリップの終わりを過ぎたら、終わりの値のまま）---
	const float clipTime = std::clamp(frame.time - clip->start, 0.0f, clip->duration);
	ComponentValue value;
	for (size_t i = 0; i < clip->curves.size(); ++i) {
		if (!clip->curves[i].GetKeys().empty()) {
			value.channels[i] = clip->curves[i].Evaluate(clipTime);
			value.isUsed[i] = true;
		}
	}
	const Handle<Entity> entity = FindTarget(frame, state);
	if (!entity.IsValid()) {
		return;
	}
	// --- 書く ---
	switch (ComponentSerializer::SetValue(entity, component, property, value)) {
	case ComponentValueResult::Written:
		break;
	case ComponentValueResult::NoComponent:
		WarnOnce(state, std::format("\"{}\" に Component \"{}\" がありません", target, component));
		break;
	case ComponentValueResult::NoField:
		WarnOnce(state, std::format("Component \"{}\" に、動かせる項目 \"{}\" がありません", component, property));
		break;
	}
}