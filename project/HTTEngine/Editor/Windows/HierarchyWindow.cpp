#include "HierarchyWindow.h"

#include <algorithm>
#include <cfloat>
#include <string>

#include <externals/imgui/imgui.h>

#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Editor/Assets/AssetIcons.h"
#include "HTTEngine/Editor/History/EditorHistory.h"
#include "HTTEngine/Editor/Widgets/EditorWidgets.h"
#include "HTTEngine/Editor/Windows/UIScreenEditor.h"
#include "HTTEngine/Editor/Viewport/SceneVisibility.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/Components/Rendering3D/ArrowStripComponent.h"
#include "HTTEngine/Components/Camera/CameraComponent.h"
#include "HTTEngine/Components/Physics/ColliderComponent.h"
#include "HTTEngine/Components/Core/FolderComponent.h"
#include "HTTEngine/Components/UI/ImageComponent.h"
#include "HTTEngine/Components/Lighting/LightComponent.h"
#include "HTTEngine/Systems/Effects/ParticleSystem.h"
#include "HTTEngine/Components/Rendering3D/PolygonRendererComponent.h"
#include "HTTEngine/Components/Rendering3D/PolygonShapeComponent.h"
#include "HTTEngine/Components/UI/PolylineComponent.h"
#include "HTTEngine/Components/UI/TextComponent.h"
#include "HTTEngine/Components/UI/VectorShapeComponent.h"
#include "HTTEngine/Components/Rendering2D/SpriteRendererComponent.h"
#include "HTTEngine/Components/Physics/WindZoneComponent.h"
#include "HTTEngine/Components/Gameplay/TriggerActivatorComponent.h"
#include "HTTEngine/Scene/Prefab/PrefabManager.h"
#include "HTTEngine/String/FileNameRule.h"

// 静的メンバ変数
Handle<Entity> HierarchyWindow::selected_;
Handle<Entity> HierarchyWindow::renaming_;
std::string HierarchyWindow::renameBuffer_;
bool HierarchyWindow::renameFocusRequest_ = false;
bool HierarchyWindow::revealRequest_ = false;
float HierarchyWindow::eyeColumnX_ = 0.0f;

namespace {
// Prefab から置いた物の名前の色（Unity と同じく青。一番上は濃く、中身は薄く）
const ImVec4 kPrefabRootColor = ImVec4(0.45f, 0.70f, 1.0f, 1.0f);
const ImVec4 kPrefabContentColor = ImVec4(0.65f, 0.80f, 0.95f, 1.0f);

// 一番上の親（ふだんは無効なHandle＝root。画面（UI Screen）の編集中は、画面の一番上の Entity。作る・貼る・ドロップする物を、この外へ出さない）
Handle<Entity> TopParent() { return UIScreenEditor::GetRoot(); }

// 画面の編集中の、画面の一番上の Entity か（消す・複製すると、画面の外にできてしまうので、させない）
bool IsUIScreenRoot(Handle<Entity> handle) { return UIScreenEditor::IsEditing() && handle == UIScreenEditor::GetRoot(); }

// 親のHandle（いなければ一番上の親）。画面の一番上の Entity なら、それ自身（兄弟に貼ると画面の外になるので、子に貼る）
Handle<Entity> ParentOf(Handle<Entity> handle) {
	if (IsUIScreenRoot(handle)) {
		return handle;
	}
	const Entity* entity = EntityManager::Get(handle);
	return (entity != nullptr && entity->parent.IsValid()) ? entity->parent : TopParent();
}

// 子がいるか（いなければ矢印を出さない）
bool HasChild(Handle<Entity> handle) {
	for (const Entity& entity : EntityManager::GetAll()) {
		if (entity.parent == handle) {
			return true;
		}
	}
	return false;
}

// フォルダか（フォルダの印 FolderComponent が付いているか。無効な Handle なら false）
bool IsFolder(Handle<Entity> handle) { return EntityManager::Get<FolderComponent>(handle) != nullptr; }

// ドロップされたEntityを受け取る。newParentの子にする（無効ならrootへ）
void AcceptEntityDrop(Handle<Entity> newParent) {
	if (!ImGui::BeginDragDropTarget()) {
		return;
	}
	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(HierarchyWindow::kDragDropType)) {
		const Handle<Entity> dropped = *static_cast<const Handle<Entity>*>(payload->Data);
		// フォルダは、一番上か、ほかのフォルダの中にだけ入れる
		// 普通の Entity の子にすると、見た目を保つために、フォルダの Transform が原点からずれる（中身が親と一緒に動くようになる）
		if (IsFolder(dropped) && newParent.IsValid() && !IsFolder(newParent) && newParent != TopParent()) {
			LogManager::Warning("フォルダは、一番上か、ほかのフォルダの中にだけ入れられます");
		} else {
			EditorHistory::RequestReparent(dropped, newParent);
		}
	}
	ImGui::EndDragDropTarget();
}

