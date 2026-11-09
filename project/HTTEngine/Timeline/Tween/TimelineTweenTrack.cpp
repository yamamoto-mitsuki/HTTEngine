#include "TimelineTweenTrack.h"

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <vector>

#include <externals/magic_enum/magic_enum.hpp>
#include <externals/nlohmann/json.hpp>

#include "HTTEngine/Curve/CurveManager.h"
#include "HTTEngine/ECS/ComponentSerializer.h"

namespace {
constexpr const char* kFromKey = "from";           // ファイルの From の項目の名前
constexpr const char* kToKey = "to";               // ファイルの To の項目の名前
constexpr const char* kEaseKey = "ease";           // ファイルの Ease のカーブの項目の名前
constexpr const char* kModeKey = "mode";           // 端の決め方
constexpr const char* kValueKey = "value";         // 端の値
constexpr const char* kParameterKey = "parameter"; // Parameter の名前

/// <summary>
/// Tween のトラックの、Entity ごとの値
/// </summary>
class TimelineTweenState : public TimelineTrackState {
public:
	std::vector<std::optional<ComponentValue>> startValues; // [クリップの番号] 始まったときの今の値（Current に使う。無ければまだ始まっていない）
	float lastTime = 0.0f;                                  // 前に動かした時間（戻ったら、始まったときの値を取り直す）
};

/// <summary>
/// 覚えた元の値1つ（Add・Multiply の基準）
/// </summary>
struct BaseValue {
	Handle<Entity> entity;
	std::string component;
	std::string property;
	ComponentValue value;
};
std::vector<BaseValue> baseValues; // 覚えた元の値（Entity が消える・エディタの停止で忘れる。数が少ないので一列に並べて順に探す）

// ===== Vector4 を成分の並びにする =====
std::array<float, ComponentValue::kChannelCount> ToChannels(const Vector4& value) { return {value.x, value.y, value.z, value.w}; }

// ===== 元の値（初めて聞かれたら、今の値を覚える）=====
ComponentValue FindBaseValue(Handle<Entity> entity, const TimelinePropertyBinding& binding, const ComponentValue& current) {
	const auto found = std::ranges::find_if(baseValues, [&](const BaseValue& base) { return base.entity == entity && base.component == binding.component && base.property == binding.property; });
	if (found != baseValues.end()) {
		return found->value;
	}
	baseValues.push_back({entity, binding.component, binding.property, current});
	return current;
}

// ===== 端の値を決める =====
// start は始まったときの値（Current）、base は元の値（Add・Multiply）
ComponentValue ResolvePoint(const TimelineTweenPoint& point, const ComponentValue& start, const ComponentValue& base, const TimelineOverrides* overrides) {
	const Vector4* parameter = (point.mode == TimelineTweenMode::Parameter && overrides != nullptr) ? overrides->FindParameter(point.parameter) : nullptr;
	const std::array<float, ComponentValue::kChannelCount> value = ToChannels((parameter != nullptr) ? *parameter : point.value);
	ComponentValue result = start; // 使う成分（isUsed）は今の値と同じ
	for (size_t i = 0; i < ComponentValue::kChannelCount; ++i) {
		switch (point.mode) {
		case TimelineTweenMode::Current:
			break; // 始まったときの値のまま
		case TimelineTweenMode::Value:
		case TimelineTweenMode::Parameter:
			result.channels[i] = value[i];
			break;
		case TimelineTweenMode::Add:
			result.channels[i] = base.channels[i] + value[i];
			break;
		case TimelineTweenMode::Multiply:
			result.channels[i] = base.channels[i] * value[i];
			break;
		}
	}
	return result;
}

//=============================================================================
// ファイル
//=============================================================================
// ===== 端1つを書く =====
SceneJson PointToJson(const TimelineTweenPoint& point) {
	SceneJson json = SceneJson::object();
	json[kModeKey] = std::string(magic_enum::enum_name(point.mode));
	json[kValueKey] = SceneJson::array({point.value.x, point.value.y, point.value.z, point.value.w});
	if (!point.parameter.empty()) {
		json[kParameterKey] = point.parameter;
	}
	return json;
}

// ===== 端1つを読む（読めない項目は既定の値のまま）=====
void PointFromJson(const SceneJson& json, TimelineTweenPoint& point, std::set<std::string>& warnings) {
	if (!json.is_object()) {
		warnings.insert("Tween の端（from・to）が読めません");
		return;
	}
	if (const auto mode = json.find(kModeKey); mode != json.end() && mode->is_string()) {
		// enum_cast：名前から enum を探す（知らない名前なら空）
		const auto parsed = magic_enum::enum_cast<TimelineTweenMode>(mode->get_ref<const std::string&>());
		if (parsed.has_value()) {
			point.mode = *parsed;
		} else {
			warnings.insert(std::format("知らない Tween の決め方 \"{}\"", mode->get_ref<const std::string&>()));
		}
	}
	if (const auto value = json.find(kValueKey); value != json.end() && value->is_array() && value->size() == ComponentValue::kChannelCount) {
		std::array<float, ComponentValue::kChannelCount> channels{};
		for (size_t i = 0; i < channels.size(); ++i) {
			channels[i] = (*value)[i].is_number() ? (*value)[i].get<float>() : 0.0f;
		}
		point.value = {channels[0], channels[1], channels[2], channels[3]};
	}
	if (const auto name = json.find(kParameterKey); name != json.end() && name->is_string()) {
		point.parameter = name->get_ref<const std::string&>();
	}
}
} // namespace

