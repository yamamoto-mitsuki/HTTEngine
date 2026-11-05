#include "MyEngine/Engine.h"
#include <shobjidl.h>
// Editor
#include "MyEngine/Editor/Gizmos/LightGizmo.h"
#include "MyEngine/Editor/Gizmos/CameraGizmo.h"
#include "MyEngine/Editor/Viewport/EditorOverlay.h"
#include "MyEngine/Editor/FrameStats.h"
#include "MyEngine/Editor/Profiler.h"
// Diagnostics
#include "MyEngine/Diagnostics/CrashHandler.h"
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Diagnostics/MyAssert.h"
// UI
#include "MyEngine/UI/GlobalVariables.h"
// Sound
#include "MyEngine/Sound/SoundManager.h"
// Systems（Component を読んで処理する係）
#include "MyEngine/Systems/EngineSystems.h"
// Input
#include "MyEngine/Input/InputManager.h"
// Math
#include "MyEngine/Math/MathIncludes.h"
// Collision
#include "MyEngine/Collision/Profiling/CollisionProfiler.h"
// Curve
// Timeline
// Particle
#include "MyEngine/Particle/ParticleManager.h"
// Graphics
#include "MyEngine/Graphics/GPU/UploadContext.h"
#include "MyEngine/Graphics/IBL/IBLBaker.h"
#include "MyEngine/Graphics/Animation/AnimationManager.h"
#include "MyEngine/Graphics/Model/ModelManager.h"
#include "MyEngine/Graphics/Pipeline/PSOManager.h"
#include "MyEngine/Graphics/Pipeline/RootSignatureManager.h"
#include "MyEngine/Graphics/Pipeline/ShaderCompiler.h"
#include "MyEngine/Graphics/Pipeline/ShaderPackageLoader.h"
#include "MyEngine/Graphics/Profiling/GPUProfiler.h"
#include "MyEngine/Graphics/RenderTarget/RenderTextureManager.h"
#include "MyEngine/Graphics/Renderer/GameScreen.h"
#include "MyEngine/Graphics/Renderer/SceneRenderer.h"
#include "MyEngine/Graphics/Renderer/RenderContext.h"
#include "MyEngine/Graphics/Renderer/RenderQueue.h"
#include "MyEngine/Graphics/Renderer/Renderer.h"
#include "MyEngine/Graphics/Renderer/SkinCache.h"
#include "MyEngine/Graphics/Text/FontManager.h"
#include "MyEngine/Graphics/Texture/TextureManager.h"

// 静的メンバ変数
std::unique_ptr<Engine> Engine::instance_ = nullptr;

namespace {
constexpr uint32_t kPostLongSide = 1920; // ポストエフェクトの描画先の、長い方の辺（ピクセル。短い方はゲームの画面の比率で決まる）
} // namespace

//=============================================================================
// 初期化
//=============================================================================
void Engine::Initialize(const WindowConfig& config, SceneFactory initialScene) {
	MY_ASSERT_MSG(initialScene != nullptr, "シーンの作り方(initialScene)が渡されていません");
	// COM（Component Object Model）の初期化
	HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
	if (FAILED(hr)) {
		LogManager::Error(std::format("Error Code: 0x{:08X}", (uint32_t)hr));
		MY_ASSERT_MSG(SUCCEEDED(hr), "COMの初期化に失敗しました");
	}
	// 通知を出すためのID登録
	SetCurrentProcessExplicitAppUserModelID(L"MyEngine");
	GameNotification::Initialize();
	// クラッシュハンドラの登録
	SetUnhandledExceptionFilter(ExportDump);

	// インスタンス生成
	instance_ = std::make_unique<Engine>(ConstructorKey());

	// ログ初期化
	LogManager::Initialize();
	// ゲームの画面の大きさ（カメラ・描画先・UI の基準が読むので、それらより先に）
	GameScreen::Initialize(config.gameWidth, config.gameHeight);
	// DirectX初期化
	DirectXCommon::Initialize();
	UploadContext::Initialize();
	RenderTextureManager::Initialize();
	TextureManager::Initialize();
	FontManager::Initialize();
	ModelManager::Initialize();
	AnimationManager::Initialize();
	// 各初期化
#ifdef USE_IMGUI
	EditorOverlay::Initialize();
#endif
	Time::Initialize();
	GPUProfiler::Initialize();
	CollisionProfiler::Initialize();
	RenderContext::Initialize();
	RenderQueue::Initialize();
	Renderer::Initialize();
	SkinCache::Initialize();
	SceneRenderer::Initialize();
	SoundManager::Initialize();
	PSOManager::Initialize();
	RootSignatureManager::Initialize();
	ShaderPackageLoader::Initialize();
	ShaderCompiler::Initialize();
	RandomEngine::Initialize();
	ParticleManager::Initialize();

	// ギズモ
#ifdef USE_IMGUI
	LightGizmo::Initialize();
	CameraGizmo::Initialize();
#endif
	EngineSystems::Initialize(); // EntityManager と全 System（Component の登録もここ）

	ShaderPackageLoader::LoadAll(ShaderPackageLoader::FindDataDir()); // .hlsl生成
	ShaderPackageLoader::LoadProject(kProjectShaderDir);             // ゲームの resources の下の .shader（エンジンの .hlsli を include できるように、エンジンの後）

	IBLBaker::Initialize(); // IBL
	SceneRenderer::InitializeSkybox(); // 天球初期化
	const RenderSize postSize = GameScreen::CalcRenderSize(kPostLongSide);
	SceneRenderer::InitializePost(postSize.width, postSize.height); // ブルーム初期化
	instance_->windowManager_.AddWindow(config, std::move(initialScene)); // ウィンドウ生成                                 
	InputManager::Initialize(GetModuleHandle(nullptr), instance_->windowManager_.GetMainHWND()); // InputManager初期化
	GlobalVariables::GetInstance()->LoadFiles(); // 調整項目適用
	instance_->lastTime_ = std::chrono::high_resolution_clock::now(); // lastTime_ を現在時刻で初期化
}


