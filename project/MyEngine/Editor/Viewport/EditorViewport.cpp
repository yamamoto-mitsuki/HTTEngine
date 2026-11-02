#ifdef USE_IMGUI
#include "EditorViewport.h"

#include "MyEngine/Camera/Camera.h"
#include "MyEngine/UI/GlobalVariables.h"
#include "MyEngine/Editor/Gizmos/SceneGizmo.h"
#include "MyEngine/Editor/Viewport/EditorOverlay.h"
#include "MyEngine/Graphics/RenderTarget/RenderTexture.h"
#include "MyEngine/Graphics/RenderTarget/RenderWindow.h"
#include "MyEngine/Graphics/Renderer/GameScreen.h"
#include "MyEngine/Graphics/Renderer/SceneRenderer.h"
#include "MyEngine/Input/InputManager.h"

namespace {
constexpr const char* kGVGroup = "Scene View"; // ImGui登録グループ名
} // namespace


//=============================================================================
// 初期化
//=============================================================================
void EditorViewport::Initialize() {
	// Game / Scene それぞれの描画先（SDR・深度あり）。大きさはゲームの画面と同じ（UI の 1 ピクセルが絵の 1 ピクセル）
	const uint32_t viewWidth = static_cast<uint32_t>(GameScreen::GetWidth());
	const uint32_t viewHeight = static_cast<uint32_t>(GameScreen::GetHeight());
	gameRT_ = RenderTextureManager::Create(viewWidth, viewHeight, RenderTextureFormat::SDR);
	sceneRT_ = RenderTextureManager::Create(viewWidth, viewHeight, RenderTextureFormat::SDR);
	// デバッグカメラ（自由視点）
	debugCamera_ = std::make_shared<DebugCamera>();
	debugCamera_->Initialize(0.45f, GameScreen::GetAspectRatio(), 0.1f, 500.0f);
	debugCamera_->SetTranslation({0.0f, 0.0f, -30.0f});
	// グリッド
	grid_ = std::make_unique<EditorGrid>();
	grid_->Initialize();

	// 現在値を初期値として登録 → 保存済みjsonがあれば読み込んでカメラへ反映
	RegisterGV();
	ApplyGV();
}


//=============================================================================
// 更新
//=============================================================================
void EditorViewport::Update() {
	// Sceneビューにあるときだけ操作
	if (sceneView_.IsHovered()) {
		debugCamera_->Update();
	}
	// ImGuiの調整項目と同期
	SyncGV();
}

//=============================================================================
// グリッドなどEditorのモデルなどの描画リクエストを送る
//=============================================================================
void EditorViewport::Draw(Camera* gameCamera) {
#ifdef USE_IMGUI
	// どちらかのビューで描くなら積む（描かないビューでは、RenderQueue が飛ばす）
	const ViewFlags& flags = GetViewFlags();
	if (flags.sceneGrid || flags.gameGrid) {
		grid_->Draw();
	}
	if (isShowCameraFrustum_ && gameCamera) {
		gameCamera->DrawFrustum(debugCamera_.get());
	}
#else
	(void)gameCamera;
#endif
}

//=============================================================================
// 描画（1ウィンドウ分の Game / Scene 2画面）
//=============================================================================
void EditorViewport::Render(const Camera* gameCamera, const std::wstring& windowTitle) {
	RenderTexture* gameRT = RenderTextureManager::Get(gameRT_);
	RenderTexture* sceneRT = RenderTextureManager::Get(sceneRT_);

	// --- 同じキューを2カメラで2回描く ---
	const ViewFlags& flags = GetViewFlags();
	// Scene はレンズだけ外す（絵を歪ませると、ギズモ・クリックで選ぶ位置が物とずれる）。View メニューで全部切れる
	const PostEffectMode sceneMode = flags.sceneEffects ? PostEffectMode::NoLens : PostEffectMode::None;
	// 霧・被写界深度も Scene だけ切れる（遠くの物を霧やぼけに隠さずに置きたいとき）。グリッドは Scene・Game 別々に切れる
	const SceneViewOptions sceneOptions = {.fog = flags.sceneFog, .depthOfField = flags.sceneDepthOfField, .editorGrid = flags.sceneGrid, .editorGizmo = true, .sceneHidden = false, .cameraVolume = false};
	const SceneViewOptions gameOptions = {.editorGrid = flags.gameGrid}; // 書かない物は初期値の true（霧・被写界深度はいつも掛ける）
	SceneRenderer::RenderToTexture(debugCamera_.get(), sceneRT, windowTitle, sceneMode, sceneOptions); // Scene：レンズ以外
	SceneRenderer::RenderToTexture(gameCamera, gameRT, windowTitle, PostEffectMode::All, gameOptions); // Game：全部

	// --- Game ---
	gameView_.Draw(gameRT);
	// ゲームのマウス座標（InputManager::GetMousePositionInGame）は、この絵の中を画面として数える
	const ImVec2& gameMin = gameView_.GetImageMin();
	const ImVec2& gameSize = gameView_.GetImageSize();
	InputManager::SetGameViewRect({gameMin.x, gameMin.y}, {gameSize.x, gameSize.y});

	// --- Scene ---
	sceneView_.Draw(sceneRT, [this] {
		// Editor（右上の軸。ImGuizmo の準備もここでする）
		EditorOverlay::SetActiveCamera(debugCamera_);
		EditorOverlay::Draw(sceneView_.GetImageMin(), sceneView_.GetImageMax(), sceneView_.GetImageSize());
		// クリックで選ぶ・選んだ物のギズモ（EditorOverlay::Draw の後）
		SceneGizmo::Draw(*debugCamera_, sceneView_.GetImageMin(), sceneView_.GetImageSize(), sceneView_.IsHovered());
	});

	// どちらかのビュー上なら入力をImGuiに食わせない
	InputManager::SetMouseInViewport(gameView_.IsHovered() || sceneView_.IsHovered());
}

