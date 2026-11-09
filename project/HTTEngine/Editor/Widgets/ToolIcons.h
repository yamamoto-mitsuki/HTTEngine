#pragma once
#include <externals/imgui/imgui.h>

// エディタの上の帯（Controlウィンドウ）に出す、絵だけのボタンの種類
enum class ToolIcon {
	Play,    // 再生（右向きの三角）
	Stop,    // 停止（四角）
	Pause,   // 一時停止（縦の棒2本）
	Restart, // やり直し（丸い矢印1本）
	Move,    // 移動の道具（4方向の矢印）
	Rotate,  // 回転の道具（丸い矢印2本）
	Scale,   // 拡縮の道具（四角と斜めの矢印）
	Local,   // ギズモの向き：物の向き（傾いた軸）
	World,   // ギズモの向き：ワールドの向き（地球）
	Snap,    // スナップ（磁石）
};


namespace ToolIcons {
// center を真ん中に、1辺 sizeの四角に入る大きさでアイコンを描く
void Draw(ImDrawList* drawList, ToolIcon icon, const ImVec2& center, float size, ImU32 color);

/// <summary>
/// 絵だけのボタン（ふつうのボタンと同じ高さの、少し横長の四角）。押したら true
/// <para>BeginDisabled の中で使うと、絵も薄くなる</para>
/// </summary>
/// <param name="id">ボタンの名前（同じウィンドウの中で、ほかのボタンとぶつからない物。画面には出ない）</param>
/// <param name="selected">押した状態の色にする（今使っている道具・再生中など）</param>
/// <param name="tooltip">マウスを乗せたときに出す説明（nullptr なら出さない）</param>
/// <param name="iconColor">絵の色（0 なら文字の色）</param>
bool Button(const char* id, ToolIcon icon, bool selected, const char* tooltip, ImU32 iconColor = 0);

// Button 1つ分の幅（並べる前に、全体の幅を測るのに使う）
float GetButtonWidth();
}