//=============================================================================
// 初期化（最初のシーンを名前で）
//=============================================================================
void Engine::Initialize(const WindowConfig& config, const std::string& firstScene) {
#ifdef USE_IMGUI
	// エディタは、前回最後に開いていたシーンから始める（Unity と同じ。覚えていなければ firstScene）
	const std::string sceneName = SceneManager::GetLastOpenedScene(firstScene);
#else
	// 製品版は、いつも firstScene から
	const std::string sceneName = firstScene;
#endif
	// 名前からシーンを作る「作り方」を渡す（Stop・Restart でも、この作り方で開いているシーンを作り直す）
	Initialize(config, [sceneName] { return SceneManager::CreateScene(sceneName); });
}


//=============================================================================
// ウィンドウが受け取るメッセージ
//=============================================================================
bool Engine::ProcessMessage() { return instance_->windowManager_.ProcessMessage(); }


//=============================================================================
// フレームの最初に行う処理
//=============================================================================
void Engine::BeginFrame() {
	InputManager::Update(); // 操作入力
	SoundManager::CleanupSourceVoices(Time::GetUnscaleDeltaTime()); // サウンド
	// deltaTime の計算
	auto now = std::chrono::high_resolution_clock::now();
	float dt = std::chrono::duration<float>(now - instance_->lastTime_).count();
	// 重い処理やブレークポイントで止めて再開したときに巨大なdeltaTimeになるのを防ぐためにクランプ
	if (dt > Time::kMaxDeltaTime) {
		dt = Time::kMaxDeltaTime;
	}
	// 今の時刻を保存しておく（次のフレームで使う）
	Time::SetDeltaTime(dt);
	instance_->lastTime_ = now;
	// Updateの処理時間を計測
	instance_->updateStart_ = std::chrono::high_resolution_clock::now();
}


//=============================================================================
// フレームの最後に行う処理
//=============================================================================
void Engine::EndFrame() {
#ifdef USE_IMGUI
	// Updateの処理時間を確定
	auto updateEnd = std::chrono::high_resolution_clock::now();
	float updateMs = std::chrono::duration<float, std::milli>(updateEnd - instance_->updateStart_).count();
	Profiler::Category(ProfCategory::CPU).Group("Times").Value("Update", updateMs, "ms");

	// Renderの処理時間を計測（CPU時間）
	auto renderStart = std::chrono::high_resolution_clock::now();
	// GPU時間での測定を開始
	GPUProfiler::BeginFrame();
#endif

	// Render
	instance_->windowManager_.PreRenderAll();
	instance_->windowManager_.PostRenderAll();

#ifdef USE_IMGUI
	// Renderの処理時間を確定（CPU）
	auto renderEnd = std::chrono::high_resolution_clock::now();
	float renderMs = std::chrono::duration<float, std::milli>(renderEnd - renderStart).count();
	Profiler::Category(ProfCategory::CPU).Group("Times").Value("Render", renderMs, "ms");
	// 統計（GPU の時間・FPS・メモリ・Draw Call など）
	FrameStats::Report();
#endif
}


//=============================================================================
// 終了処理
//=============================================================================
void Engine::Finalize() {
	EngineSystems::ReleaseScripts(); // 動いている Script を、ほかの物を片付ける前に片付ける
	CollisionProfiler::Release();
	GPUProfiler::Release();
	ShaderCompiler::Release();
	ShaderPackageLoader::Release();
	PSOManager::Release();
	RootSignatureManager::Release();
	ParticleManager::Release();
#ifdef USE_IMGUI
	EditorOverlay::Release();
#endif
	SceneRenderer::Release();
	instance_->windowManager_.Finalize();

	// --- ウィンドウ（シーン・エディタの画面）が使っている物は、ウィンドウを消した後に片付ける ---
	RenderTextureManager::Finalize();
	RenderContext::Release();
	UploadContext::Release();
	TextureManager::Release();
	FontManager::Release();
	ModelManager::Release();
	SoundManager::Release();
	InputManager::Release();

	EngineSystems::Release(); // シーン（ウィンドウ）の破棄より後にする
	// --- 今まで Release を呼んでいなかった物（作りっぱなしで、終わるときに消していなかった）---
	AnimationManager::Release();
	IBLBaker::Finalize();
	RenderQueue::Release();
	Renderer::Release();
	SkinCache::Release();
	Time::Release();
	DirectXCommon::Release(); // GPUの物（テクスチャ・RenderTextureなど）を全部消した後、一番最後に
	instance_.reset();
	LogManager::Flush();
	LogManager::Shutdown();
	CoUninitialize();
}