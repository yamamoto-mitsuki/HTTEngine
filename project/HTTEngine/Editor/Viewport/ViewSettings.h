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
