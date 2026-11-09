#pragma once

// 前方宣言
class SceneManager;


/// <summary>
/// Control ウィンドウ（エディタの上の帯）
/// <para>左：ギズモの道具（移動・回転・拡縮／軸の向き／スナップ）　真ん中：再生・一時停止・やり直し　右：保存</para>
/// <para>下の行：今の状態と、開いているシーン</para>
/// </summary>
class ControlWindow {
public:
	// 毎フレーム1回（WindowManager::PreRenderAll から。ImGuiManager::Begin の後）
	static void Draw(SceneManager* sceneManager);

private:
	// 左：ギズモの道具
	static void DrawGizmoTools();
	// 真ん中：再生・一時停止・やり直し
	static void DrawPlayButtons(SceneManager& sceneManager);
	// 右：保存（Ctrl+S も）
	static void DrawSaveButton(SceneManager& sceneManager);
	// 下の行：今の状態とシーンの名前
	static void DrawStatus(SceneManager& sceneManager);
};