// フォルダの名前を、フォルダの絵の後ろに描く（Project ウィンドウの左の木と同じ描き方。ツリーの矢印の右に置く）
// 開いていて中身があれば、開いたフォルダの絵にする
void DrawFolderLabel(const std::string& name, bool isOpen) {
	ImGui::SameLine();
	const float iconSide = ImGui::GetTextLineHeight();
	const ImVec2 iconMin = ImGui::GetCursorScreenPos();
	AssetIcons::Draw(ImGui::GetWindowDrawList(), isOpen ? AssetIcon::FolderOpen : AssetIcon::Folder, iconMin, ImVec2(iconMin.x + iconSide, iconMin.y + iconSide));
	ImGui::Dummy(ImVec2(iconSide, iconSide));
	ImGui::SameLine();
	ImGui::TextUnformatted(name.c_str());
}

// 目の絵（見せている：まぶたの線と黒目 / 隠している：まぶたの線と、斜めの線）。min は絵の左上、side は絵の1辺
void DrawEyeIcon(ImDrawList* drawList, const ImVec2& min, float side, bool isOpen, ImU32 color) {
	const ImVec2 center = {min.x + side * 0.5f, min.y + side * 0.5f};
	const float halfWidth = side * 0.42f; // 目の横の長さの半分
	// まぶたのふくらみ：2次ベジェ曲線（AddBezierQuadratic）は、真ん中で制御点までの高さの半分だけふくらむので、ふくらみの2倍の所に制御点を置く
	const float bulge = side * 0.3f;
	const float thickness = (std::max)(1.0f, side * 0.08f);
	const ImVec2 left = {center.x - halfWidth, center.y};
	const ImVec2 right = {center.x + halfWidth, center.y};
	// --- 上と下のまぶた（どちらでも描く）---
	drawList->AddBezierQuadratic(left, ImVec2(center.x, center.y - bulge * 2.0f), right, color, thickness);
	drawList->AddBezierQuadratic(left, ImVec2(center.x, center.y + bulge * 2.0f), right, color, thickness);
	if (isOpen) {
		drawList->AddCircleFilled(center, side * 0.14f, color); // 黒目
	} else {
		// 斜めの線（左上から右下へ。隠している印）
		drawList->AddLine(ImVec2(min.x + side * 0.12f, min.y + side * 0.12f), ImVec2(min.x + side * 0.88f, min.y + side * 0.88f), color, thickness);
	}
}
} // namespace


//=============================================================================
// 描画
//=============================================================================
void HierarchyWindow::Draw() {
	// Undoなどで消えたEntityを持ち越さない
	if (!EntityManager::IsAlive(selected_)) {
		selected_ = {};
	}
	if (!EntityManager::IsAlive(renaming_)) {
		renaming_ = {};
		renameFocusRequest_ = false;
	}

	if (ImGui::Begin("Hierarchy")) {
		DrawToolbar();
		UIScreenEditor::DrawBar(); // 画面（UI Screen）の編集中だけ、名前と Done・Cancel
		ImGui::Separator();

		// --- 一覧（rootから入れ子で描く）---
		// 作成・削除などはEditorHistoryに予約するだけなので、描いている途中で一覧が変わることはない
		// 左の端に目の列を空けて、ツリーをその分だけ右へずらす（Indent）
		const float eyeColumnWidth = ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.x;
		eyeColumnX_ = ImGui::GetCursorPosX();
		ImGui::Indent(eyeColumnWidth);
		if (UIScreenEditor::IsEditing()) {
			DrawEntityNode(UIScreenEditor::GetRoot()); // 画面の編集中は、画面の中身だけ（シーンの物は出さない）
		} else {
			for (const Entity& entity : EntityManager::GetAll()) {
				if (!entity.parent.IsValid()) {
					DrawEntityNode(entity.self);
				}
			}
		}
		ImGui::Unindent(eyeColumnWidth);
		revealRequest_ = false; // 開いて送るのは1回だけ（後はふつうに閉じられるように）
		DrawEmptySpace();
		HandleShortcuts();
	}
	ImGui::End();
}

