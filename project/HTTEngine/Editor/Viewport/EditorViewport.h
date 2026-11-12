#pragma once
#include <memory>
#include <string>

// 前方宣言
class Camera;
class RenderWindow;
class DebugCamera;

#include "HTTEngine/Camera/DebugCamera.h"
#include "HTTEngine/Editor/Viewport/EditorGrid.h"
#include "HTTEngine/Editor/Viewport/ViewportWindow.h"
#include "HTTEngine/Graphics/RenderTarget/RenderTextureManager.h"


/// <summary>
/// エディタの編集ビュー（Game / Scene の2画面）を1ウィンドウ分まとめて持つ。
/// <para>描画は SceneRenderer に任せ、結果を ViewportWindow で表示する。</para>
/// </summary>
class EditorViewport {
public:
	// RT2枚の確保・デバッグカメラ初期化
	void Initialize();
	// 入力
	void Update();
	// グリッドなどEditorのモデルなどの描画リクエストを送る
	void Draw(Camera* camera);
	// gameCamera視点でGame、debugCamera視点でSceneを描いて表示する
	void Render(const Camera* gameCamera, const std::wstring& windowTitle);

#ifdef USE_IMGUI
	// View メニューで切り替える、ビューごとの見え方（全ウィンドウ共通。メニュー・保存は ViewSettings。ここに足したら ViewSettings の一覧にも足す）
	struct ViewFlags {
		bool sceneEffects = true;      // Scene ビューにもポストエフェクトを掛ける（レンズは Scene では掛けない）
		bool sceneFog = true;          // Scene ビューにも霧（Fog Component）を掛ける。Game ビューにはいつも掛ける
		bool sceneSkybox = false;      // Scene ビューに天球を描く（初めは外す＝落ち着いたグラデーションでギズモを見やすく。Game ビューはカメラの Background のまま）
		bool sceneDepthOfField = true; // Scene ビューにも被写界深度（Depth Of Field Component）を掛ける（Post Effects が入っているときだけ）
		bool sceneGrid = true;         // Scene ビューにグリッドを描く
		bool gameGrid = false;         // Game ビューにグリッドを描く（遊ぶときと同じ見た目にしたいので、初めは描かない）
		bool scenePickUI = true;       // Scene ビューで、画面に重ねる UI（Image・Text）をクリックで選ぶ（外すと、UI の後ろの 3D の物を選べる）
	};
#endif

	// ===== ゲッター =====
#ifdef USE_IMGUI
	DebugCamera* GetDebugCamera() { return debugCamera_.get(); }
	bool IsGameHovered() const { return gameView_.IsHovered(); }
	bool IsSceneHovered() const { return sceneView_.IsHovered(); }
	// View メニューの印（ViewSettings が、メンバーのアドレスを覚えて切り替える・保存する）
	static ViewFlags& GetViewFlags();
#endif

private:
#ifdef USE_IMGUI
	// Scene ビューのカメラの操作の量（EditorKeymap の割り当てとマウスの動きから）
	static DebugCamera::Input ReadCameraInput();
#endif
	// --- 調整項目 ---
	void RegisterGV(); // 登録
	void ApplyGV();    // 保存値の反映
	void SyncGV();     // 毎フレームの同期
#ifdef USE_IMGUI
	// Game ビューの絵の左上に、OS のカーソルを閉じ込める・放すの案内を出す（gameView_.Draw の中から。閉じ込めた直後の数秒と、Shift+F1 で放している間）
	void DrawCursorConfineHint();
	double cursorConfineStartTime_ = -1.0; // 閉じ込め始めた時刻（ImGui::GetTime の秒。閉じ込めていない間は負）

	std::shared_ptr<DebugCamera> debugCamera_; // shared_ptr なのは、EditorOverlay が weak_ptr で見ているから
	Vector3 lastTranslation_{}; // 前フレームのカメラ位置（操作されたかの判定用）
	Vector3 lastRotation_{};    // 前フレームのカメラ回転（操作されたかの判定用）
	RenderTextureManager::Handle gameRT_;
	RenderTextureManager::Handle sceneRT_;
	ViewportWindow gameView_{"Game"};
	ViewportWindow sceneView_{"Scene"};
	// グリッドなど
	std::unique_ptr<EditorGrid> grid_;
	bool isShowCameraFrustum_ = true;
#endif
};