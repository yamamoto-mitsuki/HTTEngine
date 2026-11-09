#include "WindowManager.h"
#include <array>
#include <format>
#include <externals/imgui/imgui.h>
#include <externals/imgui/ImGuizmo.h>
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/String/ConvertString.h"
#include "MyEngine/Input/InputManager.h"
#include "MyEngine/Editor/Viewport/EditorOverlay.h"
#include "MyEngine/Editor/Viewport/ViewportWindow.h"
#include "MyEngine/Scene/IScene.h"
#include "MyEngine/Systems/EngineSystems.h"
#include "MyEngine/Graphics/Profiling/GPUProfiler.h"
#include "MyEngine/Graphics/Renderer/Renderer.h"
#include "MyEngine/Graphics/GPU/DirectXCommon.h"
#include "MyEngine/Graphics/Profiling/GPUScope.h"
#include "MyEngine/Graphics/Renderer/RenderQueue.h"
#include "MyEngine/Graphics/Renderer/RenderContext.h"
#include "MyEngine/Graphics/Renderer/SkinCache.h"
#include "MyEngine/Graphics/Renderer/SceneRenderer.h"
#include "MyEngine/Graphics/RenderTarget/RenderWindow.h"
#include "MyEngine/Graphics/Text/FontManager.h"
#include "MyEngine/Scene/SceneManager.h"
#ifdef USE_IMGUI
#include "MyEngine/Editor/Windows/ControlWindow.h"
#endif


//=============================================================================
// ウィンドウの追加
//=============================================================================
void WindowManager::AddWindow(const WindowConfig& config, SceneFactory sceneFactory) {
	// 最初のウィンドウのみRenderContextを初期化する
	if (windows_.empty()) {
		windows_.reserve(30);
	}

	// ウィンドウの生成
	std::unique_ptr<Win32Window> window = std::make_unique<Win32Window>();
	window->Init(config);
	// ウィンドウに描画するための機能の生成
	std::unique_ptr<RenderWindow> render = std::make_unique<RenderWindow>();
	render->Initialize(window.get());
	// ウィンドウサイズの変更を動的に変更できるようにする
	RenderWindow* renderPtr = render.get();
	window->SetOnResize([renderPtr](int w, int h) { renderPtr->Resize(w, h); });
	// SceneManagerの生成と初期シーンのセット
	std::unique_ptr<SceneManager> sceneManager = std::make_unique<SceneManager>();
	sceneManager->SetWindowTitle(config.title);
	sceneManager->Initialize(std::move(sceneFactory));
	// Editor
	auto editor = std::make_unique<EditorViewport>();
#ifdef USE_IMGUI
	editor->Initialize();
#endif

#ifdef USE_IMGUI
	// ImGuiのウィンドウなら初期化
	if (config.isImGui) {
		ImGuiManager::Initialize(window.get());
		window->SetImGuiTarget(true);
	}
#endif

	// ウィンドウをまとめた構造体に入れる
	windows_.push_back({std::move(window), std::move(render), std::move(sceneManager), std::move(editor)});
	LogManager::Log(std::format("AddWindow: title={} hwnd={}", ConvertString(config.title), (void*)windows_.back().window->GetHWND()));
	// シーンは上の sceneManager->Initialize() が作っている（ここでもう1つ作らない）
}

//=============================================================================
// 全ウィンドウのメッセージ処理
//=============================================================================
bool WindowManager::ProcessMessage() {
	for (std::vector<WindowSet>::iterator it = windows_.begin(); it != windows_.end();) {
		if (!it->window->ProcessMessage()) {
			// ウィンドウが閉じられたら、シーンの後片付けをしてから削除（×で閉じるのが普通の終わり方なので、ここで呼ばないとFinalizeが一度も走らない）
			if (it->sceneManager) {
				it->sceneManager->Finalize();
			}
			it = windows_.erase(it);
			continue;
		}
		++it;
	}

	// 全ウィンドウのが閉じたら終了
	return !windows_.empty();
}

//=============================================================================
// カメラを持っている最初のシーンを探す
//=============================================================================
IScene* WindowManager::FindGameScene() const {
	for (const WindowSet& w : windows_) {
		IScene* scene = w.sceneManager ? w.sceneManager->GetCurrentScene() : nullptr;
		if (scene && scene->GetCamera()) {
			return scene;
		}
	}
	return nullptr;
}