//=============================================================================
// クリップのファイル
//=============================================================================
void TimelineTweenClip::WriteExtra(SceneJson& out) const {
	out[kFromKey] = PointToJson(from);
	out[kToKey] = PointToJson(to);
	out[kEaseKey] = CurveManager::ToJson(ease);
}

void TimelineTweenClip::ReadExtra(SceneJson& in, std::set<std::string>& warnings) {
	if (const auto item = in.find(kFromKey); item != in.end()) {
		PointFromJson(*item, from, warnings);
		in.erase(item);
	}
	if (const auto item = in.find(kToKey); item != in.end()) {
		PointFromJson(*item, to, warnings);
		in.erase(item);
	}
	if (const auto item = in.find(kEaseKey); item != in.end()) {
		ease = CurveManager::FromJson(*item, warnings);
		in.erase(item);
	}
}

//=============================================================================
// トラック
//=============================================================================
// ===== クリップの上の名前（Current → Multiply・Parameter は名前）=====
std::string TimelineTweenTrack::GetClipLabel(size_t index) const {
	const auto label = [](const TimelineTweenPoint& point) {
		return (point.mode == TimelineTweenMode::Parameter) ? std::format("${}", point.parameter) : std::string(magic_enum::enum_name(point.mode));
	};
	return std::format("{} → {}", label(clips[index].from), label(clips[index].to));
}

std::unique_ptr<TimelineTrackState> TimelineTweenTrack::CreateState() const { return std::make_unique<TimelineTweenState>(); }

// ===== 動かす =====
void TimelineTweenTrack::Evaluate(const TimelineFrame& frame, TimelineTrackState& state) const {
	if (clips.empty()) {
		return;
	}
	auto& tween = static_cast<TimelineTweenState&>(state);
	tween.startValues.resize(clips.size());
	// --- 時間が戻った（最初から・ループ・前へのシーク）：始まったときの値を取り直す ---
	if (frame.time < tween.lastTime) {
		std::ranges::fill(tween.startValues, std::nullopt);
	}
	tween.lastTime = frame.time;

	// --- 今のクリップ（最初のクリップの前は、最初のクリップの始まり）---
	const TimelineTweenClip* found = FindClip(frame.time);
	const bool isBefore = (found == nullptr);
	const TimelineTweenClip& clip = isBefore ? clips.front() : *found;
	if (isBefore && clip.from.mode == TimelineTweenMode::Current) {
		return; // 始まる前は今の値のまま（何も書かない）
	}
	const Handle<Entity> entity = FindTarget(frame, state);
	if (!entity.IsValid()) {
		return;
	}
	// --- 今の値（使う成分も、ここで分かる）---
	ComponentValue current;
	switch (ComponentSerializer::GetValue(entity, binding.component, binding.property, current)) {
	case ComponentValueResult::Written:
		break;
	case ComponentValueResult::NoComponent:
		WarnOnce(state, std::format("\"{}\" に Component \"{}\" がありません", target, binding.component));
		return;
	case ComponentValueResult::NoField:
		WarnOnce(state, std::format("Component \"{}\" に、動かせる項目 \"{}\" がありません", binding.component, binding.property));
		return;
	}
	// --- 始まったときの値・元の値（プレビューは毎フレーム置いてある値に戻るので、いつも今の値）---
	ComponentValue start = current;
	ComponentValue base = current;
	if (!frame.isPreview) {
		std::optional<ComponentValue>& started = tween.startValues[static_cast<size_t>(&clip - clips.data())];
		if (!isBefore && !started.has_value()) {
			started = current;
		}
		start = started.value_or(current);
		base = FindBaseValue(entity, binding, current);
	}
	// --- From から To へ（Ease のキーが無ければ、まっすぐ）---
	const float rate = isBefore ? 0.0f : ((clip.duration > 0.0f) ? std::clamp((frame.time - clip.start) / clip.duration, 0.0f, 1.0f) : 1.0f);
	const float amount = clip.ease.GetKeys().empty() ? rate : clip.ease.Evaluate(rate);
	const ComponentValue from = ResolvePoint(clip.from, start, base, frame.overrides);
	const ComponentValue to = ResolvePoint(clip.to, start, base, frame.overrides);
	ComponentValue value;
	for (size_t i = 0; i < ComponentValue::kChannelCount; ++i) {
		value.isUsed[i] = current.isUsed[i];
		value.channels[i] = from.channels[i] + (to.channels[i] - from.channels[i]) * amount;
	}
	ComponentSerializer::SetValue(entity, binding.component, binding.property, value);
}

//=============================================================================
// 元の値
//=============================================================================
void TimelineTweenTrack::ResetBaseValues(Handle<Entity> entity) {
	std::erase_if(baseValues, [entity](const BaseValue& base) { return base.entity == entity; });
}

void TimelineTweenTrack::ClearBaseValues() { baseValues.clear(); }