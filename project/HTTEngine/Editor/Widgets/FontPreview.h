#pragma once
#include <string>

// 前方宣言
struct ImFont;

#ifdef USE_IMGUI


/// <summary>
/// フォントの見本（Inspector の Font の選択欄で、そのフォントで文字を描くため）
/// <para>ImGui 1.92 は、描いている途中でもフォントを足せる（文字の絵は、使ったときに作る）。見本に使うフォントを、見えている行の分だけ読む</para>
/// <para>フォントのファイルは大きい（日本語なら1つ数MB〜20MB）ので、読むのは1フレームに数個まで・覚えておくのも数十個までにして、
/// しばらく使っていない物から捨てる</para>
/// </summary>
class FontPreview {
public:
	/// <summary>
	/// そのファイルのフォントを、ImGui のフォントとして返す（初めてなら読み込む）
	/// <para>このフレームにもう何個も読んだ・覚えておく数がいっぱい・読めないフォントなら nullptr（読めないかは IsFailed で分かる）</para>
	/// </summary>
	/// <param name="path">フォントのファイル（UTF-8）</param>
	static ImFont* Get(const std::string& path);

	// 読もうとして、読めなかったフォントか（フォントでないファイル・stb_truetype が読めない形）
	static bool IsFailed(const std::string& path);

	// 全部捨てる（ImGuiManager::Release で、ImGui を消す前に呼ぶ）
	static void Clear();
};

#endif