//=============================================================================
// 見出し
//=============================================================================
void HierarchyWindow::DrawToolbar() {
	const float buttonSize = ImGui::GetFrameHeight();
	const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x; // この行の右端（ウィンドウの中での位置）

	ImGui::AlignTextToFramePadding(); // 文字の高さをボタンに合わせる
	ImGui::Text("Entities (%zu)", EntityManager::GetCount());

	// --- 右端に＋ボタン。押すと作成メニュー ---
	ImGui::SameLine(right - buttonSize);
	if (EditorWidgets::PlusButton("##create", ImVec2(buttonSize, buttonSize))) {
		ImGui::OpenPopup("createMenu");
	}
	ImGui::SetItemTooltip("Create");
	if (ImGui::BeginPopup("createMenu")) {
		DrawCreateMenu();
		ImGui::EndPopup();
	}
}

//=============================================================================
// Entity1つ分
//=============================================================================
void HierarchyWindow::DrawEntityNode(Handle<Entity> handle) {
	const Entity* entity = EntityManager::Get(handle);
	if (!entity) {
		return;
	}

	const bool isRenaming = (handle == renaming_);
	const bool isFolder = IsFolder(handle);
	const bool hasChild = HasChild(handle);
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
	if (!isRenaming) {
		flags |= ImGuiTreeNodeFlags_SpanAvailWidth; // 名前の変更中は、矢印の横に入力欄を置くので横幅いっぱいにしない
	}
	if (!hasChild) {
		flags |= ImGuiTreeNodeFlags_Leaf;
	}
	if (handle == selected_) {
		flags |= ImGuiTreeNodeFlags_Selected;
	}

	// EntityIdをIDにする（名前が同じEntityがあってもぶつからない）
	// Handleの番号だと、Play / Stop で作り直したときに振り直されて、開いた・閉じた状態が別のEntityに移ってしまう
	// 番号は 64 ビットなので、int にすると上の半分が消えてぶつかることがある。文字にして全部使う
	ImGui::PushID(std::to_string(entity->id).c_str());
	// 目（SetNextItemOpen より先に描く。後に描くと、開く指示を目のボタンが受け取ってしまう）
	DrawVisibilityToggle(handle);
	// Scene ビューで子を選んだとき、閉じている親を開いて見えるようにする
	if (revealRequest_ && EntityManager::IsDescendantOf(selected_, handle)) {
		ImGui::SetNextItemOpen(true);
	}
	// Prefab から置いた物は、名前を青で描く（どこまでが Prefab の中身か＝シーンで直しても保存されない所か、分かるように）
	const Handle<Entity> prefabRoot = PrefabManager::FindInstanceRoot(handle);
	if (prefabRoot.IsValid()) {
		ImGui::PushStyleColor(ImGuiCol_Text, (prefabRoot == handle) ? kPrefabRootColor : kPrefabContentColor);
	}
	// フォルダの名前は、フォルダの絵の後ろに自分で描く（DrawFolderLabel）ので、ツリーには名前を出さない
	const bool isOpen = ImGui::TreeNodeEx("##node", flags, "%s", (isRenaming || isFolder) ? "" : entity->name.c_str());
	if (prefabRoot.IsValid()) {
		ImGui::PopStyleColor();
	}
	if (revealRequest_ && handle == selected_) {
		ImGui::SetScrollHereY(); // 一覧が長くても、選んだ物まで送る
	}

	if (isRenaming) {
		ImGui::SameLine();
		DrawRenameField(handle);
	} else {
		// --- クリックで選択（右クリックでも選ぶ）、ダブルクリックで名前の変更 ---
		// 左クリックは「離したとき、ドラッグしていなければ」選ぶ。押した瞬間に選ぶと、Inspectorの参照欄へドラッグする前に
		// Inspectorの中身がドラッグしているEntityに切り替わってしまい、落とす先が消える
		const bool clickedLeft = ImGui::IsItemDeactivated() && ImGui::GetDragDropPayload() == nullptr;
		const bool clicked = clickedLeft || ImGui::IsItemClicked(ImGuiMouseButton_Right);
		if (clicked && !ImGui::IsItemToggledOpen()) {
			selected_ = handle;
		}
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen()) {
			StartRename(handle);
		}

		// --- ドラッグ（このEntityを運ぶ）・ドロップ（このEntityを親にする）---
		if (ImGui::BeginDragDropSource()) {
			ImGui::SetDragDropPayload(kDragDropType, &handle, sizeof(handle));
			ImGui::TextUnformatted(entity->name.c_str());
			ImGui::EndDragDropSource();
		}
		AcceptEntityDrop(handle);

		// --- 右クリックのメニュー ---
		if (ImGui::BeginPopupContextItem("entityMenu")) {
			DrawEntityMenu(handle);
			ImGui::EndPopup();
		}

		// --- フォルダの絵と名前（ツリーの行の操作を全部受けた後に描く。先に描くと、上の「今の行」が絵と文字の方になる）---
		if (isFolder) {
			DrawFolderLabel(entity->name, isOpen && hasChild);
		}
	}

	// --- 子を描く ---
	if (isOpen) {
		for (const Entity& other : EntityManager::GetAll()) {
			if (other.parent == handle) {
				DrawEntityNode(other.self);
			}
		}
		ImGui::TreePop();
	}
	ImGui::PopID();
}

