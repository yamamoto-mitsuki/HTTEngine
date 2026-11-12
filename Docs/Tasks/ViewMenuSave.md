参考：
- ImGui のメニュー（`BeginMenu` を入れ子にすると、カーソルを合わせた所から子メニューが横に出る。`ImGuiItemFlags_AutoClosePopups`＝押したらメニューを閉じるか）：https://github.com/ocornut/imgui/blob/master/imgui.h （「Widgets: Menus」と `ImGuiItemFlags_`）
- Unity の Scene ビューの Gizmos メニュー（見え方の切り替えを、ビューの上のメニューにまとめる形）：https://docs.unity3d.com/Manual/GizmosMenu.html
- WM_CLOSE（× と Alt+F4 で届く。ここで止めれば閉じない）：https://learn.microsoft.com/ja-jp/windows/win32/winmsg/wm-close
- 前の md：`Docs/Tasks/InputAction.md`（Settings → Editor → Keymap のページを `SettingsWindow::AddPage` で足した形。View のページも同じ形にした）

# View メニューの子メニュー・Save でエディタの設定も保存・× で閉じるときの確認

- **パートA ViewSettings（新しい）**（`Editor/Viewport/ViewSettings.h/.cpp`・vcxproj・`EditorViewport.h` のコメント）：View メニューの切り替えの一覧・保存（`Parameters/Editor/View.json`）・Settings → Editor → View のページ。見出し（Gizmos・Scene View・Game View）ごとの子メニューを描く
- **パートB ImGuiManager の View メニューを差し替える**（`ImGuiManager.cpp`）
- **パートC Save の入口を1つにする**（`SettingsWindow.h/.cpp`・`SceneManager.h`・`SceneDialogs.h/.cpp`・`ControlWindow.h/.cpp`・`EditorKeymap.cpp`）：Save ＝シーン＋Settings → Editor の全ページ＋ウィンドウの配置。閉じるときの「保存しますか？」もここ
- **パートD × で閉じるときにつなぐ**（`Win32Window.h/.cpp`・`ImGuiManager.cpp`）
- **パートE Reference**（`Docs/Reference/17-Editor.md`）
- **パートF 確かめ方**

**A → B → C → D → E の順**に写す（B は A の `ViewSettings`、D は C の `SceneDialogs::CanQuit`・`RequestQuit` を使う）。

ブランチは `feature/ViewMenuSave`（`develop` 14e183b から。この md も未コミットで置いてある）。

**写しで確かめた**（14e183b から作った写しで、答えのブランチ `answer-view`。Debug・Development・Release のビルドでエラー・警告 0。Development の Sandbox を、ImGui に入力を入れる一時的な道具で動かして撮った。くわしくは パートF の F-2）。本物のキーボードの Ctrl+S と、本物のマウスで Scene ビューのカメラを動かしてから Save する所は、まだ手で見ていない（F-3）。

**2026-11-12 本体へ直接入れた**（ブランチ `feature/ViewMenuSave`。未コミット）。写しの `14e183b..answer-view` の差分（パートA〜E）をそのまま当てた。入力を入れる一時的な道具は写しだけで、本体には入れていない。


## 0章 全体の形

### 0.1 頼まれたこと

| | 頼み | 今 |
|---|---|---|
| ① | Gizmos・Scene View・Game View の3つにカーソルを合わせたら、その中身が横から出る | View メニューを開くと、3つの見出し（`SeparatorText`）の下に全部の切り替えが縦に並ぶ |
| ② | Save で、エディタで使う物を全部保存する（DebugCamera の位置・View の設定） | Save（Ctrl+S）はシーンだけ。DebugCamera は Settings → Editor → Scene View の Save を押したときだけ。View の切り替えはどこにも保存されない（起動するたびに初めの値） |
| ③ | 保存していないまま × で閉じようとしたら「保存しますか？」 | 何も聞かずに閉じる（`Win32Window` に「閉じてよいか」を聞く仕組みはあるが、だれもつないでいない） |

### 0.2 ① 横から出る：子メニュー

ImGui の `BeginMenu` を `BeginMenu` の中で呼ぶと、カーソルを合わせた行の右に子メニューが出る（File → Open Scene と同じ）。

```
 View
 ├ Gizmos     ▶ ┌ Light Icons     ✓
 ├ Scene View ▶ │ Light Ranges    ✓
 └ Game View  ▶ │ …
                └ Skeletons       ✓
```

切り替えは続けて何個も押すことが多いので、**子メニューの中だけ「押してもメニューを閉じない」**にする（`ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false)`）。Unity の Gizmos メニューも、押しても閉じない。閉じるのは、外をクリックするか Esc。

### 0.3 ② 切り替えの持ち主と、保存する係

View の切り替えの値は、今もいろいろな所が持っている（使う所の近くに置いてある）。

| 切り替え | 値の持ち主 |
|---|---|
| Light Icons・Light Ranges | `LightGizmo::GetGlobalFlags()` |
| Camera Icons・Paths・Follow Zones・Orbits | `CameraGizmo::GetShow〇〇()` |
| Particle Shapes・Colliders・Hit Shapes・Skeletons | 各 System の `GetShowGizmos()` |
| Scene View の Post Effects〜Pick UI・Game View の Grid | `EditorViewport::GetViewFlags()` |

値の置き場所は変えない（描く所が読むので）。足りないのは「**一覧にして、メニュー・保存・Settings のページをまとめて受け持つ係**」なので、新しく `ViewSettings`（`Editor/Viewport/`）を作る。値の**アドレス**を覚えておき、メニューはそこを `MenuItem` に渡し、保存はそこを読む。

- 一覧は `ViewSettings::Initialize` の1か所だけ。切り替えを足すときは1行足せば、メニュー・保存・ページに全部出る（今までは `DrawViewMenu` に足して、保存は無かった）
- Keymap と同じく **Settings → Editor にページ（View）を足す**（`SettingsWindow::AddPage`）。`*`・Revert・Save がそのまま使える
- 保存先は `Parameters/Editor/View.json`（`Parameters/Editor/` は git に入れない＝人ごと。Keymap.json・LastScene.json と同じ所）

| 置かない所 | 理由 |
|---|---|
| GlobalVariables のグループにする | 値を GlobalVariables が別に持つことになり、持ち主（LightGizmo など）と毎フレーム写し合う必要が出る（Scene View の DebugCamera がそうなっている）。メニューの `MenuItem` は `bool*` を直接受け取りたい |
| 各持ち主が自分で保存する | 保存の形・ファイルが 7 か所に散らばる。Settings のページ・「保存していない物」の一覧が作れない |

### 0.4 ② Save の入口を1つにする

今、シーンの Save は4か所から `SceneManager::RequestSave()` を直接呼んでいる（Control の Save ボタン・Ctrl+S・File → Save・別のシーンを開く前の「保存しますか？」の Save）。ここに「エディタの設定も」を足すなら、**入口を1つ（`SceneDialogs::RequestSave()`）にして、4か所ともそこを通す**。中身は：