//=============================================================================
// View メニューの印
//=============================================================================
EditorViewport::ViewFlags& EditorViewport::GetViewFlags() {
	static ViewFlags flags; // 全ウィンドウ共通（初期値は ViewFlags に書いた値）
	return flags;
}

//=============================================================================
// 調整項目
//=============================================================================
// ===== 登録 =====
void EditorViewport::RegisterGV() {
	GlobalVariables::GetInstance()
	    ->Group(kGVGroup, SettingsSection::Editor)
	    .Category("DebugCamera")
	    .Add<Vector3>("Translation", debugCamera_->GetTranslation())
	    .Add<Vector3>("Rotation", debugCamera_->GetRotation())
	    .Add<float>("FovY", debugCamera_->GetFovY())
	    .Add<float>("NearZ", debugCamera_->GetNearZ())
	    .Add<float>("FarZ", debugCamera_->GetFarZ());
}

// ===== 保存値をデバッグカメラへ一括反映（起動時に1回） =====
void EditorViewport::ApplyGV() {
	GlobalVariables* gv = GlobalVariables::GetInstance();
	// 投影（アスペクト比はウィンドウ側の値をそのまま使う）
	debugCamera_->Initialize(
	    gv->Get<float>(kGVGroup, "DebugCamera", "FovY"), 
		debugCamera_->GetAspectRatio(), 
		gv->Get<float>(kGVGroup, "DebugCamera", "NearZ"), 
		gv->Get<float>(kGVGroup, "DebugCamera", "FarZ"));

	// 前回終了時の視点
	debugCamera_->SetTranslation(gv->Get<Vector3>(kGVGroup, "DebugCamera", "Translation"));
	debugCamera_->SetRotation(gv->Get<Vector3>(kGVGroup, "DebugCamera", "Rotation"));
	debugCamera_->ReCalcViewMatrix();

	lastTranslation_ = debugCamera_->GetTranslation();
	lastRotation_ = debugCamera_->GetRotation();
}

// ===== 毎フレームの同期 =====
void EditorViewport::SyncGV() {
	GlobalVariables* gv = GlobalVariables::GetInstance();

	// --- 投影は変化したときだけ作り直す ---
	float fovY = gv->Get<float>(kGVGroup, "DebugCamera", "FovY");
	float nearZ = gv->Get<float>(kGVGroup, "DebugCamera", "NearZ");
	float farZ = gv->Get<float>(kGVGroup, "DebugCamera", "FarZ");
	if (fovY != debugCamera_->GetFovY() || nearZ != debugCamera_->GetNearZ() || farZ != debugCamera_->GetFarZ()) {
		debugCamera_->Initialize(fovY, debugCamera_->GetAspectRatio(), nearZ, farZ);
	}

	// --- 位置・回転 ---
	if (debugCamera_->GetTranslation() != lastTranslation_ || debugCamera_->GetRotation() != lastRotation_) {
		// ビューポート操作で動いた → ImGuiの表示を追従させる（この状態でSaveすれば今の視点が保存される）
		gv->Set<Vector3>(kGVGroup, "DebugCamera", "Translation", debugCamera_->GetTranslation());
		gv->Set<Vector3>(kGVGroup, "DebugCamera", "Rotation", debugCamera_->GetRotation());
	} else {
		// カメラは動いていない → ImGuiで編集された値をカメラへ反映する
		debugCamera_->SetTranslation(gv->Get<Vector3>(kGVGroup, "DebugCamera", "Translation"));
		debugCamera_->SetRotation(gv->Get<Vector3>(kGVGroup, "DebugCamera", "Rotation"));
		debugCamera_->ReCalcViewMatrix();
	}
	lastTranslation_ = debugCamera_->GetTranslation();
	lastRotation_ = debugCamera_->GetRotation();
}
#endif