//=============================================================================
// 目（Scene ビューだけで隠す）
//=============================================================================
void HierarchyWindow::DrawVisibilityToggle(Handle<Entity> handle) {
	const float rowX = ImGui::GetCursorPosX(); // ツリーの行を描き始める所（親子の深さの分だけ右にある）
	const float side = ImGui::GetTextLineHeight();
	ImGui::SetCursorPosX(eyeColumnX_); // 深さに関係なく、一覧の左の端の列に並べる
	const ImVec2 iconMin = ImGui::GetCursorScreenPos();
	if (ImGui::InvisibleButton("##visibility", ImVec2(side, side))) {
		SceneVisibility::Toggle(handle);
	}
	ImGui::SetItemTooltip("Scene ビューだけで隠す（Game ビューの絵・ゲームの動きは変わらない。保存しない）");
	// --- 絵：隠していれば閉じた目。親を隠しているので隠れている物も閉じた目（自分で隠した物だけ濃く。マウスを乗せても濃く）---
	const bool isHiddenSelf = SceneVisibility::IsHiddenSelf(handle);
	const ImU32 color = ImGui::GetColorU32((isHiddenSelf || ImGui::IsItemHovered()) ? ImGuiCol_Text : ImGuiCol_TextDisabled);
	DrawEyeIcon(ImGui::GetWindowDrawList(), iconMin, side, !SceneVisibility::IsHidden(handle), color);
	// --- ツリーの行を、元の場所から描けるようにする ---
	ImGui::SameLine();
	ImGui::SetCursorPosX(rowX);
}

//=============================================================================
// 一覧の下の何も無い所
//=============================================================================
void HierarchyWindow::DrawEmptySpace() {
	// 残りの高さいっぱいに透明なボタンを置いて、クリック・ドロップ・右クリックを受ける
	const ImVec2 space = ImGui::GetContentRegionAvail();
	ImGui::InvisibleButton("##emptySpace", ImVec2((std::max)(space.x, 1.0f), (std::max)(space.y, ImGui::GetFrameHeight())));
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
		selected_ = {}; // 何も無い所をクリックしたら選択を外す
	}
	AcceptEntityDrop(TopParent()); // ここへ落としたら一番上へ移す（画面の編集中は、画面の一番上の子へ）
	if (ImGui::BeginPopupContextItem("emptySpaceMenu")) {
		DrawCreateMenu();
		ImGui::EndPopup();
	}
}