1. **エディタの設定**：Settings の Editor の見出しにある、保存していないページ全部（`SettingsWindow::SaveSection(SettingsSection::Editor)`）。今は Scene View（DebugCamera の位置・回転・FovY など）・Keymap・View。これからエディタのページを足しても、ここは直さなくてよい
2. **ウィンドウの配置**（`imgui.ini`。ImGui も数秒ごとに書くが、Save の時点の物を確実に残す）
3. **シーン**（今までどおり。停止中で、シーンファイルを使っているときだけ。書くのは次のフレームの頭）

`SceneDialogs` に置くのは、File メニューとシーンの「保存しますか？」をもともと受け持っているから（③の確認もここに足す）。`SceneManager`（エンジンの層。製品版にも入る）には、エディタの設定を知らせない。

| 決めたこと | 理由 |
|---|---|
| エディタの設定は **再生中でも保存できる**（シーンは今までどおり停止中だけ） | Scene ビューのカメラ・見え方は、再生中に変えても Stop で戻る物ではない。Save ボタンは「保存する物がある」ときに押せる（`SceneDialogs::CanSave`） |
| 変わっていないページは書かない | ファイルの日付を変えない（`SaveSection` は `*` の付いたページだけ） |
| Project の見出し（PostEffect・Layers など）は Save に入れない | ゲームのデータ（製品版にも入る）で、エディタの物ではない。今までどおりページの Save で |
| `EditorAction::SaveScene` の名前は変えない | Keymap.json に `File.SaveScene` の名前で書いてあるので、変えると人ごとの割り当てが外れる。説明（tooltip）だけ直す |

### 0.5 ③ × で閉じるとき

`Win32Window` の WM_CLOSE には、もとから「閉じてよいか（`onCanClose_`）」「だめだったときに呼ぶ（`onTryClose_`）」がある（public の変数のまま、だれも入れていない）。これを private にして `SetCloseGuard(canClose, onBlocked)` で渡す形にし、エディタ（`ImGuiManager::Initialize`。ImGui のウィンドウを受け取る所）からつなぐ。ゲーム（AL4_01・TD2_2）のコードはこの変数を使っていないので、直さなくてよい。

```
 ×（WM_CLOSE）
   └ canClose？ ─ はい → 閉じる
        └ いいえ → onBlocked → SceneDialogs::RequestQuit → 「保存しますか？」
              ├ Save       → RequestSave → （次のフレームの頭でシーンを書く）→ UpdateQuit が済んだのを見る
              │                 ├ 保存していない物が無くなった → ApproveQuit → RequestClose（WM_CLOSE をもう一度）→ 閉じる
              │                 └ まだ残っている（書けなかった）→ 閉じない（ログに Warning）
              ├ Don't Save → ApproveQuit → 閉じる
              └ Cancel     → 何もしない
```

- 「保存していない物」＝シーンの `*`（`SceneManager::IsDirty`）＋ Settings → Editor の `*`。確認には、何が保存されていないかを並べる（「シーン: SampleScene」「エディタの設定: Scene View・View」）。0.4 の Save で保存される物と同じ範囲にした（Save で消えない物を聞いても答えられないので）
- シーンを書くのは次のフレームの頭（`SceneManager::Update`）なので、Save を押したらすぐ閉じずに、**書き終わるのを待つ**（`SceneManager::IsSaveRequested` を足した）。書けなかったら閉じない
- 閉じるときに `DestroyWindow` をその場で呼ばない。描いている途中のフレームでウィンドウが消えないように、`PostMessage(WM_CLOSE)` で次のメッセージ処理に回す（`Win32Window::RequestClose`）
- 再生中にシーンの `*` があるときは Save を押せない（再生中はシーンを保存できない。理由を赤い字で出す）。Don't Save か Cancel

### 0.6 やらないこと・知っておくこと

- Scene ビューのカメラが動くと Scene View のページに `*` が付く（前から）。なので、カメラを動かしただけでも × で「保存しますか？」が出る（③の頼みどおり「Save で保存される物を保存していない」なので）。うるさく感じたら、「エディタの設定は閉じるときに黙って保存する」に変える手がある（その場合、③の確認はシーンだけになる）
- Save はシーンに変更が無くても書く（今までどおり。1つ前は `.bak`）
- Project の見出しのページ（PostEffect など）は、保存していなくても × で聞かない（0.4 の理由。前から）
- `imgui_style.ini`（Setting → Save Style）は Save に入れない（変わったかを調べる仕組みが無い）


## パートA ViewSettings（新しい）

### A-1 `Editor/Viewport/ViewSettings.h`（新しい）

```cpp
#pragma once
#ifdef USE_IMGUI
#include <cstdint>
#include <string>
#include <vector>

/// <summary>
/// View メニューの見出し（メニューにカーソルを合わせると、見出しごとの子メニューが横に出る）
/// </summary>
enum class ViewSection : uint8_t {
	Gizmos,    // ギズモ（ライト・カメラ・Collider など。どのビューに出るかは物ごとに決まっている）
	SceneView, // Scene ビューの絵
	GameView,  // Game ビューの絵
};

/// <summary>
/// エディタの見え方の切り替え（View メニューの中身）
/// <para>切り替えの値は持ち主（LightGizmo・CollisionSystem・EditorViewport など）が持ち、ここはその場所を覚えて、メニュー・保存・Settings のページをまとめて受け持つ</para>
/// <para>保存は Parameters/Editor/View.json（git に入れないので人ごと）。Settings → Editor → View で Save / Revert、File → Save（Ctrl+S）でも保存する</para>
/// <para>切り替えを足すときは、ViewSettings::Initialize の一覧に1行足す（メニュー・保存・Settings のページに全部出る）</para>
/// </summary>
class ViewSettings {
public:
	// 切り替えを登録し、保存した値を読み、Settings にページを足す（ImGuiManager::Initialize から1回）
	static void Initialize();
	// View メニュー（ImGui::BeginMenuBar の中から呼ぶ）。見出しにカーソルを合わせると、その切り替えが横に出る
	static void DrawMenu();

private:
	// 切り替え1つ
	struct Toggle {
		ViewSection section;            // どの見出しに出すか
		std::string name;               // メニューに出す名前（保存のキーにもなる。見出しの中でかぶらないように）
		bool* value = nullptr;          // 持ち主の値
		const bool* enabledBy = nullptr; // これが false の間は押せない（無ければいつも押せる）
		std::string tooltip;            // 説明（空なら出さない）
		bool saved = false;             // 保存した値（ファイルから読んだ・書いた値。ファイルに無ければ初めの値）。今の値と違えば *
	};

	// 切り替えの一覧（登録した順に、メニューとページに出る）
	static std::vector<Toggle>& Toggles();
	// 見出しの名前（メニューの子メニュー・ページの区切り・保存のキー）
	static const char* SectionName(ViewSection section);
	// 切り替え1つを、メニューの1行として描く
	static void DrawMenuItem(const Toggle& toggle);

	// --- 保存 ---
	static void Load();
	static void Save();
	static bool IsDirty();
	static void Revert();
	// Settings → Editor → View の中身
	static void DrawPage();
};
#endif
```

