#pragma once
#include <string>

#include "HTTEngine/Input/Action/InputActionMap.h"
#include "HTTEngine/Input/Action/InputRebinder.h"

/// <summary>
/// Input Action の表の割り当てを直す ImGui の表（Settings の Keymap ページ・Input ページ。表1つに1つ持つ）
/// <para>Action を group ごとに見出しを付けて並べ、割り当てをボタンで出す。行の左端の ＋ で足す</para>
/// <para>割り当てを押す（右クリック →「変更」でも）と、次に押したキー・ボタンを拾って入れ替える。右クリック →「消す」で外す</para>
/// <para>行の空いている所・Action の名前を右クリックすると「足す」「既定に戻す」（× や「既定」のボタンは置かない。並びを見やすくするため）</para>
/// <para>Value・Vector2 は、割り当てを行に分けて出す（Vector2 なら 上・下・左・右 のキー・ボタンと、横・縦 の軸。InputBindingRow）。行の ＋ は、その行の向きで拾う</para>
/// <para>ぶつかっている割り当ては赤くし、相手の名前と「相手の方を外す」ボタンを出す</para>
/// </summary>
class InputActionTable {
public:
	// resetLabel は、右クリックのメニューの「既定に戻す」の文字（Input ページでは「保存した割り当てに戻す」）
	explicit InputActionTable(std::string resetLabel = "既定に戻す") : resetLabel_(std::move(resetLabel)) {}

	// 表を描く（ページの中から毎フレーム。拾っている間は、案内のモーダルも出す）
	void Draw(InputActionMap& map);
	// 拾う係を動かし、拾えたら map に入れる（毎フレーム1回、フレームの頭に。表の Update の前）
	// <para>入力を読めるのはフレームの頭（WindowManager::UpdateAll の中）だけなので、Draw の中では動かさない</para>
	// <para>前のフレームに表を描いていなければ（ページを閉じた・ほかのページにした）、拾うのをやめる</para>
	void UpdateListening(InputActionMap& map);
	// 割り当てを拾っているか（その間、表の SetEnabled(false) にしておく）
	bool IsListening() const { return rebinder_.IsListening(); }
	// 拾うのをやめる（表の Action を足す・消すなど、番号が変わるとき）
	void CancelListening() { rebinder_.Cancel(); }

private:
	// Action 1つ（行ごとに、名前と割り当て）
	void DrawAction(InputActionMap& map, InputActionId id);
	// 割り当ての1行（行の名前・＋・その行の割り当て・空いている所）
	void DrawRow(InputActionMap& map, InputActionId id, InputBindingRow row);
	// 割り当て1つのボタン（押すと拾い始める。右クリックで「変更」「消す」）。消したら true（後ろが詰まるので、その行の残りを描かない）
	bool DrawBinding(InputActionMap& map, InputActionId id, int index, InputBindingRow row);
	// 右クリックのメニュー（行の空いている所・Action の名前）。row の行に「足す」、Action を「既定に戻す」
	void DrawActionMenu(InputActionMap& map, InputActionId id, InputBindingRow row, const char* popupId);
	// 行に割り当てを足し始める（行の ＋・右クリックの「足す」）
	void StartAdd(const InputActionMap& map, InputActionId id, InputBindingRow row);
	// ぶつかっている相手（行の下に出す）
	void DrawConflicts(InputActionMap& map, InputActionId id, int index);
	// 拾っている間の案内（モーダル。ほかを触れないようにして、押したクリックが別の所に効かないように）
	void DrawListening(const InputActionMap& map);

	InputRebinder rebinder_;
	std::string resetLabel_; // 右クリックのメニューの「既定に戻す」の文字
	int lastDrawFrame_ = -1; // 最後に表を描いたフレーム（ImGui::GetFrameCount）
};