//=============================================================================
// 全ウィンドウ更新
//=============================================================================
void WindowManager::UpdateAll() {
	EngineSystems::FlushPending(); // 前フレームの予約を反映

	HWND focused = GetForegroundWindow();
	for (WindowSet& w : windows_) {
		bool isFocused = (focused == w.window->GetHWND());
		InputManager::SetActiveWindow(isFocused ? w.window->GetHWND() : nullptr);
		// シーンマネージャの基底クラス
		if (w.sceneManager) {
			w.sceneManager->Update();
		}

		#ifdef USE_IMGUI
		if (w.editor) {
			w.editor->Update(); // デバッグカメラ更新
		}
#endif
	}
	// 全 System に渡すカメラと、再生中かどうか（再生中・一時停止中なら true）
	IScene* gameScene = FindGameScene();
	std::shared_ptr<Camera> gameCamera = gameScene ? gameScene->GetCamera() : nullptr;
	bool isGameRunning = false;
	for (WindowSet& w : windows_) {
		if (w.sceneManager && w.sceneManager->GetPlayState() != PlayState::Editing) {
			isGameRunning = true;
		}
	}
	EngineSystems::Update(isGameRunning, gameCamera);

	// OS のカーソル（このフレームの Play 中かと、Script・SYSTEM の頼みがそろった後）
	UpdateCursor();

	InputManager::SetActiveWindow(nullptr); // 入力をリセット
}


//=============================================================================
// OS のカーソル
//=============================================================================
// 閉じ込める・隠すのは「メインのウィンドウのシーンが Play 中（一時停止・停止中は放す・出す）」のときだけ
// 閉じ込める：ゲームが頼んでいて、メインのウィンドウが手前で、ゲーム画面がある（エディタは Shift+F1 で放していない）
// 隠す　：ゲームが頼んでいて、マウスがゲーム画面の上か、閉じ込めている
// ゲーム画面（エディタは Game ビューの絵、製品版は黒帯の内側）は、メインのウィンドウの中にある
// 頼みと場所は InputManager、Play 中かは SceneManager、OS のカーソルは Win32Window が持つ。SceneManager と Win32Window を組で持っているのはここなので、ここでつなぐ
// エディタの「Game ビューの絵の上か・どこか」は描画（PreRenderAll の EditorViewport::Render）で決まるので、ここで読むのは前のフレームの値（1フレーム遅れは目に見えない）
void WindowManager::UpdateCursor() {
	if (windows_.empty()) {
		return;
	}
	const WindowSet& main = windows_[0];
	const bool isPlaying = main.sceneManager && main.sceneManager->GetPlayState() == PlayState::Playing;

	// --- 閉じ込める（隠すかは「閉じ込めているか」も見るので先に）---
	const std::optional<RECT> confineRect = isPlaying ? InputManager::GetCursorConfineRect() : std::nullopt;
	main.window->SetCursorClip(confineRect);
	InputManager::SetCursorConfineActive(confineRect.has_value());
#ifdef USE_IMGUI
	// 閉じ込めている間は、エディタのパネルをマウスで触らない（カーソルは Game ビューの中だが、上に重なった窓・パネルの縁を押せてしまうので）
	if (main.window->GetWindowConfig().isImGui) {
		ImGuiManager::SetMouseEnabled(!confineRect.has_value());
	}
#endif

	// --- 隠す ---
	main.window->SetCursorHidden(isPlaying && InputManager::ShouldHideCursor());
}


//=============================================================================
//全ウィンドウの描画をコマンド積む
//=============================================================================
void WindowManager::DrawAll() {
	for (WindowSet& w : windows_) {
		// --- 描画リクエスト ---
		if (w.sceneManager) {
			w.sceneManager->Draw(); // シーン
			IScene* scene = w.sceneManager->GetCurrentScene();
#ifdef USE_IMGUI
			w.editor->Draw(scene ? scene->GetCamera().get() : nullptr); // Editor（グリッドなど）
#endif
		}
	}
	// 最初に見つかったシーンのカメラ（軌跡の帯をカメラへ向ける・ギズモに使う）と、そのシーンの環境光（PBR の形に使う）
	IScene* gameScene = FindGameScene();
	Camera* gameCamera = gameScene ? gameScene->GetCamera().get() : nullptr;         // 借りるだけ（持ち主はシーン・CameraSystem）
	IBLEnvironment* gameEnvironment = gameScene ? gameScene->GetEnvironment() : nullptr; // 借りるだけ（持ち主はシーン。持っていなければ nullptr）
	EngineSystems::Draw(gameCamera, gameEnvironment);
}