- `value` を `bool*` にしているのは、メニューの `ImGui::MenuItem(名前, nullptr, bool*)` にそのまま渡すため。持ち主の値は、エディタが動いている間ずっとある（static の変数）ので、アドレスを覚えておいてよい
- `enabledBy` は Depth of Field 用（Post Effects を切っていると押せない。前の `DrawViewMenu` と同じ決まり）

### A-2 `Editor/Viewport/ViewSettings.cpp`（新しい）

```cpp
#ifdef USE_IMGUI
#include "ViewSettings.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <system_error>

#include <externals/imgui/imgui.h>
#include <externals/magic_enum/magic_enum.hpp>
#include <externals/nlohmann/json.hpp>

#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Editor/Gizmos/CameraGizmo.h"
#include "HTTEngine/Editor/Gizmos/LightGizmo.h"
#include "HTTEngine/Editor/Viewport/EditorViewport.h"
#include "HTTEngine/Editor/Windows/SettingsWindow.h"
#include "HTTEngine/Systems/Animation/AnimationSystem.h"
#include "HTTEngine/Systems/Combat/HitSystem.h"
#include "HTTEngine/Systems/Effects/ParticleSystem.h"
#include "HTTEngine/Systems/Physics/CollisionSystem.h"
#include "HTTEngine/UI/GlobalVariables.h"

namespace {
using json = nlohmann::json;

constexpr const char* kSavePath = "Parameters/Editor/View.json"; // 人ごとの見え方（Parameters/Editor/ は git に入れない）
constexpr const char* kPageName = "View";                        // Settings の一覧の名前（GlobalVariables のグループの名前とかぶらないように）
} // namespace


//=============================================================================
// 初期化
//=============================================================================
void ViewSettings::Initialize() {
	LightGizmoFlags& lightGizmo = LightGizmo::GetGlobalFlags();
	EditorViewport::ViewFlags& viewFlags = EditorViewport::GetViewFlags();

	// --- 切り替えの一覧（ここに足すと、メニュー・保存・Settings のページに全部出る）---
	// 初めの値は持ち主の値のまま（保存したファイルに値があれば、Load でその値になる）
	Toggles() = {
	    // Gizmos（1つずつは、Inspector のライトの区画・Show Gizmo で）
	    {.section = ViewSection::Gizmos, .name = "Light Icons", .value = &lightGizmo.showIcon, .tooltip = "ライトのアイコン（Scene ビューだけ）"},
	    {.section = ViewSection::Gizmos, .name = "Light Ranges", .value = &lightGizmo.showRange, .tooltip = "ライトの届く範囲（Scene ビューだけ）"},
	    {.section = ViewSection::Gizmos, .name = "Camera Icons", .value = &CameraGizmo::GetShowIcons(), .tooltip = "カメラのアイコンと、写す範囲の線（Scene ビューだけ）"},
	    {.section = ViewSection::Gizmos, .name = "Camera Paths", .value = &CameraGizmo::GetShowPaths(), .tooltip = "カメラが動く道の線（Scene ビューだけ）"},
	    {.section = ViewSection::Gizmos, .name = "Camera Follow Zones", .value = &CameraGizmo::GetShowFollowZones(), .tooltip = "Camera Follow の Dead Zone・Soft Zone（Game ビューにも出る。1つずつは Inspector の Show Gizmo）"},
	    {.section = ViewSection::Gizmos, .name = "Camera Orbits", .value = &CameraGizmo::GetShowOrbits(), .tooltip = "Camera Orbit の回る輪・回る中心（Scene ビューだけ。1つずつは Inspector の Show Gizmo）"},
	    {.section = ViewSection::Gizmos, .name = "Particle Shapes", .value = &ParticleSystem::GetShowGizmos(), .tooltip = "粒の出る形（Scene ビューだけ。1つずつは Inspector の Show Gizmo）"},
	    {.section = ViewSection::Gizmos, .name = "Colliders", .value = &CollisionSystem::GetShowGizmos(), .tooltip = "Collider の線（Game ビューにも出る）"},
	    {.section = ViewSection::Gizmos, .name = "Hit Shapes", .value = &HitSystem::GetShowGizmos(), .tooltip = "攻撃の形・ジャスト判定の範囲・Hurtbox（Game ビューにも出る）"},
	    {.section = ViewSection::Gizmos, .name = "Skeletons", .value = &AnimationSystem::GetShowGizmos(), .tooltip = "骨（Game ビューにも出る）"},
	    // Scene View（レンズは Scene では掛けない）
	    {.section = ViewSection::SceneView, .name = "Post Effects", .value = &viewFlags.sceneEffects, .tooltip = "ブルームなどのポストエフェクト（レンズは掛けない。ギズモ・クリックの位置がずれるので）"},
	    {.section = ViewSection::SceneView, .name = "Fog", .value = &viewFlags.sceneFog, .tooltip = "霧（Fog Component）"},
	    {.section = ViewSection::SceneView, .name = "Skybox", .value = &viewFlags.sceneSkybox, .tooltip = "天球（外すと、ギズモが見やすい落ち着いたグラデーション）"},
	    {.section = ViewSection::SceneView, .name = "Depth of Field", .value = &viewFlags.sceneDepthOfField, .enabledBy = &viewFlags.sceneEffects, .tooltip = "被写界深度（Post Effects が入っているときだけ）"},
	    {.section = ViewSection::SceneView, .name = "Grid", .value = &viewFlags.sceneGrid, .tooltip = "グリッド"},
	    {.section = ViewSection::SceneView, .name = "Pick UI", .value = &viewFlags.scenePickUI, .tooltip = "Image・Text をクリックで選ぶ・ドラッグで動かす（外すと、UI の後ろの 3D の物を選べる）"},
	    // Game View
	    {.section = ViewSection::GameView, .name = "Grid", .value = &viewFlags.gameGrid, .tooltip = "グリッド（遊ぶときと同じ見た目にしたいので、初めは描かない）"},
	};
	for (Toggle& toggle : Toggles()) {
		toggle.saved = *toggle.value;
	}

	Load();

	// --- Settings → Editor → View ---
	SettingsWindow::AddPage({
	    .name = kPageName,
	    .section = SettingsSection::Editor,
	    .draw = &ViewSettings::DrawPage,
	    .isDirty = &ViewSettings::IsDirty,
	    .revert = &ViewSettings::Revert,
	    .save = &ViewSettings::Save,
	    .saveLocation = kSavePath,
	    .revertTooltip = "保存した見え方に戻します",
	});
}

// ===== 切り替えの一覧 =====
std::vector<ViewSettings::Toggle>& ViewSettings::Toggles() {
	static std::vector<Toggle> toggles;
	return toggles;
}

// ===== 見出しの名前 =====
const char* ViewSettings::SectionName(ViewSection section) {
	switch (section) {
	case ViewSection::Gizmos:
		return "Gizmos";
	case ViewSection::SceneView:
		return "Scene View";
	case ViewSection::GameView:
		return "Game View";
	}
	return "";
}


//=============================================================================
// View メニュー
//=============================================================================
// ===== メニュー =====
void ViewSettings::DrawMenu() {
	if (!ImGui::BeginMenu("View")) {
		return;
	}
	// 見出しごとの子メニュー（カーソルを合わせると横に出る）
	for (const ViewSection section : magic_enum::enum_values<ViewSection>()) {
		if (!ImGui::BeginMenu(SectionName(section))) {
			continue;
		}
		// 押してもメニューを閉じない（いくつも続けて切り替えられるように。外をクリックか Esc で閉じる）
		ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
		for (const Toggle& toggle : Toggles()) {
			if (toggle.section == section) {
				DrawMenuItem(toggle);
			}
		}
		ImGui::PopItemFlag();
		ImGui::EndMenu();
	}
	ImGui::EndMenu();
}

// ===== メニューの1行 =====
void ViewSettings::DrawMenuItem(const Toggle& toggle) {
	const bool enabled = (toggle.enabledBy == nullptr) || *toggle.enabledBy;
	ImGui::MenuItem(toggle.name.c_str(), nullptr, toggle.value, enabled);
	if (!toggle.tooltip.empty()) {
		ImGui::SetItemTooltip("%s", toggle.tooltip.c_str());
	}
}


//=============================================================================
// 保存
//=============================================================================
// ===== 読む（無い・読めない項目は、今の値のまま）=====
void ViewSettings::Load() {
	std::ifstream stream(kSavePath);
	if (!stream) {
		return; // まだ保存していない
	}
	const json root = json::parse(stream, nullptr, false); // false: 壊れていても例外を投げない
	if (root.is_discarded() || !root.is_object()) {
		LogManager::Error(std::string("[ViewSettings] 見え方のファイルを読めません（初めの値のままにします）: ") + kSavePath);
		return;
	}
	for (Toggle& toggle : Toggles()) {
		const auto section = root.find(SectionName(toggle.section));
		if (section == root.end() || !section->is_object()) {
			continue;
		}
		const auto value = section->find(toggle.name);
		if (value != section->end() && value->is_boolean()) {
			*toggle.value = value->get<bool>();
			toggle.saved = *toggle.value;
		}
	}
}

// ===== 書く（全部の切り替えを、見出しごとに）=====
void ViewSettings::Save() {
	json root = json::object();
	for (const Toggle& toggle : Toggles()) {
		root[SectionName(toggle.section)][toggle.name] = *toggle.value;
	}
	std::error_code error;
	std::filesystem::create_directories(std::filesystem::path(kSavePath).parent_path(), error);
	std::ofstream stream(kSavePath);
	if (!stream) {
		LogManager::Error(std::string("[ViewSettings] 見え方のファイルを書けません: ") + kSavePath);
		return;
	}
	stream << std::setw(4) << root << std::endl;
	for (Toggle& toggle : Toggles()) {
		toggle.saved = *toggle.value;
	}
}

// ===== 保存していない変更があるか =====
bool ViewSettings::IsDirty() {
	for (const Toggle& toggle : Toggles()) {
		if (*toggle.value != toggle.saved) {
			return true;
		}
	}
	return false;
}

// ===== 保存した値に戻す =====
void ViewSettings::Revert() {
	for (const Toggle& toggle : Toggles()) {
		*toggle.value = toggle.saved;
	}
}


//=============================================================================
// Settings → Editor → View
//=============================================================================
void ViewSettings::DrawPage() {
	for (const ViewSection section : magic_enum::enum_values<ViewSection>()) {
		ImGui::SeparatorText(SectionName(section));
		ImGui::PushID(static_cast<int>(section)); // Scene View と Game View の Grid が同じ ID にならないように
		for (const Toggle& toggle : Toggles()) {
			if (toggle.section != section) {
				continue;
			}
			const bool enabled = (toggle.enabledBy == nullptr) || *toggle.enabledBy;
			ImGui::BeginDisabled(!enabled);
			ImGui::Checkbox(toggle.name.c_str(), toggle.value);
			ImGui::EndDisabled();
			if (!toggle.tooltip.empty()) {
				ImGui::SetItemTooltip("%s", toggle.tooltip.c_str());
			}
		}
		ImGui::PopID();
	}
}
#endif
```