//=============================================================================
// メニュー
//=============================================================================
void HierarchyWindow::DrawEntityMenu(Handle<Entity> handle) {
	if (ImGui::MenuItem("Rename", "F2")) {
		StartRename(handle);
	}
	ImGui::Separator();
	if (ImGui::MenuItem("Copy", "Ctrl+C")) {
		EditorHistory::Copy(handle);
	}
	// Paste：このEntityと同じ親の下（兄弟）に貼る　Paste As Child：このEntityの子として貼る
	if (ImGui::MenuItem("Paste", "Ctrl+V", false, EditorHistory::CanPaste())) {
		EditorHistory::RequestPaste(ParentOf(handle));
	}
	if (ImGui::MenuItem("Paste As Child", nullptr, false, EditorHistory::CanPaste())) {
		EditorHistory::RequestPaste(handle);
	}
	if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, !IsUIScreenRoot(handle))) {
		EditorHistory::RequestDuplicate(handle);
	}
	ImGui::Separator();
	// --- Prefab にする（resources/prefabs/<名前>.prefab。前の中身は .bak に残る）---
	// ファイルの名前になるので、名前が英数字と _ - だけのときに押せる
	// 同じ名前の別の Prefab がもうあるときは押せない（上書きすると、それを置いたシーンがみな変わる。チームでほかの人の Prefab を書き換えないように）
	const Entity* entity = EntityManager::Get(handle);
	const bool isValidName = (entity != nullptr) && IsValidFileName(entity->name);
	const bool isOtherPrefab = isValidName && PrefabManager::IsOtherPrefabFile(handle, MakePrefabFilePath(entity->name));
	if (ImGui::MenuItem("Save As Prefab", nullptr, false, isValidName && !isOtherPrefab)) {
		PrefabManager::Save(handle, MakePrefabFilePath(entity->name));
	}
	if (!isValidName) {
		ImGui::SetItemTooltip("名前を英数字と _ - だけにすると保存できる（ファイルの名前になるので）");
	} else if (isOtherPrefab) {
		ImGui::SetItemTooltip("同じ名前の Prefab がもうある（上書きすると、それを置いたシーンがみな変わる）。その Prefab を直すなら、置いて Apply。別の Prefab にするなら名前を変える");
	} else {
		ImGui::SetItemTooltip("子も一緒に resources/prefabs/ に保存する。これからは Prefab から置いた物になる");
	}
	// --- Prefab から置いた物の一番上：元の Prefab に書く・Prefab のとおりに戻す・つながりを切る（Inspector のボタンと同じ）---
	if (PrefabManager::FindInstanceRoot(handle) == handle) {
		if (ImGui::MenuItem("Apply to Prefab")) {
			PrefabManager::Apply(handle);
		}
		ImGui::SetItemTooltip("上書きを全部、元の Prefab のファイルに書く（ほかのシーンの物も、次に開いたときから同じになる）");
		// 戻せるのは一番外側の置いた物だけ（入れ子の中は、外側の Revert で戻す）
		if (ImGui::MenuItem("Revert Prefab", nullptr, false, !PrefabManager::FindOwner(handle).IsValid())) {
			EditorHistory::RequestRevertPrefab(handle);
		}
		ImGui::SetItemTooltip("上書きを全部捨てて、Prefab のとおりに戻す（あとから足した子も消える。Ctrl+Z で戻せる）");
		if (ImGui::MenuItem("Unpack Prefab")) {
			PrefabManager::Unpack(handle);
		}
		ImGui::SetItemTooltip("Prefab とのつながりを切る（中身もシーンファイルに書かれるようになる。Prefab を直しても変わらなくなる）");
	}
	ImGui::Separator();
	if (ImGui::MenuItem("Create Child")) {
		EditorHistory::RequestCreate("Child", handle);
	}
	if (IsFolder(handle) && ImGui::MenuItem("Create Folder")) {
		EditorHistory::RequestCreate("Folder", handle, {ComponentSnapshot::Make(FolderComponent{})});
	}
	if (ImGui::MenuItem("Delete", "Del", false, !IsUIScreenRoot(handle))) {
		EditorHistory::RequestDestroy(handle);
	}
}

