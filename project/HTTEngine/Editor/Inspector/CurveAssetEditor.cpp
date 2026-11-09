#ifdef USE_IMGUI
#include "CurveAssetEditor.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "HTTEngine/Curve/CurveManager.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/Editor/History/EditorHistory.h"
#include "HTTEngine/Editor/Inspector/AssetInspector.h"
#include "HTTEngine/Editor/Widgets/CurveEditor.h"

namespace {
constexpr std::string_view kTypeName = "Curve"; // Inspector の見出しに出す名前
constexpr float kCanvasHeight = 280.0f;         // カーブの絵の高さ（ピクセル）

/// <summary>
/// .curve のファイルを Inspector で直す係
/// </summary>
class CurveFileEditor final : public AssetEditor {
public:
	std::string_view GetExtension() const override { return CurveManager::kFileExtension; }
	std::string_view GetTypeName() const override { return kTypeName; }
	bool CanLoad(const std::filesystem::path& filePath) override { return CurveManager::Load(filePath) != 0; }

	bool Draw(const std::filesystem::path& filePath, ComponentUI& ui) override {
		const uint32_t handle = CurveManager::Load(filePath);
		const Curve* current = CurveManager::Get(handle);
		if (current == nullptr) {
			return false;
		}
		// 別のファイルへ移ったら、見ている範囲と選択をやり直す
		if (handle != handle_) {
			handle_ = handle;
			editor_ = CurveEditor{};
		}
		// 写しを直して、変わっていたら書き戻す
		Curve curve = *current;
		const bool isChanged = editor_.Draw("curve", curve, kCanvasHeight, ui);
		if (isChanged) {
			CurveManager::Set(handle, curve);
		}
		// 操作が終わったら Undo に積む（値はもう変わっているので Record。ファイルも一緒に戻す）
		if (std::optional<CurveEdit> edit = editor_.TakeFinishedEdit()) {
			const auto apply = [handle](const Curve& value) {
				CurveManager::Set(handle, value);
				return CurveManager::Save(handle);
			};
			EditorHistory::Record([apply, before = std::move(edit->before)] { return apply(before); }, [apply, after = std::move(edit->after)] { return apply(after); });
		}
		ui.Label("ダブルクリック：キーを足す / Delete：消す / F：全体を見る");
		ui.Label("ホイール：拡大縮小（Ctrl：時間だけ、Shift：値だけ） / 中ボタン：移動");
		return isChanged;
	}

	void Save(const std::filesystem::path& filePath) override { CurveManager::Save(CurveManager::Load(filePath)); }

private:
	uint32_t handle_ = 0; // 今直しているファイル
	CurveEditor editor_;  // 絵と操作
};
} // namespace

//=============================================================================
// 初期化
//=============================================================================
void CurveAssetEditor::Initialize() { AssetInspector::Register(std::make_unique<CurveFileEditor>()); }
#endif