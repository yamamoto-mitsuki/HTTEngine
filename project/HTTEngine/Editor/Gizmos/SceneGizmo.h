#pragma once
#include <externals/imgui/imgui.h>

// 前方宣言
class DebugCamera;

// ギズモでする操作
enum class GizmoOperation {
	Move,   // 移動（W）
	Rotate, // 回転（E）
	Scale,  // 拡縮（R）
};

// ギズモの軸の向き
enum class GizmoSpace {
	Local, // 物の向き（物が回っていれば、軸も一緒に回る）
	World, // ワールドの向き（いつも X・Y・Z）
};

/// <summary>
/// ギズモの設定（Controlウィンドウのボタンと、Sceneビューのキーで変える）
/// </summary>
struct GizmoSettings {
	GizmoOperation operation = GizmoOperation::Move; // 今の道具
	GizmoSpace space = GizmoSpace::Local; // 軸の向き（拡縮はいつもLocal）
	bool snap = false;        // 決まった量ずつ動かす（Ctrl を押している間は逆になる）
	float moveSnap = 0.5f;    // 移動の刻み（長さ）
	float rotateSnap = 10.0f; // 回転の刻み（度）
	float scaleSnap = 0.1f;   // 拡縮の刻み（倍）
};


/// <summary>
///  Sceneビューで、物をクリックして選ぶ・選んだ物をギズモで動かす（移動・回転・拡縮）
/// <para>選んだ物は HierarchyWindow が持つ（Hierarchy・Inspector と同じ物を指す）</para>
/// <para>ギズモで動かした分は、1回のドラッグで1件の履歴になる（Ctrl+Z で戻せる）</para>
/// <para>キー（Sceneビューを触っている時だけ）：W 移動・E 回転・R 拡縮・F 選んだ物へ寄る</para>
/// </summary>
class SceneGizmo {
public:

	/// <summary>
	/// Scene ビューの絵の上に描く。ViewportWindow の overlay の中から、毎フレーム1回
	/// <para>EditorOverlay::Draw（ImGuizmo の BeginFrame・描き先・絵の場所を決める）の後に呼ぶ</para>
	/// </summary>
	/// <param name="camera">Scene ビューのカメラ（F キーで動かす）</param>
	/// <param name="imageMin">絵の左上（画面の座標）</param>
	/// <param name="imageSize">絵の大きさ</param>
	/// <param name="isHovered">マウスが絵の上にあるか</param>
	static void Draw(DebugCamera& camera, const ImVec2& imageMin, const ImVec2& imageSize, bool isHovered);

	// 設定（Control ウィンドウが読み書きする）
	static GizmoSettings& GetSettings();
};