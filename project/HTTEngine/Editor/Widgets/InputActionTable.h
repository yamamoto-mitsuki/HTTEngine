#pragma once
#include "HTTEngine/Input/Action/InputActionMap.h"
#include "HTTEngine/Input/Action/InputRebinder.h"

/// <summary>
/// Input Action の表の割り当てを直す ImGui の表（Settings の Keymap ページなど。表1つに1つ持つ）
/// <para>Action を group ごとに見出しを付けて並べ、割り当てをボタンで出す。ボタンを押すと、次に押したキー・ボタンを拾って入れ替える</para>
/// <para>ぶつかっている割り当ては赤くし、相手の名前と「相手の方を外す」ボタンを出す</para>
/// </summary>
class InputActionTable {
public:
	// 表を描く（ページの中から毎フレーム。拾っている間は、案内のモーダルも出す）
	void Draw(InputActionMap& map);
	// 拾う係を動かし、拾えたら map に入れる（毎フレーム1回、フレームの頭に。表の Update の前）
	// <para>入力を読めるのはフレームの頭（WindowManager::UpdateAll の中）だけなので、Draw の中では動かさない</para>
	// <para>前のフレームに表を描いていなければ（ページを閉じた・ほかのページにした）、拾うのをやめる</para>
	void UpdateListening(InputActionMap& map);
	// 割り当てを拾っているか（その間、表の SetEnabled(false) にしておく）
	bool IsListening() const { return rebinder_.IsListening(); }

private:
	// Action 1行（名前・割り当て・既定に戻す）
	void DrawAction(InputActionMap& map, InputActionId id);
	// 割り当て1つのボタン（押すと拾い始める。右の × で外す）。外したら true（後ろが詰まるので、その行の残りを描かない）
	bool DrawBinding(InputActionMap& map, InputActionId id, int index);
	// ぶつかっている相手（行の下に出す）
	void DrawConflicts(InputActionMap& map, InputActionId id, int index);
	// 拾っている間の案内（モーダル。ほかを触れないようにして、押したクリックが別の所に効かないように）
	void DrawListening(const InputActionMap& map);

	InputRebinder rebinder_;
	int lastDrawFrame_ = -1; // 最後に表を描いたフレーム（ImGui::GetFrameCount）
};