void HierarchyWindow::DrawCreateMenu() {
	// 作る物は一番上の親の下（ふだんは root。画面の編集中は、画面の一番上の子）
	const Handle<Entity> top = TopParent();
	if (ImGui::MenuItem("Create Entity")) {
		EditorHistory::RequestCreate("Entity", top);
	}
	if (ImGui::MenuItem("Create Child", nullptr, false, selected_.IsValid())) {
		EditorHistory::RequestCreate("Child", selected_);
	}
	// --- フォルダ：Hierarchy を整理する入れ物（一番上に作る。中へはドラッグで入れる）---
	if (ImGui::MenuItem("Folder")) {
		EditorHistory::RequestCreate("Folder", top, {ComponentSnapshot::Make(FolderComponent{})});
	}
	// --- ライト：Componentを付けた状態で作る（1回のUndoでEntityごと消える）---
	if (ImGui::BeginMenu("Light")) {
		if (ImGui::MenuItem("Directional Light")) {
			TransformComponent transform;
			transform.translation = {0.0f, 3.0f, 0.0f};       // 位置は照らし方に関係ない（ギズモを描く場所）
			SetEulerAngles(transform, {50.0f, -30.0f, 0.0f}); // 斜め上から照らす（Unityの最初のシーンと同じ位置・向き）
			EditorHistory::RequestCreate("Directional Light", top, {ComponentSnapshot::Make(transform), ComponentSnapshot::Make(DirectionalLightComponent{})});
		}
		if (ImGui::MenuItem("Point Light")) {
			EditorHistory::RequestCreate("Point Light", top, {ComponentSnapshot::Make(PointLightComponent{})});
		}
		if (ImGui::MenuItem("Spot Light")) {
			TransformComponent transform;
			SetEulerAngles(transform, {90.0f, 0.0f, 0.0f}); // 真下を照らす（前＝+Zを、X軸で90度倒す）
			EditorHistory::RequestCreate("Spot Light", top, {ComponentSnapshot::Make(transform), ComponentSnapshot::Make(SpotLightComponent{})});
		}
		ImGui::EndMenu();
	}
	// --- 3D の形：形・見た目・当たりを付けた状態で作る ---
	if (ImGui::BeginMenu("3D Object")) {
		if (ImGui::MenuItem("Polygon")) {
			// 横から見た形（初めは横 4m・高さ 1m の四角）を奥へ押し出した足場。Inspector の Edit Shape で点を動かす
			ColliderComponent collider;
			collider.shape = ColliderShape::Polygon;
			EditorHistory::RequestCreate("Polygon", top, {ComponentSnapshot::Make(PolygonShapeComponent{}), ComponentSnapshot::Make(PolygonRendererComponent{}), ComponentSnapshot::Make(collider)});
		}
		if (ImGui::MenuItem("Arrow Strip")) {
			// 「> > > >」と流れる矢印の列（向きは Rotation の Z。Inspector で数・間・大きさ・速さを変えると、停止中もすぐ見られる）
			EditorHistory::RequestCreate("Arrow Strip", top, {ComponentSnapshot::Make(ArrowStripComponent{})});
		}
		if (ImGui::MenuItem("Wind Zone")) {
			// 風の吹く範囲（箱。風は右へ吹く。Rotation の Z で向きを変える）。範囲と向きは Collider のギズモと一緒に出る
			EditorHistory::RequestCreate("Wind Zone", top, {ComponentSnapshot::Make(WindZoneComponent{})});
		}
		if (ImGui::MenuItem("Trigger Activator")) {
			// 入ったら・出たら Target を出す・消す範囲（Is Trigger の箱。範囲は Collider のギズモで見える）。Target は Inspector でドラッグして入れる
			ColliderComponent collider;
			collider.isTrigger = true;
			collider.size = {4.0f, 4.0f, 4.0f};
			EditorHistory::RequestCreate("Trigger Activator", top, {ComponentSnapshot::Make(collider), ComponentSnapshot::Make(TriggerActivatorComponent{})});
		}
		ImGui::EndMenu();
	}
	// --- 効果：パーティクル（初めの値のまま、停止中は Inspector の Preview で流して見る）---
	if (ImGui::BeginMenu("Effects")) {
		if (ImGui::MenuItem("Particle System")) {
			EditorHistory::RequestCreate("Particle System", top, {ComponentSnapshot::Make(ParticleSystemComponent{})});
		}
		ImGui::EndMenu();
	}
	// --- カメラ：Camera Component を付けた状態で作る（ゲームの絵はこのカメラから撮る）---
	if (ImGui::MenuItem("Camera")) {
		TransformComponent transform;
		transform.translation = {0.0f, 0.0f, -10.0f}; // 原点を正面（-Z側）から見る
		EditorHistory::RequestCreate("Main Camera", top, {ComponentSnapshot::Make(transform), ComponentSnapshot::Make(CameraComponent{})});
	}
	// --- 画像：3D空間に置く板（Sprite Renderer）・画面に重ねる絵（Image）・図形（Vector Shape）・折れ線（Polyline）・文字（Text）を付けた状態で作る ---
	if (ImGui::MenuItem("Sprite")) {
		EditorHistory::RequestCreate("Sprite", top, {ComponentSnapshot::Make(SpriteRendererComponent{})});
	}
	if (ImGui::MenuItem("Image")) {
		EditorHistory::RequestCreate("Image", top, {ComponentSnapshot::Make(ImageComponent{})});
	}
	if (ImGui::MenuItem("Vector Shape")) {
		EditorHistory::RequestCreate("Vector Shape", top, {ComponentSnapshot::Make(VectorShapeComponent{})});
	}
	if (ImGui::MenuItem("Polyline")) {
		EditorHistory::RequestCreate("Polyline", top, {ComponentSnapshot::Make(PolylineComponent{})});
	}
	if (ImGui::MenuItem("Text")) {
		EditorHistory::RequestCreate("Text", top, {ComponentSnapshot::Make(TextComponent{})});
	}
	ImGui::Separator();
	// 何も無い所から貼るので、一番上（root）に貼る
	if (ImGui::MenuItem("Paste", nullptr, false, EditorHistory::CanPaste())) {
		EditorHistory::RequestPaste(top);
	}
}