- 子メニューの中は、見出しが違えば ID の場所も違う（`BeginMenu` がウィンドウを作る）ので、Scene View と Game View の「Grid」はそのままでぶつからない（前は1つのメニューに並んでいたので `Grid###sceneGrid` にしていた）。Settings のページは1つのウィンドウに並ぶので `PushID` で分ける
- 保存のファイルは、見出しの名前と切り替えの名前で書く（並び順・数が変わっても読める。無い名前は初めの値のまま）

```json
{
    "Game View": { "Grid": false },
    "Gizmos": { "Colliders": false, "Light Icons": true, … },
    "Scene View": { "Grid": false, "Post Effects": true, … }
}
```

### A-3 `HTTEngine.vcxproj`・`HTTEngine.vcxproj.filters`

`ViewportWindow` の行の後ろに足す。

```xml
<!-- HTTEngine.vcxproj：ClCompile の並び -->
    <ClCompile Include="HTTEngine\Editor\Viewport\ViewportWindow.cpp" />
    <ClCompile Include="HTTEngine\Editor\Viewport\ViewSettings.cpp" />
<!-- HTTEngine.vcxproj：ClInclude の並び -->
    <ClInclude Include="HTTEngine\Editor\Viewport\ViewportWindow.h" />
    <ClInclude Include="HTTEngine\Editor\Viewport\ViewSettings.h" />
```

```xml
<!-- HTTEngine.vcxproj.filters -->
    <ClCompile Include="HTTEngine\Editor\Viewport\ViewSettings.cpp">
      <Filter>HTTEngine\Editor\Viewport</Filter>
    </ClCompile>
    <ClInclude Include="HTTEngine\Editor\Viewport\ViewSettings.h">
      <Filter>HTTEngine\Editor\Viewport</Filter>
    </ClInclude>
```

### A-4 `Editor/Viewport/EditorViewport.h`（コメントだけ）

```cpp
	// View メニューで切り替える、ビューごとの見え方（全ウィンドウ共通。メニュー・保存は ViewSettings。ここに足したら ViewSettings の一覧にも足す）
	struct ViewFlags {
```

