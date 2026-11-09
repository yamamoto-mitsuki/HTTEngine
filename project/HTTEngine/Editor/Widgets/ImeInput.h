#pragma once
#include <cstdint>


/// <summary>
/// 日本語入力（IME）の、変換中の文字を扱う
/// <para>Windowsが出す小さな変換窓の代わりに、ImGuiの入力欄のカーソルの所へ変換中の文字を描く（候補の一覧はWindowsのまま）</para>
/// <para>全部の入力欄に効く（Hierarchyの名前・Add Componentの検索・数値の直接入力など）</para>
/// </summary>
class ImeInput {
public:
	/// <summary>
	/// ImGuiを作った後に1回呼ぶ。候補の一覧を出す位置の決め方を、ImGuiの標準から差し替える
	/// </summary>
	static void Initialize();

	/// <summary>
	/// ウィンドウのメッセージを見て、IMEが変換中かどうかを覚える（Win32Window::WindowProcから呼ぶ）
	/// </summary>
	/// <returns>trueならImGuiに渡さない（IMEが変換の確定・取り消しに使ったEnter / Esc）</returns>
	static bool HandleMessage(unsigned int message, std::uintptr_t wparam);

	/// <summary>
	/// ImGui::NewFrameの後に1回呼ぶ。変換中の文字をIMEから読む
	/// </summary>
	static void NewFrame();

	/// <summary>
	/// 全部のウィンドウを描いた後、ImGui::Renderの前に1回呼ぶ。変換中の文字を入力欄の中に描く
	/// </summary>
	static void DrawComposition();

	// 変換中の文字があるか（入力欄が、選んでいた文字を先に消すのに使う）
	static bool IsComposing();
};