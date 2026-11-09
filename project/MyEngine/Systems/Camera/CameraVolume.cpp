#include "CameraVolume.h"

#include <algorithm>
#include <format>

#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/Graphics/PostEffect/DepthOfFieldPass.h"

namespace {
// 1項目を、左のチェックと右の値を1行に並べて見せる。保存の名前は key（チェックは key + "Override"）
// 外した項目は灰色にするだけで値は残す（隠すと保存から落ちて、チェックを入れ直したときに値が消える）
void DescribeItem(ComponentUI& ui, const char* name, const char* key, CameraVolumeValue& item, FieldStyle style) {
	ui.Field(std::format("###{}Override", key).c_str(), item.isOverride, Tip("チェックを入れると、このカメラの値を使う。外すと全体の設定のまま"));
	ui.SameLineAfterCheck();
	ui.BeginDisabled(!item.isOverride);
	ui.Field(std::format("{}###{}", name, key).c_str(), item.value, style);
	ui.EndDisabled();
}
} // namespace

//=============================================================================
// Inspector と保存
//=============================================================================
void CameraVolume::Describe(ComponentUI& ui, CameraVolumeComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと、このカメラも全体の設定のまま"));
	ui.Field("Weight###weight", value.weight, Range(0.0f, 1.0f).Tip("全部の項目の効き具合。0：全体の設定のまま 〜 1：このカメラの値"));
	ui.Label("チェックを入れた項目だけ、このカメラの値を使う。切り替えると一緒に補間する");
	// ===== 被写界深度 =====
	ui.Separator("Depth Of Field");
	ui.Field(
	    "Focus###focus", value.focus,
	    Tip("ピントの合わせ方。Global：全体の設定のまま / Distance：カメラから Focus Distance / LookAtTarget：Camera Look At が見ている点 / FollowTarget：このカメラの Camera Follow の Target"));
	ui.BeginDisabled(value.focus != CameraVolumeFocus::Distance);
	ui.Field("Focus Distance###focusDistance", value.focusDistance, AtLeast(0.0f).Tip("カメラからピントの合う所までの奥行き（m）"));
	ui.EndDisabled();
	DescribeItem(ui, "Focus Range", "focusRange", value.focusRange, AtLeast(0.0f).Tip("ピントの前後この奥行き（m）までは、ぼけない"));
	DescribeItem(ui, "Far Transition", "farTransition", value.farTransition, AtLeast(0.01f).Tip("ピントの範囲の奥から、一番ぼけるまでの奥行き（m）"));
	DescribeItem(ui, "Near Transition", "nearTransition", value.nearTransition, AtLeast(0.0f).Tip("ピントの範囲の手前から、一番ぼけるまでの奥行き（m）。0 なら手前はぼかさない"));
	DescribeItem(
	    ui, "Max Blur", "maxBlur", value.maxBlur,
	    Range(0.0f, DepthOfFieldPass::kMaxBlurLimit).Tip("一番ぼけた所の、ぼけの半径（ピクセル）。0 ならぼかさない。Depth Of Field Component が無いシーンでも、ここを上書きすればぼける"));
	// ===== ブルーム =====
	ui.Separator("Bloom");
	DescribeItem(ui, "Threshold", "bloomThreshold", value.bloomThreshold, AtLeast(0.0f).Tip("これより明るい所だけ光る"));
	DescribeItem(ui, "Knee", "bloomKnee", value.bloomKnee, AtLeast(0.0f).Tip("しきい値付近の柔らかさ"));
	DescribeItem(ui, "Intensity", "bloomIntensity", value.bloomIntensity, AtLeast(0.0f).Tip("合成の強さ。0 なら光らない。全体の設定で切ってあっても、ここを上書きすれば掛かる"));
	// ===== レンズ =====
	ui.Separator("Lens");
	DescribeItem(ui, "Distortion", "lensDistortion", value.lensDistortion, Tip("画面端の歪み。0.2 で気づく程度。全体の設定で切ってあっても、どれかを上書きすれば掛かる"));
	DescribeItem(ui, "Aberration", "lensAberration", value.lensAberration, Tip("色収差。0.02 で十分"));
	DescribeItem(ui, "Radial Blur", "lensRadialBlur", value.lensRadialBlur, AtLeast(0.0f).Tip("中心へ向かって流れるブラー。0.10 で強め"));
	DescribeItem(ui, "Vignette", "lensVignette", value.lensVignette, AtLeast(0.0f).Tip("端の暗さ"));
	// ===== 露出 =====
	ui.Separator("ToneMap");
	DescribeItem(ui, "Exposure", "exposure", value.exposure, AtLeast(0.0f).Tip("露出。2 で1段明るい・0.5 で1段暗い"));
	// ===== 色の調整 =====
	ui.Separator("Color Adjust");
	DescribeItem(ui, "Saturation", "saturation", value.saturation, AtLeast(0.0f).Tip("彩度。1 でそのまま・0 で白黒（同じ明るさの灰色）・1 より大きいと色が濃くなる。UI には掛からない"));
}

//=============================================================================
// 上書きを作る
//=============================================================================
PostOverrides CameraVolume::MakeOverrides(const CameraVolumeComponent& volume, std::optional<float> focusDistance) {
	PostOverrides overrides;
	if (!volume.enabled) {
		return overrides;
	}
	const float weight = std::clamp(volume.weight, 0.0f, 1.0f);
	if (focusDistance.has_value()) {
		overrides[PostParam::FocusDistance] = {*focusDistance * weight, weight};
	}
	const auto setItem = [&overrides, weight](PostParam param, const CameraVolumeValue& item) {
		if (item.isOverride) {
			overrides[param] = {item.value * weight, weight};
		}
	};
	setItem(PostParam::FocusRange, volume.focusRange);
	setItem(PostParam::FarTransition, volume.farTransition);
	setItem(PostParam::NearTransition, volume.nearTransition);
	setItem(PostParam::MaxBlur, volume.maxBlur);
	setItem(PostParam::BloomThreshold, volume.bloomThreshold);
	setItem(PostParam::BloomKnee, volume.bloomKnee);
	setItem(PostParam::BloomIntensity, volume.bloomIntensity);
	setItem(PostParam::LensDistortion, volume.lensDistortion);
	setItem(PostParam::LensAberration, volume.lensAberration);
	setItem(PostParam::LensRadialBlur, volume.lensRadialBlur);
	setItem(PostParam::LensVignette, volume.lensVignette);
	setItem(PostParam::Exposure, volume.exposure);
	setItem(PostParam::Saturation, volume.saturation);
	return overrides;
}