```cpp
	// View メニューの印（ViewSettings が、メンバーのアドレスを覚えて切り替える・保存する）
	static ViewFlags& GetViewFlags();
```


## パートB ImGuiManager の View メニューを差し替える

`Editor/ImGuiManager.cpp`

### B-1 include

消す（View メニューの中身は ViewSettings.cpp へ移ったので、ここでは使わなくなる）：

```cpp
#include "HTTEngine/Systems/Animation/AnimationSystem.h"
#include "HTTEngine/Systems/Combat/HitSystem.h"
#include "HTTEngine/Systems/Effects/ParticleSystem.h"
#include "HTTEngine/Systems/Physics/CollisionSystem.h"
#include "HTTEngine/Editor/Gizmos/LightGizmo.h"
#include "HTTEngine/Editor/Gizmos/CameraGizmo.h"
```

`EditorViewport.h` の行を差し替える：

```cpp
#include "HTTEngine/Editor/Viewport/ViewSettings.h"
```

### B-2 `DrawViewMenu` を消す

無名 namespace の中の `// View メニュー` から `DrawViewMenu` の終わりの `}` までを、まるごと消す。

`DrawMenuBar` の呼ぶ所：

```cpp
		DrawFileMenu();
		DrawEditMenu();
		ViewSettings::DrawMenu();
		DrawSettingMenu();
```

### B-3 `Initialize` で読む

`EditorKeymap::Initialize();` の次：

```cpp
	// View メニューの切り替え（保存した見え方を読み、Settings に View のページを足す）
	ViewSettings::Initialize();
```

ここまでで ① はでき上がる（ビルドして、View にカーソルを合わせて確かめられる）。


## パートC Save の入口を1つにする

### C-1 `Editor/Windows/SettingsWindow.h`

`AddPage` の下に足す：

```cpp
	// --- 見出しごとにまとめて（File → Save・閉じるときの確認から）---
	// section の中で、保存していない変更があるページの名前（一覧と同じ順）
	static std::vector<std::string> GetDirtyPageNames(SettingsSection section);
	// section の中で、保存していない変更があるページを全部保存する
	static void SaveSection(SettingsSection section);
```

### C-2 `Editor/Windows/SettingsWindow.cpp`

`HasPage` の後ろ（`// 描画` の大見出しの前）に足す：

```cpp
//=============================================================================
// 見出しごとにまとめて
//=============================================================================
// ===== 保存していない変更があるページの名前 =====
std::vector<std::string> SettingsWindow::GetDirtyPageNames(SettingsSection section) {
	std::vector<std::string> names;
	// GlobalVariables のグループ → AddPage のページ（左の一覧と同じ順）
	GlobalVariables* gv = GlobalVariables::GetInstance();
	for (const std::string& name : gv->GetGroupNames(section)) {
		if (gv->IsGroupDirty(name)) {
			names.push_back(name);
		}
	}
	for (const SettingsPage& page : pages_) {
		if (page.section == section && page.isDirty()) {
			names.push_back(page.name);
		}
	}
	return names;
}

// ===== 保存していない変更があるページを全部保存 =====
void SettingsWindow::SaveSection(SettingsSection section) {
	// 変わっていないページは書かない（ファイルの日付を変えない）
	GlobalVariables* gv = GlobalVariables::GetInstance();
	for (const std::string& name : gv->GetGroupNames(section)) {
		if (gv->IsGroupDirty(name)) {
			gv->SaveGroup(name);
		}
	}
	for (const SettingsPage& page : pages_) {
		if (page.section == section && page.isDirty()) {
			page.save();
		}
	}
}
```

### C-3 `Scene/SceneManager.h`

`RequestSave` の下に足す：

```cpp
	// RequestSave をまだ済ませていないか（済んだかは、その後の IsDirty で分かる。エディタの「保存してから閉じる」が待つのに使う）
	bool IsSaveRequested() const { return isSaveRequested_; }
```

### C-4 `Editor/Windows/SceneDialogs.h`（全部差し替え）

```cpp
#pragma once
#include <cstdint>
#include <functional>
#include <string>

/// <summary>
/// シーンのファイルの操作で出す確認のウィンドウと、File メニューの中身
/// <para>Save は「開いているシーン」と「エディタの設定（Settings → Editor の全ページ・ウィンドウの配置）」をまとめて保存する（RequestSave。Control ウィンドウの Save・Ctrl+S・File → Save が全部ここを通る）</para>
/// <para>保存していない変更があるまま別のシーンを開く・エディタを閉じるときは「保存しますか？（Save / Don't Save / Cancel）」を聞く</para>
/// <para>New Scene・Save As は名前を聞く（使える文字は英数字と _ -。FileNameRule.h）</para>
/// <para>シーンを開く・作る・保存するのは停止中だけ（再生中はメニューが押せない。エディタの設定は再生中でも保存できる）</para>
/// </summary>
class SceneDialogs {
public:
	// 毎フレーム1回（ImGuiManager::Begin から）。確認のウィンドウとショートカット（Ctrl+N・Ctrl+Shift+S）
	static void Draw();
	// File メニューの中身（ImGui::BeginMenu("File") の中から呼ぶ）
	static void DrawFileMenuItems();

	// --- 保存 ---
	// 保存する物があるか（シーンを保存できる・エディタの設定に保存していない変更がある）。Save のボタン・メニューを押せるか
	static bool CanSave();
	// 開いているシーン（保存できるときだけ。書くのは次のフレームの頭）と、エディタの設定（すぐ書く）を保存する
	static void RequestSave();

	// --- シーンを開く・作る ---
	// name のシーンを開く。保存していない変更があれば、先に聞く（Project ウィンドウのダブルクリックからも）
	static void RequestOpen(const std::string& name);
	// 名前を聞いて、新しいシーンを作って開く
	static void RequestNew();
	// 名前を聞いて、今のシーンをその名前で保存し、開き直す
	static void RequestSaveAs();

	// --- エディタを閉じる（ウィンドウの ×・Alt+F4。ImGuiManager がウィンドウにつなぐ）---
	// もう閉じてよいか（保存していない変更が無い・「保存しますか？」に答え終わった）
	static bool CanQuit();
	// 閉じてよいか聞く。答えが出て閉じてよくなったら close を呼ぶ（close はウィンドウにもう一度「閉じて」と送る）
	static void RequestQuit(std::function<void()> close);

private:
	// 確認のウィンドウの種類
	enum class Dialog {
		None,
		Unsaved,  // 保存しますか？（別のシーンを開く前）
		Quit,     // 保存しますか？（エディタを閉じる前）
		NewScene, // 新しいシーンの名前
		SaveAs,   // 名前を付けて保存
	};
	// エディタを閉じる流れ
	enum class QuitState : uint8_t {
		None,        // 閉じようとしていない
		WaitingSave, // 「保存しますか？」で Save を押した。シーンの保存（次のフレームの頭）が済むのを待っている
		Approved,    // 閉じてよい（ウィンドウに「閉じて」を送った）
	};
	static const char* TitleOf(Dialog dialog);
	static void DrawUnsavedDialog();
	static void DrawQuitDialog();
	// 名前を聞くウィンドウ（NewScene・SaveAs で共通）
	static void DrawNameDialog(Dialog dialog);
	static void HandleShortcuts();

	// --- 保存の中身 ---
	// 開いているシーンを保存できるか（停止中で、シーンファイルを使っている）
	static bool CanSaveScene();
	// 開いているシーンに保存していない変更があるか
	static bool IsSceneDirty();
	// 保存していない物が何かあるか（シーン・エディタの設定）
	static bool HasUnsavedChanges();

	// --- 閉じる ---
	// Save を押した後、保存が済んだかを見る（済んで、保存していない物が無くなったら閉じる）
	static void UpdateQuit();
	// 閉じてよいことにして、ウィンドウを閉じる
	static void ApproveQuit();

	static Dialog openRequest_;                // 次のフレームに開く確認のウィンドウ（ほかのポップアップの中から開くと入れ子になるので、1フレーム預かる）
	static std::string pendingOpen_;           // 「保存しますか？」に答えた後に開くシーン
	static std::string nameBuffer_;            // 名前の入力欄の中身
	static bool nameFocusRequest_;             // 名前の入力欄を出した最初のフレームだけ true（すぐに打てるように）
	static QuitState quitState_;               // エディタを閉じる流れのどこか
	static std::function<void()> closeWindow_; // 閉じてよくなったら呼ぶ（RequestQuit でもらう）
};
```

