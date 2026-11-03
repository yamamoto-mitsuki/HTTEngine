#include "InspectorWindow.h"

#include <algorithm>
#include <cfloat>
#include <cstddef>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <Windows.h>
#include <shellapi.h>

#include <externals/imgui/imgui.h>
#include <externals/magic_enum/magic_enum.hpp>

#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/Editor/Inspector/AssetInspector.h"
#include "MyEngine/Editor/History/EditorHistory.h"
#include "MyEngine/Editor/Inspector/ComponentEditor.h"
#include "MyEngine/Editor/Inspector/LightEditor.h"
#include "MyEngine/Editor/Inspector/ModelRendererEditor.h"
#include "MyEngine/Editor/Inspector/TransformEditor.h"
#include "MyEngine/Editor/Widgets/EditorWidgets.h"
#include "MyEngine/Editor/Windows/CodeWindow.h"
#include "MyEngine/Components/Core/FolderComponent.h"
#include "MyEngine/Components/Core/PrefabInstanceComponent.h"
#include "MyEngine/Editor/Windows/HierarchyWindow.h"
#include "MyEngine/Scene/PrefabManager.h"
#include "MyEngine/String/ConvertString.h"

namespace {
const ImVec4 kPrefabWarningColor = ImVec4(1.0f, 0.55f, 0.4f, 1.0f); // Prefab と違う・読めないときの文字の色

/// <summary>
/// 置いた物が Prefab と違うか、の覚え（調べるたびに中身を全部 JSON に書くので、毎フレームはしない。履歴が動いたか、選ぶ物が変わったときだけ調べ直す）
/// </summary>
struct PrefabModifiedCache {
	EntityId rootId = 0;      // 調べた置いた物
	uint64_t changeCount = 0; // 調べたときの EditorHistory::GetChangeCount
	bool isValid = false;     // false なら、次は必ず調べ直す（Apply した直後など）
	bool isModified = false;  // 調べた結果
};
PrefabModifiedCache prefabModifiedCache;

constexpr float kAddComponentWidth = 560.0f;     // Add Componentのポップアップの幅（左にカテゴリ、右にその中身を並べる）
constexpr float kAddComponentMaxHeight = 420.0f; // Add Componentのポップアップの高さの上限（超えたら、左右それぞれでスクロール）
constexpr float kCategoryColumnWidth = 160.0f;   // 左のカテゴリの列の幅

// Add Componentに出すか（外せる種類で、検索にも合う）
bool IsListed(const ComponentEditor& editor, const ImGuiTextFilter& filter) { return editor.IsOptional() && filter.PassFilter(editor.GetName()); }

// コンパイラが入れたファイルの場所（C:\...\Source\Components\Spin.cpp）を、今の作業フォルダ（ゲームのプロジェクトのフォルダ）からの相対パスに縮める
// 別のドライブなどで縮められなければ、そのまま返す
std::string ShortenSourcePath(const char* file) {
	std::error_code error; // 例外ではなくエラーコードで受ける
	const std::filesystem::path relative = std::filesystem::relative(file, error);
	return (error || relative.empty()) ? std::string(file) : relative.string();
}

// 書いてあるファイルを、.cpp を開くアプリ（Windows の「既定のアプリ」。ふつうは Visual Studio）で開く
// file はコンパイラが入れた UTF-8 の文字なので、Windows に渡す前にワイド文字にする
void OpenSourceFile(const char* file) { ShellExecuteW(nullptr, L"open", ConvertString(std::string(file)).c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

// カテゴリのパスの depth 段目の名前（"Gameplay/Movement" の1段目は "Movement"）。そこで終わっていれば空
std::string_view CategorySegment(std::string_view path, size_t depth) {
	size_t start = 0;
	for (size_t i = 0; i < depth; ++i) {
		const size_t slash = path.find('/', start);
		if (slash == std::string_view::npos) {
			return {}; // これより下の段は無い
		}
		start = slash + 1;
	}
	const size_t slash = path.find('/', start);
	return path.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
}

// Component と Component の境目（見出しの帯だけだと、どこまでが1つの Component か分かりにくいので、間を空けて線を引く）
void DrawComponentSeparator() {
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
}

// 置いた物が Prefab と違うか（覚えた結果が使えれば、それを返す）
bool IsPrefabModified(Handle<Entity> root) {
	const EntityId rootId = EntityManager::Get(root)->id;
	const uint64_t changeCount = EditorHistory::GetChangeCount();
	PrefabModifiedCache& cache = prefabModifiedCache;
	if (!cache.isValid || cache.rootId != rootId || cache.changeCount != changeCount) {
		cache = {rootId, changeCount, true, PrefabManager::IsModified(root)};
	}
	return cache.isModified;
}

// Prefab から置いた物なら、元の Prefab・違うか・ボタン（Apply・Unpack）を出す
void DrawPrefabInfo(Handle<Entity> handle) {
	const Handle<Entity> root = PrefabManager::FindInstanceRoot(handle);
	const PrefabInstanceComponent* link = EntityManager::Get<PrefabInstanceComponent>(root);
	if (link == nullptr) {
		return;
	}
	const std::string path = link->path.ToString();
	ImGui::TextDisabled("Prefab: %s%s", path.c_str(), (root == handle) ? "" : "（の中身）");
	if (link->prefab == 0) {
		ImGui::TextColored(kPrefabWarningColor, "Prefab のファイルが読めません（届いたら、シーンを開き直す）");
		return;
	}
	// --- Prefab と違うか（シーンに書くのは参照だけなので、違う所は保存されない）---
	if (IsPrefabModified(root)) {
		ImGui::TextColored(kPrefabWarningColor, "Prefab と違う所があります。このままシーンを保存すると、Prefab のとおりに戻ります");
	} else {
		ImGui::TextDisabled("Prefab と同じ（直したいときは Prefab を直すか、ここで直して Apply）");
	}
	// --- ボタン ---
	if (root != handle) {
		if (ImGui::SmallButton("Select Root")) {
			HierarchyWindow::SetSelected(root);
		}
		ImGui::SameLine();
	}
	if (ImGui::SmallButton("Apply")) {
		PrefabManager::Apply(root);
		prefabModifiedCache.isValid = false; // ファイルが変わったので調べ直す
	}
	ImGui::SetItemTooltip("シーンで直した中身を、元の Prefab のファイルに書く（ほかのシーンの物も、次に開いたときから同じになる）");
	ImGui::SameLine();
	if (ImGui::SmallButton("Unpack")) {
		PrefabManager::Unpack(root);
	}
	ImGui::SetItemTooltip("Prefab とのつながりを切る（中身もシーンファイルに書かれるようになる。Prefab を直しても変わらなくなる）");
}

// Componentを1つ、押せる項目として出す
void DrawAddComponentItem(Handle<Entity> handle, const ComponentEditor& editor) {
	// すでに持っている・追加を予約済みなら、灰色にして押せなくする
	ImGui::BeginDisabled(editor.Get(handle) || editor.IsAddPending(handle));
	if (ImGui::Selectable(editor.GetName())) {
		EditorHistory::RequestAddComponent(handle, editor);
		// 一覧はスクロールするための子ウィンドウの中なので、Selectable を押してもポップアップは自動では閉じない。ここで閉じる
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndDisabled();
	// Script は右の端に灰色で「Script」と出す（データの Component と同じ所に並ぶので、見分けられるように）
	if (editor.IsScript()) {
		const char* tag = "Script";
		const ImVec2 rectMin = ImGui::GetItemRectMin();
		const ImVec2 rectMax = ImGui::GetItemRectMax();
		const float tagWidth = ImGui::CalcTextSize(tag).x;
		ImGui::GetWindowDrawList()->AddText(ImVec2(rectMax.x - tagWidth, rectMin.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), tag);
	}
	// ゲームの物は、書いてあるファイルを出す（押せない物の上でも出す）
	if (const char* file = editor.GetSourceFile(); file != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
		ImGui::SetTooltip("%s", ShortenSourcePath(file).c_str());
	}
}

// [begin, end) は同じ上位カテゴリの並び。depth段目の名前で切って、入れ子のメニューにする
void DrawAddComponentLevel(Handle<Entity> handle, const std::vector<const ComponentEditor*>& items, size_t begin, size_t end, size_t depth, const ImGuiTextFilter& filter) {
	size_t index = begin;

	// パスがこの段で終わっている物（"Gameplay" 直下など）を先に並べる。並びの決まりで、これらは必ず先頭に集まっている
	while (index < end && CategorySegment(items[index]->GetCategory(), depth).empty()) {
		DrawAddComponentItem(handle, *items[index]);
		++index;
	}

	// 残りを、同じ名前のかたまりごとに入れ子にする
	while (index < end) {
		const std::string name(CategorySegment(items[index]->GetCategory(), depth));
		size_t groupEnd = index;
		while (groupEnd < end && CategorySegment(items[groupEnd]->GetCategory(), depth) == name) {
			++groupEnd;
		}
		// 検索中は、見つかったカテゴリを全部開いて見せる
		if (filter.IsActive()) {
			ImGui::SetNextItemOpen(true);
		}
		if (ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth)) {
			DrawAddComponentLevel(handle, items, index, groupEnd, depth + 1, filter);
			ImGui::TreePop();
		}
		index = groupEnd;
	}
}

/// <summary>
/// 上の段のカテゴリ1つ分（"Gameplay" など）。items の [begin, end) がそのカテゴリの物
/// </summary>
struct CategoryRange {
	std::string name;
	size_t begin = 0;
	size_t end = 0;
};

// 並べた items を、上の段のカテゴリごとの範囲に分ける（登録表はカテゴリ順なので、同じカテゴリの物は必ず続いて並んでいる）
std::vector<CategoryRange> SplitByTopCategory(const std::vector<const ComponentEditor*>& items) {
	std::vector<CategoryRange> ranges;
	for (size_t i = 0; i < items.size(); ++i) {
		const std::string_view name = CategorySegment(items[i]->GetCategory(), 0);
		if (ranges.empty() || ranges.back().name != name) {
			ranges.push_back({std::string(name), i, i});
		}
		ranges.back().end = i + 1;
	}
	return ranges;
}

// Add Component のポップアップの場所と大きさを決める（直前に描いたボタンの下に入れば下、入らなければ上）
void PlaceAddComponentPopup() {
	const ImVec2 buttonMin = ImGui::GetItemRectMin();
	const ImVec2 buttonMax = ImGui::GetItemRectMax();
	const ImGuiViewport* viewport = ImGui::GetWindowViewport(); // エディタ全体の画面（メニューバーの下から）
	const float margin = ImGui::GetStyle().WindowPadding.y;     // 画面の端から少し離す
	const float screenLeft = viewport->WorkPos.x + margin;
	const float screenRight = viewport->WorkPos.x + viewport->WorkSize.x - margin;
	// 幅：決めた幅とボタンの幅の大きい方。画面に入らなければ画面の幅まで
	const float width = (std::min)((std::max)(kAddComponentWidth, buttonMax.x - buttonMin.x), screenRight - screenLeft);
	// 左右：ボタンの左にそろえる。右にはみ出すなら左へずらす（Inspector は画面の右端に置くことが多いので）
	const float x = (std::max)(screenLeft, (std::min)(buttonMin.x, screenRight - width));
	// 上下：下に全部入る、または下の方が広いなら下
	const float spaceBelow = viewport->WorkPos.y + viewport->WorkSize.y - buttonMax.y - margin;
	const float spaceAbove = buttonMin.y - viewport->WorkPos.y - margin;
	const bool opensBelow = (spaceBelow >= kAddComponentMaxHeight) || (spaceBelow >= spaceAbove);
	const float height = (std::min)(kAddComponentMaxHeight, opensBelow ? spaceBelow : spaceAbove);
	if (opensBelow) {
		ImGui::SetNextWindowPos(ImVec2(x, buttonMax.y)); // 左上をボタンの下に
	} else {
		ImGui::SetNextWindowPos(ImVec2(x, buttonMin.y), ImGuiCond_Always, ImVec2(0.0f, 1.0f)); // 左下をボタンの上に
	}
	ImGui::SetNextWindowSize(ImVec2(width, height));
	// 背景を不透明にする（スタイルの PopupBg は少し透けるので、下の Inspector の文字が一覧に重なって読みにくい）
	ImGui::SetNextWindowBgAlpha(1.0f);
}

// 左の列：カテゴリ（押すと右にその中身）
void DrawCategoryColumn(const std::vector<CategoryRange>& categories, ImGuiTextFilter& filter, std::string& selectedCategory) {
	if (ImGui::BeginChild("##categories", ImVec2(kCategoryColumnWidth, 0.0f), ImGuiChildFlags_Borders)) {
		for (const CategoryRange& category : categories) {
			// 名前の右に数（検索中は見つかった数）
			const std::string label = std::format("{} ({})", category.name, category.end - category.begin);
			const bool isSelected = !filter.IsActive() && category.name == selectedCategory;
			if (ImGui::Selectable(label.c_str(), isSelected)) {
				// 検索中に押したら、検索をやめてそのカテゴリを見せる
				selectedCategory = category.name;
				filter.Clear();
			}
		}
	}
	ImGui::EndChild();
}

// 右の列：中身（サブカテゴリがあれば、押すと下に開く入れ子）
void DrawComponentColumn(Handle<Entity> handle, const std::vector<const ComponentEditor*>& items, const std::vector<CategoryRange>& categories, std::vector<CategoryRange>::const_iterator selected, ImGuiTextFilter& filter) {
	if (ImGui::BeginChild("##components", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
		if (categories.empty()) {
			ImGui::TextDisabled("見つかりません");
		} else if (filter.IsActive()) {
			// 検索中は、見つかった物を全部（カテゴリごとに見出しを付けて）
			for (const CategoryRange& category : categories) {
				ImGui::SeparatorText(category.name.c_str());
				DrawAddComponentLevel(handle, items, category.begin, category.end, 1, filter);
			}
		} else {
			DrawAddComponentLevel(handle, items, selected->begin, selected->end, 1, filter);
		}
	}
	ImGui::EndChild();
}

// ポップアップの中身（検索欄と、左右の列）
void DrawAddComponentPopup(Handle<Entity> handle) {
	// ポップアップは同時に1つしか開かないので、関数の中のstaticで持つ
	static ImGuiTextFilter filter;       // 検索欄の文字
	static std::string selectedCategory; // 左で選んでいるカテゴリ（閉じても覚えておき、次に開いたときも同じ所から）

	// --- 検索欄（開いた瞬間に文字を打てるようにする。Enter で一番上の物を足す）---
	if (ImGui::IsWindowAppearing()) {
		filter.Clear();
		ImGui::SetKeyboardFocusHere();
	}
	ImGui::SetNextItemWidth(-FLT_MIN);
	// EnterReturnsTrue：Enter を押したときだけ true（打った文字は毎フレーム Build で検索に使う）
	const bool isEntered = ImGui::InputTextWithHint("##search", "検索（Enter で一番上を追加）", filter.InputBuf, IM_ARRAYSIZE(filter.InputBuf), ImGuiInputTextFlags_EnterReturnsTrue);
	filter.Build();

	// --- 出す物（外せる種類で、検索に合う物）---
	std::vector<const ComponentEditor*> items;
	for (const std::unique_ptr<ComponentEditor>& editor : ComponentEditorRegistry::GetAll()) {
		if (IsListed(*editor, filter)) {
			items.push_back(editor.get());
		}
	}

	// --- Enter：検索で見つかった物のうち、まだ付いていない一番上の物を足す ---
	if (isEntered && filter.IsActive()) {
		const auto first = std::find_if(items.begin(), items.end(), [handle](const ComponentEditor* editor) { return !editor->Get(handle) && !editor->IsAddPending(handle); });
		if (first != items.end()) {
			EditorHistory::RequestAddComponent(handle, **first);
			ImGui::CloseCurrentPopup();
			return;
		}
	}

	// --- 上の段のカテゴリごとに分ける。選んでいるカテゴリが無ければ（初めて開いた・検索で消えた）、一番上にする ---
	const std::vector<CategoryRange> categories = SplitByTopCategory(items);
	auto selected = std::find_if(categories.begin(), categories.end(), [](const CategoryRange& category) { return category.name == selectedCategory; });
	if (selected == categories.end() && !categories.empty()) {
		selected = categories.begin();
		selectedCategory = selected->name;
	}

	DrawCategoryColumn(categories, filter, selectedCategory);
	ImGui::SameLine();
	DrawComponentColumn(handle, items, categories, selected, filter);
}
} // namespace

//=============================================================================
// 初期化
//=============================================================================
void InspectorWindow::Initialize() {
	// エンジンのComponent。Inspectorの区画はカテゴリ順（同じカテゴリの中は登録した順）に並ぶ
	ComponentEditorRegistry::Register(std::make_unique<TransformEditor>());
	ComponentEditorRegistry::Register(std::make_unique<ModelRendererEditor>());
	ComponentEditorRegistry::Register(std::make_unique<DirectionalLightEditor>());
	ComponentEditorRegistry::Register(std::make_unique<PointLightEditor>());
	ComponentEditorRegistry::Register(std::make_unique<SpotLightEditor>());
}

//=============================================================================
// 描画
//=============================================================================
void InspectorWindow::Draw() {
	if (ImGui::Begin("Inspector")) {
		const Handle<Entity> handle = HierarchyWindow::GetSelected();
		if (EntityManager::IsAlive(handle)) {
			DrawHeader(handle);
			if (EntityManager::Get<FolderComponent>(handle) != nullptr) {
				// --- フォルダ：Transform も Component も出さない（中身の位置が変わらないように、Transform は原点のまま使う）---
				ImGui::TextWrapped("フォルダ（Hierarchy を整理する入れ物）。中へは Hierarchy でドラッグして入れる。左上のチェックを外すと、中身ごと無効になる");
			} else {
				for (const std::unique_ptr<ComponentEditor>& editor : ComponentEditorRegistry::GetAll()) {
					DrawComponent(handle, *editor);
				}
				DrawAddComponent(handle);
			}
		} else if (AssetInspector::HasSelection()) {
			AssetInspector::Draw(); // Project ウィンドウで選んだファイル（Physic Material など）
		} else {
			ImGui::TextDisabled("No entity selected");
		}
	}
	ImGui::End();

	// どの項目も操作していない（マウスを離した・入力を確定した）なら、ここまでの編集を1件の履歴にまとめる
	if (!ImGui::IsAnyItemActive()) {
		EditorHistory::CommitComponentChanges();
	}
}

//=============================================================================
// Entityそのものの情報
//=============================================================================
void InspectorWindow::DrawHeader(Handle<Entity> handle) {
	// ポインタは使い捨てにする（ARCHITECTURE.md 原則1）。持ち越すのはHandleだけ
	const Entity* entity = EntityManager::Get(handle);
	if (!entity) {
		return;
	}

	// --- 有効・無効（直接書き換えず、履歴を通す。反映は次のフレームの頭）---
	bool isActive = entity->isActive;
	if (ImGui::Checkbox("##isActive", &isActive)) {
		EditorHistory::RequestSetActive(handle, isActive);
	}
	ImGui::SameLine();

	// --- 名前（変更はHierarchyのダブルクリック・F2から）---
	ImGui::TextUnformatted(entity->name.c_str());

	// --- 参考情報（読み取り専用）---
	ImGui::TextDisabled("ID: %llu  Handle: %u / %u", entity->id, handle.index, handle.generation);
	if (const Entity* parent = EntityManager::Get(entity->parent)) {
		ImGui::TextDisabled("Parent: %s", parent->name.c_str());
	} else {
		ImGui::TextDisabled("Parent: (root)");
	}
	DrawPrefabInfo(handle);
	DrawComponentSeparator();
}

//=============================================================================
// Component1つ分の区画
//=============================================================================
void InspectorWindow::DrawComponent(Handle<Entity> handle, const ComponentEditor& editor) {
	void* component = editor.Get(handle);
	if (!component) {
		return; // このEntityは持っていない
	}

	// CollapsingHeaderはIDの範囲を作らないので、自分で作る（"Position" などの項目名がComponent同士でぶつからないように）
	ImGui::PushID(editor.GetName());
	// Script の見出しには、Unity と同じく「(Script)」を付ける（データの Component と同じカテゴリに並ぶので、見分けられるように）
	const std::string title = editor.IsScript() ? std::format("{} (Script)", editor.GetName()) : std::string(editor.GetName());
	const bool isOpen = ImGui::CollapsingHeader(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

	// --- 見出しにマウスを乗せると、書いてあるファイルを出す（ゲームのComponentはどこに置いてもよいので、探さなくて済むように）---
	if (const char* file = editor.GetSourceFile(); file != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
		ImGui::SetTooltip("%s", ShortenSourcePath(file).c_str());
	}

	// --- 見出しの右クリック ---
	if (ImGui::BeginPopupContextItem("componentMenu")) {
		if (ImGui::MenuItem("Remove Component", nullptr, false, editor.IsOptional())) {
			EditorHistory::RequestRemoveComponent(handle, editor);
		}
		// 書いてあるファイルを開く（エンジンの中の Code ウィンドウで読む・Visual Studio で直す）
		if (const char* file = editor.GetSourceFile(); file != nullptr) {
			ImGui::Separator();
			if (ImGui::MenuItem("Open in Code Window")) {
				CodeWindow::Open(std::filesystem::path(ConvertString(std::string(file))));
			}
			if (ImGui::MenuItem("Edit Script")) {
				OpenSourceFile(file);
			}
		}
		ImGui::EndPopup();
	}

	if (isOpen) {
		// 描く前の中身を写しておき、描いた後と比べる。変わった所だけがUndoの対象になる
		// （写し方は editor が決める。Componentごとの項目を1つずつ書き並べなくてよいので、Componentが増えてもここは変えない）
		const std::vector<std::byte> before = editor.Save(component);
		editor.Draw(component);
		EditorHistory::RecordComponentChange(handle, editor, before, editor.Save(component));
	}
	ImGui::PopID();

	// --- 次の Component との境目 ---
	DrawComponentSeparator();
}

//=============================================================================
// Componentを足すボタン
//=============================================================================
void InspectorWindow::DrawAddComponent(Handle<Entity> handle) {
	ImGui::Spacing();

	// Componentの見出し（CollapsingHeader）と同じ色の、横幅いっぱいのボタンにする
	ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Header));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_HeaderHovered));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
	const bool clicked = EditorWidgets::PlusButton("Add Component", ImVec2(-FLT_MIN, 0.0f));
	ImGui::PopStyleColor(3);
	if (clicked) {
		ImGui::OpenPopup("addComponent");
	}

	// --- 横に広いポップアップ（左にカテゴリ、右にその中身）を、ボタンの下に入れば下、入らなければ上に出す ---
	PlaceAddComponentPopup();
	if (ImGui::BeginPopup("addComponent")) {
		DrawAddComponentPopup(handle);
		ImGui::EndPopup();
	}
}