//=============================================================================
// ウィンドウ描画開始
//=============================================================================
void WindowManager::PreRenderAll() {
	// ===== 文字のアトラスを GPU へ送る（DrawAll の Text で、初めて使った文字が足されている）=====
	// 描画とは別のコマンドリストで送って待つので、このフレームの描画からもう読める
	FontManager::UploadAtlases();

	// ===== スキンのあるメッシュを CS で曲げる（どのウィンドウも描き始める前に、1フレームに1回）=====
	// DrawAll の DrawModel が積んだ仕事を全部ここで Dispatch する。Scene ビュー・Game ビューは、同じ曲げた頂点を描く
	SkinCache::Dispatch(DirectXCommon::GetCommandList());

	// ウィンドウ分描画する
	for (WindowSet& w : windows_) {
		// ImGuiのフレーム開始
#ifdef USE_IMGUI
		if (w.window->GetWindowConfig().isImGui) {
			// 調整項目のImGui
			ImGuiManager::Begin();
			ControlWindow::Draw(w.sceneManager.get()); // 再生・ギズモの道具・保存
		}
#endif

		// ===== 描画開始処理(RenderTargetの切り替えやバリアの設定など) =====
		w.renderer->PreDraw(); // バリア変更 + クリア
		
		// シーンのカメラ取得
		IScene* scene = w.sceneManager->GetCurrentScene();
		Camera* gameCamera = scene ? scene->GetCamera().get() : nullptr;

#ifdef USE_IMGUI
		// Debug: シーンとエディタの表示
		w.editor->Render(gameCamera, w.window->GetTitle());
#else
		// Release: RenderTexture を経由せずスワップチェーンへ直接描く
		RECT rc{};
		GetClientRect(w.window->GetHWND(), &rc);
		SceneRenderer::RenderToWindow(gameCamera, w.renderer.get(), w.window->GetTitle(), static_cast<float>(rc.right - rc.left), static_cast<float>(rc.bottom - rc.top));
#endif

	};

#ifdef USE_IMGUI
	for (WindowSet& w : windows_) {
		if (w.window->GetWindowConfig().isImGui) {
			// ImGuiリクエストを積む
			ImGuiManager::ProcessRequests();
			break;
		}
	}
#endif
}

//=============================================================================
// 全ウィンドウ描画終了
//=============================================================================
void WindowManager::PostRenderAll() {
#ifdef USE_IMGUI
	// isImGuiがtrueのウィンドウだけImGuiを描画
	for (WindowSet& w : windows_) {
		if (w.window->GetWindowConfig().isImGui) {
			GPU_SCOPE(DirectXCommon::GetCommandList(), "ImGui");
			ImGuiManager::Render();
			RenderImGui();
			break;
		}
	}
#endif

	// バリアの入れ替えと共有バッファのオフセット値をリセット
	for (WindowSet& w : windows_) {
		w.renderer->PostDraw();
	}
	
	// コマンドリストの実行
	ExecuteOnly();
	// デバイスが削除されていないかチェック
	DirectXCommon::CheckDeviceRemoved();
	// 全ウィンドウのバッファを入れ替える
	for (WindowSet& w : windows_) {
		w.renderer->GetSwapChain()->Present(1, 0);
	}
	// デバイスが削除されていないかチェック
	DirectXCommon::CheckDeviceRemoved();

#ifdef USE_IMGUI
	for (WindowSet& w : windows_) {
		if (w.window->GetWindowConfig().isImGui) {
			// ImGuiリクエストをクリア
			ImGuiManager::ClearRequests();
			break;
		}
	}
#endif

	RenderQueue::Clear(); // PrimitiveRendererリクエストをクリア
	RenderContext::ResetFrame();     // GPU用データの書き込み位置を先頭に戻す
	SceneRenderer::ResetViewIndex(); // カメラのリングバッファ参照位置リセット

	// プロジェクト側で追加した後処理を実行
	for (const auto& cb : framEndCallbacks_) {
		cb.func();
	}

	// 全ウィンドウのコマンドリストがコマンドキューに投げられた後、ここで待つ
	DirectXCommon::WaitForGPU();
	// GPUがもう読まないと確かになったので、このフレームに返してもらったSRVスロットを配り直せるようにする
	DirectXCommon::RecycleFreedSRVSlots();
	// デバイスが削除されていないかチェック
	DirectXCommon::CheckDeviceRemoved();

	// コマンドアロケータ、コマンドリストをリセット
	DirectXCommon::GetCommandAllocator()->Reset();
	DirectXCommon::GetCommandList()->Reset(DirectXCommon::GetCommandAllocator(), nullptr);

	// ウィンドウのサイズ処理
	for (WindowSet& w : windows_) {
		if (w.window->GetPendingResize()) {
			int width = w.window->GetPendingWidth();
			int height = w.window->GetPendingHeight();
			w.window->ClearPendingResize();
			w.renderer->Resize(width, height, false);
		}
	}
}

