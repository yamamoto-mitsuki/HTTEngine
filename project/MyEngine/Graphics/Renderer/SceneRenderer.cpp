#include "SceneRenderer.h"
#include "MyEngine/Camera/Camera.h"
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/Graphics/GPU/DirectXCommon.h"
#include "MyEngine/Graphics/GPU/GpuResources.h"
#include "MyEngine/Graphics/Profiling/GPUScope.h"
#include "MyEngine/Graphics/RenderTarget/RenderTexture.h"
#include "MyEngine/Graphics/RenderTarget/RenderWindow.h"
#include "MyEngine/Graphics/Renderer/GameScreen.h"
#include "MyEngine/Graphics/Renderer/RenderQueue.h"
#include "MyEngine/Scene/Skybox.h"

std::unique_ptr<SceneRenderer> SceneRenderer::instance_ = nullptr;
namespace {
static constexpr uint32_t kMaxViewsPerFrame = 4;
static constexpr uint32_t kCameraSlots = 8;
static constexpr uint32_t kCameraSlotSize = 256;
} // namespace

//=============================================================================
// 初期化 / 解放
//=============================================================================
// ===== 初期化 =====
void SceneRenderer::Initialize() {
	MY_ASSERT_MSG(instance_ == nullptr, "Initialize()が2回以上呼ばれています");
	instance_ = std::make_unique<SceneRenderer>(ConstructorKey());
	// GPUに送るカメラ構造体をMapして作成
	instance_->cameraCB_ = GpuResources::CreateMappedUploadBuffer(kCameraSlotSize * kMaxViewsPerFrame, reinterpret_cast<void**>(&instance_->cameraCBMapped_));
	// 天球生成。初期化はまだ
	instance_->skybox_ = std::make_unique<Skybox>();
	LogManager::Log("Initialized");
}

// ===== 天球 =====
void SceneRenderer::InitializeSkybox() { instance_->skybox_->Initialize(); }

// ===== ポストエフェクト =====
void SceneRenderer::InitializePost(uint32_t width, uint32_t height) {
	// 3D を描く置き場。HDR（1 を超える明るさを残す。3D の PSO の描画先の形式と同じ）。被写界深度が深度を読むので、深度を読める形で作る
	instance_->postRT_ = std::make_unique<RenderTexture>(width, height, RenderTextureFormat::HDR, true, true);
	instance_->postProcess_ = std::make_unique<PostProcess>();
	instance_->postProcess_->Initialize(width, height);
}

// ===== 解放 =====
void SceneRenderer::Release() { instance_.reset(); }

//=============================================================================
// ビューごとの設定
//=============================================================================
// ===== このビューだけの設定を覚える =====
void SceneRenderer::ApplyViewOptions(const SceneViewOptions& options) {
	instance_->viewOptions_ = options;
	RenderQueue::SetLayerVisible(DrawLayer::EditorGrid, options.editorGrid); // グリッドは描く物を1回だけ積み、ビューごとに飛ばす
	RenderQueue::SetLayerVisible(DrawLayer::EditorGizmo, options.editorGizmo); // ギズモは描く物を1回だけ積み、ビューごとに飛ばす
	RenderQueue::SetSceneHiddenVisible(options.sceneHidden); // 目で隠した物も同じ（積むのは1回、Scene ビューだけ飛ばす）
}

// ===== このビューに掛けるカメラの上書き =====
PostOverrides SceneRenderer::GetViewPostOverrides() { return instance_->viewOptions_.cameraVolume ? instance_->cameraPostOverrides_ : PostOverrides{}; }

// ===== このビューに掛ける被写界深度 =====
DepthOfFieldSettings SceneRenderer::GetViewDepthOfField() {
	DepthOfFieldSettings settings = instance_->depthOfField_;
	const PostOverrides camera = GetViewPostOverrides();
	settings.focusOverride = camera[PostParam::FocusDistance];
	settings.focusRange = camera.Apply(PostParam::FocusRange, settings.focusRange);
	settings.farTransition = camera.Apply(PostParam::FarTransition, settings.farTransition);
	settings.nearTransition = camera.Apply(PostParam::NearTransition, settings.nearTransition);
	// 全体で切ってある被写界深度は「ぼけ 0」として混ぜる（Max Blur を上書きしたカメラの間だけ掛かる）
	settings.maxBlur = camera.Apply(PostParam::MaxBlur, settings.enabled ? settings.maxBlur : 0.0f);
	if (camera.HasAny({PostParam::MaxBlur})) {
		settings.enabled = settings.maxBlur > 0.0f;
	}
	settings.enabled = settings.enabled && instance_->viewOptions_.depthOfField;
	return settings;
}

