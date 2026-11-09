#include "UIScreenEditor.h"

#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "HTTEngine/Core/AssetMoveRegistry.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/Editor/History/EditorHistory.h"
#include "HTTEngine/Editor/Inspector/AssetInspector.h"
#include "HTTEngine/Editor/Viewport/SceneVisibility.h"
#include "HTTEngine/Editor/Windows/HierarchyWindow.h"
#include "HTTEngine/Graphics/Renderer/GameScreen.h"
#include "HTTEngine/Scene/Prefab/PrefabManager.h"
#include "HTTEngine/Scene/SceneManager.h"
#include "HTTEngine/String/ConvertString.h"
#include "HTTEngine/UI/UIScreen.h"

namespace {
constexpr ImU32 kFrameColor = IM_COL32(90, 200, 255, 255); // 画面の枠（選んだ物の橙と分けて、水色）
constexpr ImU32 kGuideColor = IM_COL32(90, 200, 255, 70);  // 真ん中の線（うすく）
constexpr ImU32 kLabelBackColor = IM_COL32(0, 0, 0, 160);  // 名前の後ろ（シーンが明るくても読めるように）
constexpr float kFrameThickness = 2.0f;                    // 枠の太さ（画面のピクセル）

/// <summary>
/// 編集の状態（1つの画面だけ編集できる）
/// </summary>
struct EditState {
	bool isEditing = false;
	std::filesystem::path filePath;    // 編集中の画面のファイル
	Handle<Entity> root;               // 編集中の画面の一番上の Entity
	std::filesystem::path openRequest; // 次のフレームの頭で開く画面（空なら頼まれていない）
	std::optional<bool> closeRequest;  // 次のフレームの頭で閉じる（true：保存する / false：保存しない。無ければ頼まれていない）
};
EditState edit;

// 画面の名前（ファイルの名前から拡張子を外した物。"resources/ui/Pause.uiscreen" → "Pause"）
std::string NameOf(const std::filesystem::path& filePath) { return ToUtf8(filePath.stem()); }

// 入る（フレームの頭で）。中身を今のシーンに置いて、選べる物・Hierarchy・履歴を画面だけにする
void Begin(const std::filesystem::path& filePath) {
	const SceneManager* main = SceneManager::GetMain();
	if (main == nullptr || main->GetPlayState() != PlayState::Editing) {
		LogManager::Warning("画面の編集は、停止中だけできます（Stop してから開いてください）");
		return;
	}
	// 同じファイルは1回だけ読む（保存し直すと、PrefabManager::Save が中身を差し替える）
	const uint32_t prefab = PrefabManager::Load(filePath);
	if (prefab == 0) {
		return; // 読めなかった（PrefabManager がログに出している）
	}
	EditorHistory::Suspend();                                           // シーンの履歴はよけておく（Ctrl+Z でシーンの操作を戻さないように）
	edit.root = PrefabManager::Instantiate(prefab, {0.0f, 0.0f, 0.0f}); // Component は次のフレームの頭に付く
	EntityManager::Get(edit.root)->isDontSave = true;                   // 子も一緒に、シーンに保存しない
	edit.filePath = filePath;
	edit.isEditing = true;
	SceneVisibility::SetPickRoot(edit.root);
	HierarchyWindow::SetSelected(edit.root);
	LogManager::Log(std::format("画面の編集を始めました: {}", ToUtf8(filePath)));
}

/// <summary>
/// .uiscreen のファイルを Inspector に出す係（中身は Edit UI Screen で直す。ここでは開くボタンだけ）
/// </summary>
class UIScreenAssetEditor final : public AssetEditor {
public:
	std::string_view GetExtension() const override { return kUIScreenFileExtension; }
	std::string_view GetTypeName() const override { return "UI Screen"; }
	bool Draw(const std::filesystem::path& filePath, ComponentUI& ui) override {
		ui.Label(std::format("ゲームからは UIScreen::Open(\"{}\") で出す・UIScreen::Close(\"{}\") で消す", NameOf(filePath), NameOf(filePath)).c_str());
		if (ui.Button("Edit UI Screen")) {
			UIScreenEditor::RequestOpen(filePath);
		}
		ui.Label("Project ウィンドウでダブルクリックしても開ける（停止中だけ）");
		return false; // ここでは中身を変えない
	}
	void Save(const std::filesystem::path&) override {} // 保存は Done（UIScreenEditor）
};
} // namespace

//=============================================================================
// 初期化
//=============================================================================
void UIScreenEditor::Initialize() {
	AssetInspector::Register(std::make_unique<UIScreenAssetEditor>());
	// 編集中・開く予約の画面の名前を変えたら、新しいパスに保存する（Done で前の名前のファイルを作り直さないように）
	AssetMoveRegistry::AddListener([](const MovedPath& moved) {
		moved.Apply(edit.filePath);
		moved.Apply(edit.openRequest);
	});
}

//=============================================================================
// 入る・出る
//=============================================================================
void UIScreenEditor::RequestOpen(const std::filesystem::path& filePath) { edit.openRequest = filePath; }

void UIScreenEditor::RequestClose(bool save) { edit.closeRequest = save; }