//=============================================================================
// ショートカット（Hierarchyを触っているときだけ）
//=============================================================================
void HierarchyWindow::HandleShortcuts() {
	// 名前や数値を入力中なら、入力欄のほうの操作を優先する
	if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || renaming_.IsValid() || ImGui::GetIO().WantTextInput) {
		return;
	}
	if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
		StartRename(selected_); // 選んでいなければ何もしない
	}
	if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && selected_.IsValid() && !IsUIScreenRoot(selected_)) {
		EditorHistory::RequestDestroy(selected_);
	}
	// IsKeyChordPressed：修飾キーまで完全に一致したときだけ（Ctrl+Shift+Cでは反応しない）
	if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C) && selected_.IsValid()) {
		EditorHistory::Copy(selected_);
	}
	if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V)) {
		EditorHistory::RequestPaste(ParentOf(selected_)); // 選んでいるEntityの兄弟に貼る（選んでいなければroot）
	}
	if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D) && selected_.IsValid() && !IsUIScreenRoot(selected_)) {
		EditorHistory::RequestDuplicate(selected_);
	}
}

//=============================================================================
// 名前の変更を始める
//=============================================================================
void HierarchyWindow::StartRename(Handle<Entity> handle) {
	const Entity* entity = EntityManager::Get(handle);
	if (!entity) {
		return;
	}
	renaming_ = handle;
	renameBuffer_ = entity->name; // 今の名前から編集を始める
	renameFocusRequest_ = true;
	selected_ = handle;
}

//=============================================================================
// 名前の入力欄
//=============================================================================
void HierarchyWindow::DrawRenameField(Handle<Entity> handle) {
	// 出した最初のフレームだけ、入力欄にフォーカスを移す（すぐに文字を打てるように）
	const bool justStarted = renameFocusRequest_;
	if (renameFocusRequest_) {
		ImGui::SetKeyboardFocusHere();
		renameFocusRequest_ = false;
	}

	ImGui::SetNextItemWidth(-FLT_MIN);
	// EnterReturnsTrue：Enterで確定　AutoSelectAll：最初は全選択（そのまま打つと置き換わる）
	const bool entered = EditorWidgets::InputText("##rename", renameBuffer_, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);

	// --- 確定：Enter、または欄の外をクリックして抜けた（書き換えていた場合）---
	// Escで抜けたときはImGuiが中身を元に戻すので、名前は変わらない＝取り消しになる
	if (entered || ImGui::IsItemDeactivatedAfterEdit()) {
		EditorHistory::RequestRename(handle, renameBuffer_); // 空・同じ名前なら何もしない
	}

	// --- 終了：Enter・Esc・欄の外をクリック、のどれでも入力欄を閉じる ---
	bool finished = entered || ImGui::IsItemDeactivated();
	// 入力欄が有効にならないまま別の所をクリックされたときも閉じる（出しっぱなしにしない）
	if (!justStarted && !ImGui::IsItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		finished = true;
	}
	if (finished) {
		renaming_ = {};
	}
}