### C-5 `Editor/Windows/SceneDialogs.cpp`

**include と静的メンバ変数**（足す行に `// ★`。★のコメントは写さない）：

```cpp
#include <utility> // ★
#include <vector>

#include <externals/imgui/imgui.h>

#include "HTTEngine/Diagnostics/LogManager.h" // ★
#include "HTTEngine/Editor/Keymap/EditorKeymap.h"
#include "HTTEngine/Editor/Widgets/EditorWidgets.h"
#include "HTTEngine/Editor/Windows/SettingsWindow.h" // ★
#include "HTTEngine/Scene/SceneManager.h"
#include "HTTEngine/String/FileNameRule.h"
#include "HTTEngine/UI/GlobalVariables.h" // ★（SettingsSection）

// 静的メンバ変数
SceneDialogs::Dialog SceneDialogs::openRequest_ = SceneDialogs::Dialog::None;
std::string SceneDialogs::pendingOpen_;
std::string SceneDialogs::nameBuffer_;
bool SceneDialogs::nameFocusRequest_ = false;
SceneDialogs::QuitState SceneDialogs::quitState_ = SceneDialogs::QuitState::None; // ★
std::function<void()> SceneDialogs::closeWindow_;                                  // ★
```

**保存**（無名 namespace の `} // namespace` の後ろ、`// 頼む` の大見出しの前に足す）：

```cpp
//=============================================================================
// 保存（Control ウィンドウの Save・Ctrl+S・File → Save・「保存しますか？」の Save）
//=============================================================================
// ===== 開いているシーンを保存できるか =====
bool SceneDialogs::CanSaveScene() {
	// シーンファイルを使わない前の書き方のシーンは、保存できない
	return CanEditScene() && SceneManager::GetMain()->GetSceneFile() != nullptr;
}

// ===== 開いているシーンに保存していない変更があるか =====
bool SceneDialogs::IsSceneDirty() {
	const SceneManager* sceneManager = SceneManager::GetMain();
	return sceneManager != nullptr && sceneManager->IsDirty();
}

// ===== 保存していない物が何かあるか =====
bool SceneDialogs::HasUnsavedChanges() { return IsSceneDirty() || !SettingsWindow::GetDirtyPageNames(SettingsSection::Editor).empty(); }

// ===== 保存する物があるか =====
bool SceneDialogs::CanSave() {
	// シーンは変更が無くても保存できる（今までどおり）。エディタの設定は、変更があるときだけ（再生中でも）
	return CanSaveScene() || !SettingsWindow::GetDirtyPageNames(SettingsSection::Editor).empty();
}

// ===== 保存する =====
void SceneDialogs::RequestSave() {
	// --- エディタの設定（Scene ビューのカメラ・View の見え方・Keymap など、Settings → Editor の全ページ）---
	SettingsWindow::SaveSection(SettingsSection::Editor);
	// --- ウィンドウの配置（ImGui も数秒ごとに書くが、Save の時点の配置を確実に残す）---
	if (const char* iniFile = ImGui::GetIO().IniFilename) {
		ImGui::SaveIniSettingsToDisk(iniFile);
	}
	// --- シーン（書くのは次のフレームの頭。再生中・シーンファイルが無いときは書かない）---
	if (CanSaveScene()) {
		SceneManager::GetMain()->RequestSave();
	}
}
```

**`Draw`**（2行足す）：

```cpp
void SceneDialogs::Draw() {
	HandleShortcuts();
	UpdateQuit(); // ★
	// 預かっていた確認のウィンドウを開く（ここはどのポップアップの中でもないので、入れ子にならない）
	if (openRequest_ != Dialog::None) {
		ImGui::OpenPopup(TitleOf(openRequest_));
		openRequest_ = Dialog::None;
	}
	DrawUnsavedDialog();
	DrawQuitDialog(); // ★
	DrawNameDialog(Dialog::NewScene);
	DrawNameDialog(Dialog::SaveAs);
}
```

**`TitleOf`**（`case Dialog::Unsaved:` の後ろに足す）：

```cpp
	case Dialog::Quit:
		return "Unsaved Changes###Quit"; // 見た目の題は同じ。「###」の後ろを ID にして、別のシーンを開く前の確認と分ける
```

**`DrawFileMenuItems` の Save**（差し替え。Save As は `hasFile` のまま）：

```cpp
	// シーンとエディタの設定（再生中は、エディタの設定に変更があるときだけ押せる）
	if (ImGui::MenuItem("Save", EditorKeymap::GetShortcutText(EditorAction::SaveScene).c_str(), false, CanSave())) {
		RequestSave();
	}
```

**`DrawUnsavedDialog` の Save**（`sceneManager->RequestSave();` を `RequestSave();` に）：

```cpp
	// Save：保存してから開く（SceneManager は同じフレームに頼まれたら「保存 → 開く」の順にする。保存できなければ開かない）
	// エディタの設定も、ふつうの Save と同じく一緒に保存する
	if (ImGui::Button("Save")) {
		RequestSave();
		sceneManager->RequestOpenScene(pendingOpen_);
		ImGui::CloseCurrentPopup();
	}
```

**閉じる**（`DrawUnsavedDialog` の後ろ、`// 名前を聞く` の大見出しの前に足す）：

