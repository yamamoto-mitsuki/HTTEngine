#include "Source/Scripts/ListFieldSample.h"

#include <format>

#include "MyEngine/Diagnostics/LogManager.h"

SCRIPT(ListFieldSample, "Test");

namespace {
// ===== 要素1つの見せ方（Script とデータの Component で同じ物を使う）=====
void DescribeStage(ComponentUI& ui, DistanceStage& stage) {
	ui.Field("Max Distance###maxDistance", stage.maxDistance, AtLeast(0.0f).Tip("この段階になる、相手までの距離の上限（m）"));
	ui.Field("Damage Scale###damageScale", stage.damageScale, AtLeast(0.0f).Drag(0.01f).Tip("この段階のダメージの倍率"));
}

// ===== 見出しの文字（折りたたんだままでも中身が分かるように）=====
std::string MakeStageTitle(const DistanceStage& stage) { return std::format("〜{:.1f}m  ×{:.2f}", stage.maxDistance, stage.damageScale); }
} // namespace

// データの Component（FixedList）。見せ方は Script と同じ ui.ListField
COMPONENT(ListFieldSampleData, "Test") { ui.ListField("Stages###stages", value.stages, &DescribeStage, &MakeStageTitle, Tip("距離の段階（4つまで）")); }


//=============================================================================
// 毎フレーム
//=============================================================================
void ListFieldSample::Update() {
	const int stage = FindStage(targetDistance_);
	if (stage == lastStage_) {
		return;
	}
	lastStage_ = stage;
	const float scale = (stage >= 0) ? stages_[static_cast<size_t>(stage)].damageScale : 0.0f;
	LogManager::Log(std::format("ListFieldSample: {:.1f}m は段階 {}（倍率 {:.2f}）", targetDistance_, stage, scale));
}

int ListFieldSample::FindStage(float distance) const {
	for (size_t i = 0; i < stages_.size(); ++i) {
		if (distance <= stages_[i].maxDistance) {
			return static_cast<int>(i);
		}
	}
	return -1;
}


//=============================================================================
// Inspector と保存
//=============================================================================
void ListFieldSample::Fields(ComponentUI& ui) {
	ui.ListField("Stages###stages", stages_, &DescribeStage, &MakeStageTitle, Tip("距離の段階（近い順に並べる）"));
	// 要素の中に一覧がある（要素の見せ方の中で、もう一度 ui.ListField を呼ぶ）
	ui.ListField(
	    "Waves###waves", waves_,
	    [](ComponentUI& ui, SpawnWave& wave) {
		    ui.Field("Delay###delay", wave.delay, AtLeast(0.0f).Tip("前の組から何秒後に出すか"));
		    ui.ListField("Points###points", wave.points, [](ComponentUI& ui, Vector3& point) { ui.Field("Position###position", point, Tip("出す場所（ワールドの位置）")); }, Tip("出す場所"));
	    },
	    [](const SpawnWave& wave) { return std::format("{:.1f} 秒後・{} か所", wave.delay, wave.points.size()); }, Tip("出す場所の組"));
	ui.ListField("Targets###targets", targets_, [](ComponentUI& ui, GameObject& target) { ui.Field("Target###target", target, Tip("相手（Hierarchy からドラッグで入れる）")); }, Tip("相手の一覧"));
	ui.Field("Target Distance###targetDistance", targetDistance_, AtLeast(0.0f).Tip("段階を調べる距離（m。再生中に変えると、ログの段階が変わる）"));
}