//=============================================================================
// Viewport と Scissorの設定
//=============================================================================
void SceneRenderer::SetViewportAndScissor(const ViewRect& rect) {
	D3D12_VIEWPORT vp{rect.x, rect.y, rect.width, rect.height, 0.0f, 1.0f};
	D3D12_RECT sc{static_cast<LONG>(rect.x), static_cast<LONG>(rect.y), static_cast<LONG>(rect.x + rect.width), static_cast<LONG>(rect.y + rect.height)};
	auto* cmdList = DirectXCommon::GetCommandList();
	cmdList->RSSetViewports(1, &vp);
	cmdList->RSSetScissorRects(1, &sc);
}

//=============================================================================
// シーン描画の本体
//=============================================================================
// ===== 3D。HDR の postRT_ へ描き、被写界深度まで掛けた絵を返す =====
// 3D の PSO は HDR の絵に描く形なので、ポストエフェクトを切ったビューもここを通る（その後、トーンマップだけ掛けて画面へ）
// 半透明・線・パーティクルは、被写界深度でぼかした後に描く（深度を書かないので、深度でぼけ方を決められない。
// 先に描くと、後ろにある物（空など）と同じだけぼける。Unreal の半透明の「Render After DOF」と同じ考え）
RenderTexture* SceneRenderer::RenderWorldForPost(const Camera* camera, const std::wstring& windowTitle) {
	RenderTexture* work = instance_->postRT_.get();
	const ViewRect workRect = {0.0f, 0.0f, static_cast<float>(work->GetWidth()), static_cast<float>(work->GetHeight())};
	const DepthOfFieldSettings depthOfField = GetViewDepthOfField();
	D3D12_GPU_VIRTUAL_ADDRESS camCB = camera ? UploadCameraCB(camera) : 0;

	// --- 不透明な物と空 ---
	work->PreDraw();
	SetViewportAndScissor(workRect);
	RenderOpaque(camera, camCB, windowTitle);
	// --- 被写界深度を掛けないなら、そのまま半透明も描く ---
	if (!depthOfField.enabled || camera == nullptr) {
		RenderTransparent(camCB, windowTitle);
		work->PostDraw();
		return work;
	}
	// --- 被写界深度：ぼかした絵へ、元の深度で前後を比べながら半透明を描く ---
	work->PostDraw();
	RenderTexture* blurred = instance_->postProcess_->ApplyDepthOfField(*work, *camera, depthOfField);
	SetViewportAndScissor(workRect);
	RenderTransparent(camCB, windowTitle);
	blurred->PostDraw();
	return blurred;
}

// ===== 不透明な物と空 =====
void SceneRenderer::RenderOpaque(const Camera* camera, D3D12_GPU_VIRTUAL_ADDRESS camCB, const std::wstring& windowTitle) {
	auto* cmdList = DirectXCommon::GetCommandList();
	RenderQueue::FlushOpaqueMesh(windowTitle, camCB); // 不透明Model（関数内で計測済み）
	if (instance_->skybox_) {
		GPU_SCOPE(cmdList, "Skybox");
		instance_->skybox_->Draw(camera, camCB);
	}
	// UseDepthOfFieldの粒は、ここ（被写界深度の前）で描いて一緒にぼかす。深度は書かないので、ぼけ方は粒の後ろにある物の距離で決まる
	{
		GPU_SCOPE(cmdList, "Particle (Before DOF)");
		RenderQueue::FlushParticle(windowTitle, camCB, true);
	}
}

// ===== 深度を書かない物（半透明・線・パーティクル）=====
void SceneRenderer::RenderTransparent(D3D12_GPU_VIRTUAL_ADDRESS camCB, const std::wstring& windowTitle) {
	auto* cmdList = DirectXCommon::GetCommandList();
	RenderQueue::FlushTransparentMesh(windowTitle, camCB); // 半透明Model（関数内で計算済み）
	{
		GPU_SCOPE(cmdList, "Line");
		RenderQueue::FlushLine(windowTitle, camCB);
	}
	{
		GPU_SCOPE(cmdList, "Particle");
		RenderQueue::FlushParticle(windowTitle, camCB, false); // 普通の粒（被写界深度の後。ぼけない）
	}
}

