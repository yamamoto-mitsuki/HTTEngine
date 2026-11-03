#include <MyEngine/Engine.h>


//=============================================================================
// エンジンの起動確認・機能テスト用の exe（Sandbox）
//=============================================================================
// ===== 入口 =====
int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
	// --- ウィンドウの設定 ---
	WindowConfig config;
	config.title = L"MyEngine Sandbox"; // タイトルバーの文字
	config.width = 1280;                // ウィンドウの幅（ピクセル）
	config.height = 720;                // ウィンドウの高さ（ピクセル）
#ifdef USE_IMGUI
	config.isImGui = true; // Debug・Development はエディタ（ImGui）を出す
#endif

	// --- 起動 ---
	// 最初のシーンは resources/scenes/SampleScene.scene.json
	// エディタ（Debug・Development）は、前回最後に開いていたシーンから始まる（Parameters/Editor/LastScene.json）
	Engine::Initialize(config, "SampleScene");

	// --- ゲームループ ---
	while (Engine::ProcessMessage()) {
		Engine::BeginFrame();
		Engine::GetWindowManager()->UpdateAll();
		Engine::GetWindowManager()->DrawAll();
		Engine::EndFrame();
	}

	// --- 終了 ---
	Engine::Finalize();
	return 0;
}
