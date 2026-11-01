#ifdef USE_IMGUI
#include "TimelineAssetEditor.h"

#include <cstdint>
#include <format>
#include <memory>

#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/Editor/Inspector/AssetInspector.h"
#include "MyEngine/Editor/Timeline/TimelineWindow.h"
#include "MyEngine/Timeline/TimelineManager.h"

namespace {
constexpr std::string_view kTypeName = "Timeline"; // Inspector の見出しに出す名前

/// <summary>
/// .timeline のファイルを Inspector に出す係
/// </summary>
class TimelineFileEditor final : public AssetEditor {
public:
	std::string_view GetExtension() const override { return TimelineManager::kFileExtension; }
	std::string_view GetTypeName() const override { return kTypeName; }
	bool CanLoad(const std::filesystem::path& filePath) override { return TimelineManager::Load(filePath) != 0; }

	bool Draw(const std::filesystem::path& filePath, ComponentUI& ui) override {
		const Timeline* timeline = TimelineManager::Get(TimelineManager::Load(filePath));
		if (timeline == nullptr) {
			return false;
		}
		ui.Label(std::format("トラック：{} 個　長さ：{:.2f} 秒", timeline->tracks.size(), timeline->GetDuration()).c_str());
		if (ui.Button("Open Timeline")) {
			TimelineWindow::Open(filePath);
		}
		ui.Label("Project でダブルクリックしても開ける");
		return false; // ここでは中身を変えない
	}

	void Save(const std::filesystem::path&) override {} // 保存は Timeline ウィンドウ
};
} // namespace

//=============================================================================
// 初期化
//=============================================================================
void TimelineAssetEditor::Initialize() { AssetInspector::Register(std::make_unique<TimelineFileEditor>()); }
#endif