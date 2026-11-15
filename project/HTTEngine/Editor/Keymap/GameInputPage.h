#pragma once

/// <summary>
/// Settings → Project → Input のページ（ゲームの Action と既定の割り当てを直す。保存は Parameters/Input/InputActions.json）
/// <para>Action を足す・消す・並べ替える、名前・種類（Button・Value・Vector2）などを決める所と、割り当ての表（InputActionTable）</para>
/// <para>ゲームの表（GameInput）を直接は直さず、自分の表（InputGate::Editor で読む）を持つ。ゲームの表は Game の決まりで読むので、ImGui を触っている間（＝このページを触っている間）は何も拾えないため</para>
/// <para>Save でファイルに書き、GameInput に読み直させる（Play 中でも、すぐゲームに効く）</para>
/// </summary>
class GameInputPage {
public:
	// ファイルを読み、Settings にページを足す（ImGuiManager::Initialize から1回）
	static void Initialize();
	// 割り当てを拾う係を動かす（毎フレーム1回。WindowManager::UpdateAll の、ImGui のウィンドウの番。入力を読めるのはここだけ）
	static void Update();

private:
	// ページの中身
	static void Draw();
};