```cpp
//=============================================================================
// エディタを閉じる（ウィンドウの ×・Alt+F4）
//=============================================================================
// ===== もう閉じてよいか（ウィンドウが WM_CLOSE で聞く）=====
bool SceneDialogs::CanQuit() { return quitState_ == QuitState::Approved || !HasUnsavedChanges(); }

// ===== 閉じてよいか聞く（CanQuit が false だったとき、ウィンドウが呼ぶ）=====
void SceneDialogs::RequestQuit(std::function<void()> close) {
	if (quitState_ == QuitState::WaitingSave) {
		return; // もう Save を押して、保存が済むのを待っている（済めば閉じる）
	}
	closeWindow_ = std::move(close);
	openRequest_ = Dialog::Quit;
}

// ===== 保存しますか？（閉じる前）=====
void SceneDialogs::DrawQuitDialog() {
	CenterNextWindow();
	if (!ImGui::BeginPopupModal(TitleOf(Dialog::Quit), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		return;
	}

	// --- 保存していない物を並べる（何が消えるか分かるように）---
	ImGui::Text("保存していない変更があります。閉じる前に保存しますか？");
	ImGui::Spacing();
	const bool isSceneDirty = IsSceneDirty();
	if (isSceneDirty) {
		ImGui::BulletText("シーン: %s", SceneManager::GetMain()->GetOpenSceneName().c_str());
	}
	const std::vector<std::string> editorPages = SettingsWindow::GetDirtyPageNames(SettingsSection::Editor);
	if (!editorPages.empty()) {
		std::string pages; // "Scene View・View"
		for (const std::string& name : editorPages) {
			if (!pages.empty()) {
				pages += "・";
			}
			pages += name;
		}
		ImGui::BulletText("エディタの設定: %s", pages.c_str());
	}
	// 再生中はシーンを保存できない（Play 中の変更は Stop で戻る物なので、保存する物ではない）
	const bool canSaveAll = !isSceneDirty || CanSaveScene();
	if (!canSaveAll) {
		ImGui::Spacing();
		ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.4f, 1.0f), "再生中はシーンを保存できません（Stop してから閉じてください）");
	}
	ImGui::Spacing();

	// --- Save：保存して、保存できたら閉じる（シーンを書くのは次のフレームの頭なので、UpdateQuit が待つ）---
	ImGui::BeginDisabled(!canSaveAll);
	if (ImGui::Button("Save")) {
		RequestSave();
		quitState_ = QuitState::WaitingSave;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	// --- Don't Save：変更を捨てて閉じる ---
	if (ImGui::Button("Don't Save")) {
		ImGui::CloseCurrentPopup();
		ApproveQuit();
	}
	ImGui::SameLine();
	// --- Cancel：閉じない ---
	if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
		closeWindow_ = nullptr;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

// ===== Save を押した後、保存が済んだら閉じる =====
void SceneDialogs::UpdateQuit() {
	if (quitState_ != QuitState::WaitingSave) {
		return;
	}
	const SceneManager* sceneManager = SceneManager::GetMain();
	if (sceneManager != nullptr && sceneManager->IsSaveRequested()) {
		return; // まだフレームの頭でシーンを書いていない
	}
	// 書けなかった物が残っていたら、閉じない（消えてしまうので。理由はログに出ている）
	if (HasUnsavedChanges()) {
		LogManager::Warning("保存できなかった物があるので、閉じるのをやめました");
		quitState_ = QuitState::None;
		closeWindow_ = nullptr;
		return;
	}
	ApproveQuit();
}

// ===== 閉じてよいことにして、閉じる =====
void SceneDialogs::ApproveQuit() {
	quitState_ = QuitState::Approved;
	// ウィンドウにもう一度「閉じて」と送る（今度は CanQuit が true なので閉じる。描いている途中で消さないように、次のフレームの頭で）
	if (closeWindow_) {
		closeWindow_();
		closeWindow_ = nullptr;
	}
}
```

- `UpdateQuit` は `Draw` の頭で呼ぶ。1フレームの順は「メッセージ → `UpdateAll`（`SceneManager::Update` がシーンを書く）→ `PreRenderAll`（ここ）」なので、Save を押した次のフレームのここでは、もう書き終わっている

### C-6 `Editor/Windows/ControlWindow.h`

```cpp
	// 右：保存（Ctrl+S も。シーンとエディタの設定。中身は SceneDialogs::RequestSave）
	static void DrawSaveButton();
```

### C-7 `Editor/Windows/ControlWindow.cpp`

include に足す：

```cpp
#include "HTTEngine/Editor/Windows/SceneDialogs.h"
```

`Draw` の呼ぶ所：`DrawSaveButton(*sceneManager);` → `DrawSaveButton();`

`DrawSaveButton`（`sceneManager` を使う所を `SceneDialogs` に）：

```cpp
void ControlWindow::DrawSaveButton() {
	// 保存の中身（シーン＋エディタの設定）と、押せるかは SceneDialogs が決める（File → Save と同じ）
	const bool canSave = SceneDialogs::CanSave();

	// --- 右端に寄せる ---
	（ここは今のまま）
	ImGui::BeginDisabled(!canSave);
	if (ImGui::Button(label)) {
		SceneDialogs::RequestSave();
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("%s。シーンは停止中だけ", EditorKeymap::WithShortcut("シーンとエディタの設定（Scene ビューのカメラ・View・Keymap など）を保存", EditorAction::SaveScene).c_str());

	// --- Ctrl+S（文字の入力中は、入力欄のほうを優先する。名前の変更中に Ctrl+S を押しても保存しない）---
	const bool isTyping = ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive();
	if (canSave && !isTyping && EditorKeymap::IsTriggered(EditorAction::SaveScene)) {
		SceneDialogs::RequestSave();
	}
}
```

### C-8 `Editor/Keymap/EditorKeymap.cpp`（説明だけ）

`EditorAction::SaveScene` の `.tooltip`：

```cpp
.tooltip = "開いているシーン（停止中だけ）と、エディタの設定（Settings → Editor の全ページ）を保存する",
```


## パートD × で閉じるときにつなぐ

### D-1 `Window/Win32Window.h`

`ClearPendingResize` の下から `private:` までを差し替える（`onCanClose_`・`onTryClose_` は private へ）：

```cpp
	void ClearPendingResize() { pendingResize_ = false; }

	/// <summary>
	/// ×（Alt+F4 も）で閉じる前に聞く。canClose が false を返したら閉じずに onBlocked を呼ぶ（エディタは「保存しますか？」を出す）
	/// <para>答えが出て閉じてよくなったら、RequestClose でもう一度「閉じて」と送る（そのときは canClose が true を返す）</para>
	/// </summary>
	void SetCloseGuard(std::function<bool()> canClose, std::function<void()> onBlocked) {
		onCanClose_ = std::move(canClose);
		onTryClose_ = std::move(onBlocked);
	}
	// × を押したのと同じ「閉じて」を送る（次のメッセージ処理で、SetCloseGuard の canClose を通ってから閉じる）
	void RequestClose() const;

	// ウィンドウシステム
	bool isPositionLocked_ = false;

private:
	std::function<bool()> onCanClose_; // 閉じてよいか（無ければいつも閉じる）
	std::function<void()> onTryClose_; // 閉じてよくなかったときに呼ぶ
```

