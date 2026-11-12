#pragma once
#include <optional>
#include <vector>

#include "HTTEngine/Input/Action/InputActionMap.h"


/// <summary>
/// 割り当てを拾う係（「次に押したキー・ボタン」を、Action の割り当てにする）
/// <para>Start の後、毎フレーム Update を呼ぶ。拾えたら Captured を返し、GetCaptured で割り当てを渡す（表に入れるのは呼ぶ側。ぶつかりを見せてから入れたいので）</para>
/// <para>Update は、入力を読める所（表の InputActionMap::Update と同じ、フレームの頭）で呼ぶ。WindowManager::UpdateAll の後（描く間・ImGui の中）は InputManager がウィンドウを手前でない扱いにするので、何も拾えない</para>
/// <para>始めたときに押していた物（Start を押したクリックなど）は、離して押し直すまで拾わない。Esc でやめる（なので Esc は割り当てられない）</para>
/// <para>修飾キー（Ctrl・Shift・Alt）を押しながらほかを押すと "Ctrl+Z" のように拾う。修飾キーだけを押して離すと、そのキー1つを拾う</para>
/// <para>拾っている間は、その表の SetEnabled(false) にしておく（Ctrl+Z を拾うときに Undo が動かないように）。エディタの Keymap とゲームのキーコンフィグの画面の両方で使う</para>
/// </summary>
class InputRebinder {
public:
	// Update の結果
	enum class Result {
		Idle,      // 拾っていない
		Listening, // 拾っている（まだ何も押していない）
		Captured,  // 拾えた（GetCaptured で受け取る）
		Canceled,  // Esc でやめた
	};

	// 拾い始める（map の id の、index 番目の割り当てを作る。index が割り当ての数と同じなら、新しく足す分）
	void Start(const InputActionMap& map, InputActionId id, int index);
	// やめる
	void Cancel();
	// 毎フレーム1回
	Result Update();

	bool IsListening() const { return map_ != nullptr; }
	InputActionId GetAction() const { return action_; }
	int GetIndex() const { return index_; }
	// 拾った割り当て（Captured を返した後だけ。向きは、元の割り当ての向きを引き継ぐ）
	const std::optional<InputBinding>& GetCaptured() const { return captured_; }

private:
	// 入力1つを、拾うときの決まりで「押している」とみなすか（軸は大きく動かしたときだけ）
	bool IsDown(const InputControl& control) const;
	// 拾い終える
	void Finish(const InputBinding& binding);

	const InputActionMap* map_ = nullptr; // 拾っている表（拾っていなければ nullptr。持ち主ではない）
	InputActionId action_;
	int index_ = -1;
	InputDeviceMask accepts_ = InputDeviceMask::None; // 拾ってよい入力
	InputDirection direction_ = InputDirection::PositiveX; // 作る割り当ての向き（元の割り当てから）
	bool isFirstUpdate_ = true;                       // 始めてから、まだ Update していない（最初の Update は、押している物を覚えるだけ）
	std::optional<InputControl> pendingModifierKey_; // 修飾キーだけを押している（離したら、それ1つを拾う）
	InputModifiers pendingModifiers_ = InputModifiers::None; // 修飾キーを押したときに、一緒に押していたほかの修飾キー
	std::vector<bool> wasDown_;                       // 前のフレームに押していたか（InputBindingText::AllControls の順）
	std::optional<InputBinding> captured_;
};
