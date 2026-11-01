#pragma once
#include <optional>

#include <externals/imgui/imgui.h>

#include "MyEngine/Curve/Curve.h"
#include "MyEngine/Math/Vector2.h"

// 前方宣言
class ComponentUI;

/// <summary>
/// 1回の操作の前と後のカーブ（Undo に積むための物）
/// </summary>
struct CurveEdit {
	Curve before; // 操作の前
	Curve after;  // 操作の後
};


/// <summary>
/// カーブを直す ImGui の部品（Inspector・ウィンドウのどこにでも置ける）
/// <para>知っているのは Curve だけ。ファイルへの保存と Undo への記録は、持ち主が行う（TakeFinishedEdit）</para>
/// <para>操作：クリックで選ぶ / つまんで動かす / ダブルクリックでキーを足す（線の近くなら形を変えない） / Delete で消す /
/// ホイールで拡大縮小（Ctrl：時間だけ、Shift：値だけ） / 中ボタンか Alt + 左ボタンで移動 / F で全体を見る</para>
/// </summary>
class CurveEditor {
public:
	/// <summary>
	/// 描いて、操作を curve に反映する（毎フレーム呼ぶ）
	/// </summary>
	/// <param name="id">ImGui の ID（同じウィンドウに2つ置くときは変える）</param>
	/// <param name="curve">直すカーブ</param>
	/// <param name="canvasHeight">カーブの絵の高さ（ピクセル）</param>
	/// <param name="ui">選んでいるキーの数値を描く係</param>
	/// <param name="fieldsWidth">数値の欄を絵の右に置くときの幅（0 なら絵の下に置く）</param>
	/// <returns>このフレームで curve を変えたか</returns>
	bool Draw(const char* id, Curve& curve, float canvasHeight, ComponentUI& ui, float fieldsWidth = 0.0f);

	/// <summary>
	/// 操作（つまんで離す・打って確定する・足す・消す）が終わっていれば、その前と後を返す。1回の操作につき1回だけ返る
	/// </summary>
	std::optional<CurveEdit> TakeFinishedEdit();

private:
	/// <summary>
	/// つまんでいる物
	/// </summary>
	enum class DragTarget {
		None,
		Key,
		InHandle,
		OutHandle,
	};

	/// <summary>
	/// マウスの下にある物
	/// </summary>
	struct Hit {
		DragTarget target = DragTarget::None;
		int key = -1; // キーの番号（無ければ -1）
	};

	// ===== 上の並び =====
	void DrawToolbar(Curve& curve);
	void ApplyPreset(Curve& curve, Ease::Type type);

	// ===== カーブの絵 =====
	void DrawCanvas(Curve& curve, float canvasWidth, float canvasHeight);
	void HandleView();
	void HandleMouse(Curve& curve);
	void HandleKeyboard(Curve& curve);
	void FrameAll(const Curve& curve);
	Hit FindHit(const Curve& curve, const ImVec2& mouse) const;
	void AddKeyAt(Curve& curve, const Vector2& position, float mouseY);
	void RemoveSelectedKey(Curve& curve);
	void DrawGridLines(ImDrawList* drawList, bool isTime) const;
	void DrawCurve(ImDrawList* drawList, const Curve& curve) const;
	void DrawKeys(ImDrawList* drawList, const Curve& curve) const;

	// ===== 数値の入力 =====
	void DrawKeyFields(Curve& curve, ComponentUI& ui);

	// ===== 座標の変換 =====
	ImVec2 ToScreen(const Vector2& position) const;
	Vector2 ToCurve(const ImVec2& screen) const;

	// ===== 1回の操作 =====
	void BeginEdit(const Curve& curve);
	void FinishEdit(const Curve& curve);

	// ===== 見ている範囲 =====
	Vector2 viewMin_ = {0.0f, 0.0f};   // 左下（時間, 値）
	Vector2 viewMax_ = {1.0f, 1.0f};   // 右上（時間, 値）
	bool needsFrame_ = true;           // 次に描くとき、全体が見える範囲にする
	ImVec2 canvasMin_ = {0.0f, 0.0f};  // 絵の左上（画面のピクセル）
	ImVec2 canvasSize_ = {1.0f, 1.0f}; // 絵の大きさ（ピクセル）

	// ===== 選択・つまんでいる物 =====
	int selectedKey_ = -1;               // 選んでいるキー（無ければ -1）
	DragTarget drag_ = DragTarget::None; // つまんでいる物
	Vector2 dragOffset_ = {0.0f, 0.0f};  // つまんだ物とマウスのずれ（カーブの単位）

	// ===== 1回の操作 =====
	// 操作の間は「操作の前のカーブ」から毎回作り直す。途中で区間が縮んでハンドルが縮んでも、戻せば元の長さへ戻る
	std::optional<Curve> editBefore_;       // 操作の前のカーブ（操作をしていなければ空）
	int editKey_ = -1;                      // editBefore_ の中での、直しているキーの番号
	std::optional<CurveEdit> finishedEdit_; // 終わった操作（TakeFinishedEdit で渡す）
};