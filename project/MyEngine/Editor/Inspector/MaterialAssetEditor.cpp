#ifdef USE_IMGUI
#include "MaterialAssetEditor.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include <externals/imgui/imgui.h>

#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/Editor/History/EditorHistory.h"
#include "MyEngine/Editor/Inspector/AssetInspector.h"
#include "MyEngine/Graphics/Material/MaterialManager.h"

namespace {
constexpr std::string_view kTypeName = "Material"; // Inspector の見出しに出す名前

/// <summary>
/// .material のファイルを Inspector で直す係
/// </summary>
class MaterialFileEditor final : public AssetEditor {
public:
	std::string_view GetExtension() const override { return MaterialManager::kFileExtension; }
	std::string_view GetTypeName() const override { return kTypeName; }
	bool CanLoad(const std::filesystem::path& filePath) override { return MaterialManager::Load(filePath) != 0; }

	bool Draw(const std::filesystem::path& filePath, ComponentUI& ui) override {
		const uint32_t handle = MaterialManager::Load(filePath);
		const Material* current = MaterialManager::Get(handle);
		if (current == nullptr) {
			return false;
		}
		// 別のファイルへ移ったら、直している途中の記録を捨てる（前のファイルの分は、移る前に Undo に積んである）
		if (handle != handle_) {
			handle_ = handle;
			before_.reset();
		}
		// --- 写しを直して、変わっていたら書き戻す（実行中なら、次に描くときからすぐ効く）---
		Material material = *current;
		MaterialManager::Describe(ui, material);
		ui.Space();
		ui.Label("このファイルを選んだ物みんなに効く（つまむ・打つのが終わると保存。Undo で戻せる）");
		const bool isChanged = !(material == *MaterialManager::Get(handle));
		if (isChanged) {
			if (!before_) {
				before_ = *MaterialManager::Get(handle); // 操作を始める前の中身（つまんでいる間は、最初の1回だけ）
			}
			MaterialManager::Set(handle, material);
		}
		// --- 操作が終わったら Undo に積む（値はもう変わっているので Record。ファイルも一緒に戻す）---
		// どの項目も操作していなくなった（つまむ・打つのが終わった）ときが、1回の操作の終わり
		if (before_ && !ImGui::IsAnyItemActive()) {
			const auto apply = [handle](const Material& value) {
				MaterialManager::Set(handle, value);
				return MaterialManager::Save(handle);
			};
			EditorHistory::Record([apply, before = std::move(*before_)] { return apply(before); }, [apply, after = *MaterialManager::Get(handle)] { return apply(after); });
			before_.reset();
		}
		return isChanged;
	}

	void Save(const std::filesystem::path& filePath) override { MaterialManager::Save(MaterialManager::Load(filePath)); }

private:
	uint32_t handle_ = 0;            // 今直しているファイル
	std::optional<Material> before_; // 操作を始める前の中身（操作していないときは空）
};
} // namespace

//=============================================================================
// 初期化
//=============================================================================
void MaterialAssetEditor::Initialize() { AssetInspector::Register(std::make_unique<MaterialFileEditor>()); }
#endif