// ===== 2D。ポストエフェクトの後に重ねるので、ぼけたり歪んだりしない =====
void SceneRenderer::RenderUI(const std::wstring& windowTitle, const ViewRect& rect) {
	auto* cmdList = DirectXCommon::GetCommandList();
	SetViewportAndScissor(rect);
	GPU_SCOPE(cmdList, "2D");
	RenderQueue::Flush2d(windowTitle);
}

//=============================================================================
// シーン一括描画
//=============================================================================
// ===== Texture =====
void SceneRenderer::RenderToTexture(const Camera* camera, RenderTexture* target, const std::wstring& windowTitle, PostEffectMode mode, const SceneViewOptions& options) {
	MY_ASSERT_MSG(instance_->postProcess_ != nullptr, "InitializePost を先に呼んでください（3D は HDR の絵に描いてから画面へ写すので、その置き場が要る）");
	// ポストエフェクトを掛けないビューは、被写界深度も掛けない（ブルーム・レンズは下で外す）
	SceneViewOptions viewOptions = options;
	if (mode == PostEffectMode::None) {
		viewOptions.depthOfField = false;
	}
	ApplyViewOptions(viewOptions);
	const ViewRect rect = {0.0f, 0.0f, static_cast<float>(target->GetWidth()), static_cast<float>(target->GetHeight())};
	// 一旦 HDR の絵へ描き、光の層を作ってから target へ合成する（露出・トーンマップで 0〜1 に収める）
	RenderTexture* scene = RenderWorldForPost(camera, windowTitle);
	instance_->postProcess_->Prepare(*scene, GetViewPostOverrides(), mode != PostEffectMode::None); // 張り替える処理はここで全部済ませる
	target->PreDraw();                                                      // その後に描画先を張る
	instance_->postProcess_->Composite(rect, mode == PostEffectMode::All);
	RenderUI(windowTitle, rect); // UIは効果の外側に重ねる
	target->PostDraw();
}

// ===== Window =====
void SceneRenderer::RenderToWindow(const Camera* camera, RenderWindow* window, const std::wstring& windowTitle, float windowWidth, float windowHeight) {
	MY_ASSERT_MSG(instance_->postProcess_ != nullptr, "InitializePost を先に呼んでください（3D は HDR の絵に描いてから画面へ写すので、その置き場が要る）");
	ApplyViewOptions({}); // ウィンドウ（製品版のゲーム）は全部掛ける
	RenderTexture* scene = RenderWorldForPost(camera, windowTitle);

	// 効果のパスでRTVが切り替わっているので、ウィンドウへ張り直す
	instance_->postProcess_->Prepare(*scene, GetViewPostOverrides());
	window->BindRenderTarget(); // 描画先はウィンドウ
	// ウィンドウの中の、ゲームの比率の四角にだけ描く（外は帯の色で塗る）
	const ViewRect rect = GameScreen::CalcFitRect(windowWidth, windowHeight);
	window->ClearBars(rect, windowWidth, windowHeight);
	instance_->postProcess_->Composite(rect);
	RenderUI(windowTitle, rect); // UIは効果の外側に重ねる
}

//=============================================================================
// GPU転送用のカメラを更新
//=============================================================================
// ===== UploadCameraCB：% をやめて Assert =====
D3D12_GPU_VIRTUAL_ADDRESS SceneRenderer::UploadCameraCB(const Camera* camera) {
	MY_ASSERT_MSG(instance_->cameraSlot_ < kMaxViewsPerFrame, "1フレームのビュー数が kMaxViewsPerFrame を超えました");
	uint32_t slot = instance_->cameraSlot_++;
	auto* dst = reinterpret_cast<CameraData*>(instance_->cameraCBMapped_ + slot * kCameraSlotSize);
	// --- 転送情報 ---
	const Matrix4x4& v = camera->GetViewMatrix();
	dst->viewProj = v * camera->GetProjectionMatrix();
	dst->worldPosition = camera->GetTranslation();
	dst->right = {v.m[0][0], v.m[1][0], v.m[2][0]};
	dst->up = {v.m[0][1], v.m[1][1], v.m[2][1]};
	// 霧：切ってあるビュー（エディタの Scene ビュー）には「霧なし」（濃さの上限が 0）を送る
	dst->fog = instance_->viewOptions_.fog ? instance_->fog_ : FogData{};
	return instance_->cameraCB_->GetGPUVirtualAddress() + slot * kCameraSlotSize;
}