WM_CLOSE の所（`WindowProc`）は今のままでよい（もとから `onCanClose_` → `onTryClose_` の順に見ている）。

### D-2 `Window/Win32Window.cpp`

`ProcessMessage` の後ろに足す：

```cpp
// ===== 「閉じて」を送る =====
void Win32Window::RequestClose() const {
	// その場で DestroyWindow しない（描いている途中のフレームで、ウィンドウが消えないように。次の ProcessMessage で WM_CLOSE を通る）
	PostMessage(hwnd_, WM_CLOSE, 0, 0);
}
```

### D-3 `Editor/ImGuiManager.cpp`

`Initialize` の `AssetIcons::Initialize();` の後ろ（`LogManager::Log("Initialized");` の前）：

```cpp
	// ×（Alt+F4 も）で閉じるとき、保存していない物（シーン・エディタの設定）があれば「保存しますか？」を出す（答えるまで閉じない）
	// window は WindowManager が持っていて、閉じるまで消えない（2つ目のラムダは、閉じてよくなったときに1回だけ呼ばれる）
	window->SetCloseGuard([] { return SceneDialogs::CanQuit(); }, [window] { SceneDialogs::RequestQuit([window] { window->RequestClose(); }); });
```

（`SceneDialogs.h` はもう include してある）


## パートE Reference

`Docs/Reference/17-Editor.md` の表。

**Save の行**（差し替え）：

```
| **Save（Ctrl+S）** | Control ウィンドウ・File → Save。**開いているシーン**（停止中だけ。シーンファイル `resources/scenes/〇〇.scene.json` に書く。1つ前は `.bak` に残る）と、**エディタの設定**（Settings → Editor の、保存していないページ全部＝Scene View〈Scene ビューのカメラの位置〉・View〈View メニューの見え方〉・Keymap）と**ウィンドウの配置**（`imgui.ini`）をまとめて保存する。エディタの設定は再生中でも保存できる |
```

**File の行**（差し替え）と、その下に **閉じる** の行を足す：

```
| **File** | New Scene（Ctrl+N）・Open Scene・Save（Ctrl+S。上の Save と同じ）・Save As（Ctrl+Shift+S）。シーンを開く・作る・名前を付けて保存は停止中だけ |
| **閉じる（× ・Alt+F4）** | 保存していない物（シーンの `*`・Settings → Editor の `*`）があると「保存しますか？」が出て、何が保存されていないかを並べる。Save：保存して閉じる（保存できなかったら閉じない）／Don't Save：捨てて閉じる／Cancel：閉じない。再生中にシーンの変更があるときは Save を押せない（Stop してから） |
```

**Settings の行**：「Editor：Scene View（Scene ビューのカメラ）。」→「Editor：Scene View（Scene ビューのカメラ）・Keymap・View（View メニューの見え方）。」

**View → Gizmos の行の上**に足す：

```
| **View** | Gizmos・Scene View・Game View の3つの見出し。カーソルを合わせると、その見出しの切り替えが横に出る。押してもメニューは閉じない（続けて切り替えられる）。切り替えは Settings → Editor → View にも同じ物が並び、Save で `Parameters/Editor/View.json`（git に入らないので人ごと）。次の起動で保存した見え方から始まる |
```

**表の下の注意**（差し替え）：

```
> **Save しないで閉じようとすると「保存しますか？」が出る**（Don't Save で閉じると、停止中の編集は消える。Play → Stop では消えない）。Play 中の変更は Stop で戻るので、Play 中に見つけた良い値は、Stop してから入れ直して Save する（Unity と同じ）。
```


## パートF 確かめ方

### F-1 ビルド

Debug・Development・Release の3つ（Release は `USE_IMGUI` が無いので、ViewSettings・SceneDialogs は入らない。Win32Window の `SetCloseGuard` はだれも呼ばず、今までどおり × ですぐ閉じる）。

### F-2 写しで確かめた事（Development の Sandbox）

ImGui に入力を入れる一時的な道具（写しの ImGuiManager にだけ入れて、確かめた後に外した）で押して、撮った。

| # | やった事 | 結果 |
|---|---|---|
| 1 | View を押す | Gizmos ▶・Scene View ▶・Game View ▶ の3行だけが出る |
| 2 | Gizmos にカーソルを合わせる | 右に Light Icons〜Skeletons の10行が出る（全部 ✓） |
| 3 | Colliders を押す | ✓ が外れ、メニューは開いたまま。説明（Collider の線…）が出る。Settings の一覧に `View *` |
| 4 | Scene View にカーソルを合わせる | Post Effects・Fog・Skybox・Depth of Field・Grid・Pick UI が出る |
| 5 | Settings → Scene View で Translation の Y を 0.6 に | `Scene View *`・`DebugCamera *` |
| 6 | × を送る（WM_CLOSE） | 閉じずに「保存しますか？」。「エディタの設定: Scene View・View」が並ぶ |
| 7 | Cancel | 閉じない |
| 8 | もう一度 × → Save | `Parameters/Editor/View.json`（Colliders・Scene View の Grid が false）・`Parameters/Editor/Scene View/DebugCamera.json`（Translation の Y 0.6）・`imgui.ini` が書かれてから閉じる |
| 9 | 開き直す | Colliders の ✓ が外れたまま始まる。`View` に `*` は付かない |
| 10 | 何も変えずに × | 何も聞かずに閉じる |
| 11 | ＋ → Create Entity（シーンに `*`）→ × | 「シーン: SampleScene」が並ぶ。Save → シーンファイルに Entity が入ってから閉じる |
| 12 | Entity を作る → Play → × | 「再生中はシーンを保存できません（Stop してから閉じてください）」が赤で出て、Save は押せない。Don't Save → シーンファイルは書かれずに閉じる |

### F-3 手で見てほしい所

1. **Ctrl+S**：本物のキーボードで。エディタの設定に `*` がある状態で Ctrl+S → `*` が全部消える（Ctrl+S は DirectInput で読むので、写しの道具では押せなかった）
2. **Scene ビューのカメラ**：本物のマウスで動かす → Settings → Scene View に `*` → Save → 閉じて開き直すと、同じ位置から始まる
3. **再生中の Save ボタン**：Play 中に View の切り替えを変えると、Control の Save が押せるようになる（押すとエディタの設定だけ書く。シーンは書かない）
4. 子メニューの出方（カーソルを斜めに動かしたとき、隣の見出しに切り替わってしまわないか）