void UIScreenEditor::Flush() {
	// --- Hierarchy で一番上を消した・Undo で消えた：閉じる（中身がもう無いので、保存はしない）---
	if (edit.isEditing && !EntityManager::IsAlive(edit.root)) {
		LogManager::Warning("編集中の画面の一番上の Entity が無くなったので、保存せずに閉じました");
		EndNow(false);
	}
	// --- 閉じる（Done・Cancel）---
	if (edit.closeRequest.has_value()) {
		EndNow(*std::exchange(edit.closeRequest, std::nullopt));
	}
	// --- 開く（ほかの画面を編集中なら、それを保存して閉じてから）---
	if (!edit.openRequest.empty()) {
		const std::filesystem::path filePath = std::exchange(edit.openRequest, {});
		if (edit.isEditing && filePath == edit.filePath) {
			return; // もう開いている
		}
		EndNow(true);
		Begin(filePath);
	}
}

void UIScreenEditor::EndNow(bool save) {
	if (!edit.isEditing) {
		return;
	}
	if (save) {
		Save();
	}
	// 中身を今すぐ消す（フレームの境目なので、予約をその場で反映してよい）
	if (EntityManager::IsAlive(edit.root)) {
		EntityManager::Destroy(edit.root); // 子も一緒に消える
		EntityManager::FlushDestroy();
	}
	EditorHistory::Resume(); // シーンの履歴に戻す（画面の編集の履歴は捨てる。消えた Entity を指しているので）
	SceneVisibility::SetPickRoot({});
	HierarchyWindow::SetSelected({});
	LogManager::Log(std::format("画面の編集を終わりました（{}）: {}", save ? "保存した" : "保存していない", ToUtf8(edit.filePath)));
	edit.isEditing = false;
	edit.root = {};
	edit.filePath.clear();
}

void UIScreenEditor::Save() {
	if (!edit.isEditing || !EntityManager::IsAlive(edit.root)) {
		return;
	}
	// 中身は Prefab と同じ形。読み込み済みなら中身も差し替わるので、次の UIScreen::Open から新しい中身になる
	PrefabManager::Save(edit.root, ToUtf8(edit.filePath));
}

//=============================================================================
// 調べる
//=============================================================================
bool UIScreenEditor::IsEditing() { return edit.isEditing; }

Handle<Entity> UIScreenEditor::GetRoot() { return edit.isEditing ? edit.root : Handle<Entity>{}; }

//=============================================================================
// 描く
//=============================================================================
void UIScreenEditor::DrawBar() {
	if (!edit.isEditing) {
		return;
	}
	ImGui::AlignTextToFramePadding();
	ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kFrameColor), "UI Screen: %s", NameOf(edit.filePath).c_str());
	// --- 右端に Done・Cancel ---
	const ImGuiStyle& style = ImGui::GetStyle();
	const float doneWidth = ImGui::CalcTextSize("Done").x + style.FramePadding.x * 2.0f;
	const float cancelWidth = ImGui::CalcTextSize("Cancel").x + style.FramePadding.x * 2.0f;
	const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x; // この行の右端（ウィンドウの中での位置）
	ImGui::SameLine(right - doneWidth - cancelWidth - style.ItemSpacing.x);
	if (ImGui::Button("Done")) {
		RequestClose(true);
	}
	ImGui::SetItemTooltip("画面のファイルに保存して、編集を終わる");
	ImGui::SameLine();
	if (ImGui::Button("Cancel")) {
		RequestClose(false);
	}
	ImGui::SetItemTooltip("保存せずに、編集を終わる（開いてから直した所は消える）");
	// 説明は灰色で、ウィンドウの幅で折り返す（TextDisabled は折り返さないので、色を変えて TextWrapped）
	ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	ImGui::TextWrapped("Sceneビューの絵の縁がゲームの画面");
	ImGui::PopStyleColor();
}

void UIScreenEditor::DrawFrame(const ImVec2& imageMin, const ImVec2& imageSize) {
	if (!edit.isEditing) {
		return;
	}
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImVec2 imageMax(imageMin.x + imageSize.x, imageMin.y + imageSize.y);
	const ImVec2 center(imageMin.x + imageSize.x * 0.5f, imageMin.y + imageSize.y * 0.5f);
	// --- 真ん中の十字（左右・上下の真ん中にそろえるときの目安）---
	drawList->AddLine(ImVec2(center.x, imageMin.y), ImVec2(center.x, imageMax.y), kGuideColor);
	drawList->AddLine(ImVec2(imageMin.x, center.y), ImVec2(imageMax.x, center.y), kGuideColor);
	// --- 枠：UI はゲームの画面を絵の全体に合わせて描くので、絵の縁＝画面の縁（線の太さの半分だけ内側に描く）---
	const float half = kFrameThickness * 0.5f;
	drawList->AddRect(ImVec2(imageMin.x + half, imageMin.y + half), ImVec2(imageMax.x - half, imageMax.y - half), kFrameColor, 0.0f, 0, kFrameThickness);
	// --- 左上に名前 ---
	const std::string label = std::format("UI Screen: {}  ({:.0f}×{:.0f})", NameOf(edit.filePath), GameScreen::GetWidth(), GameScreen::GetHeight());
	const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
	const ImVec2 textMin(imageMin.x + 8.0f, imageMin.y + 8.0f);
	drawList->AddRectFilled(ImVec2(textMin.x - 4.0f, textMin.y - 2.0f), ImVec2(textMin.x + textSize.x + 4.0f, textMin.y + textSize.y + 2.0f), kLabelBackColor, 3.0f);
	drawList->AddText(textMin, kFrameColor, label.c_str());
}