//=============================================================================
// 積まれたコマンドを実行
//=============================================================================
void WindowManager::ExecuteOnly() {
	// GPUで記録したtickをReadbackBufferへコピー
	GPUProfiler::Resolve(DirectXCommon::GetCommandList());
	// コマンドリストを閉じる
	HRESULT hr = DirectXCommon::GetCommandList()->Close();
	if (FAILED(hr)) {
		LogManager::Error(std::format("Error Code: 0x{:08X}", (uint32_t)hr));
		LogManager::Flush();
		assert(false && "コマンドリストの内容を確定できませんでした");
	}
	// GPUにコマンドリストの実行を行わせる
	std::array<ID3D12CommandList*, 1> commandLists = {DirectXCommon::GetCommandList()};
	DirectXCommon::GetCommandQueue()->ExecuteCommandLists(static_cast<UINT>(commandLists.size()), commandLists.data());
}

//=============================================================================
// 終了処理
//=============================================================================
void WindowManager::Finalize() {
	DirectXCommon::WaitForGPU();
#ifdef USE_IMGUI
	for (WindowSet& w : windows_) {
		if (w.window->GetWindowConfig().isImGui) {
			ImGuiManager::Release();
			break;
		}
	}
#endif
	windows_.clear();
}

//=============================================================================
// ImGui描画
//=============================================================================
#ifdef USE_IMGUI
void WindowManager::RenderImGui() {
	const std::wstring& target = imguiTargetWindow_.empty() ? windows_[0].window->GetTitle() : imguiTargetWindow_;
	for (WindowSet& w : windows_) {
		if (w.window->GetTitle() == target) {
			D3D12_CPU_DESCRIPTOR_HANDLE rtv = w.renderer->GetCurrentRTVHandle();
			DirectXCommon::GetCommandList()->OMSetRenderTargets(1, &rtv, false, nullptr);
			ImGuiManager::RenderDrawData();
			return;
		}
	}
}
#endif

//==========================================
// ゲッター
//==========================================
Win32Window* WindowManager::GetWindowByTitle(const std::wstring& title) {
	for (WindowSet& w : windows_) {
		if (w.window->GetTitle() == title) {
			return w.window.get();
		}
	}
	return nullptr;
}

IScene* WindowManager::GetSceneByTitle(const std::wstring& title) {
	for (WindowSet& w : windows_) {
		if (w.window->GetTitle() == title) {
			return w.sceneManager->GetCurrentScene();
		}
	}
	return nullptr;
}

//==========================================
// プロジェクト固有終了処理の登録
//==========================================
int WindowManager::AddFrameEndCallback(std::function<void()> callback, const std::wstring& targetTitle) {
	int id = nextCallbackId_;
	nextCallbackId_++;
	framEndCallbacks_.push_back({id, targetTitle, std::move(callback)});
	LogManager::Log(std::format("[WindowManager::AddFrameEndCallback] FrameEndCallback登録 id={} target={}", id, targetTitle.empty() ? "全ウィンドウ" : ConvertString(targetTitle)));
	return id;
}
//==========================================
// プロジェクト固有終了処理の登録解除
//==========================================
void WindowManager::RemoveFrameEndCallback(int id) {
	std::erase_if(framEndCallbacks_, [id](const CallbackEntry& entry) { return entry.id == id; });
	LogManager::Log(std::format("[WindowManager::RemoveFrameEndCallback] FrameEndCallback解除 id={}", id));
}