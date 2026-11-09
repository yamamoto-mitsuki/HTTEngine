#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "HTTEngine/Time/EngineTime.h"
#include "HTTEngine/Window/WindowManager.h"
#include "HTTEngine/Graphics/GPU/DirectXCommon.h"


/// <summary>
/// エンジンの基盤。ここでゲームループ、初期化を管理
/// </summary>
class Engine {
public:
	// ===== Passkey Idiom =====
	class ConstructorKey {
	private:
		ConstructorKey() = default;
		friend class Engine;
	};
	explicit Engine(ConstructorKey) {} // 鍵を受け取るコンストラクタ


	/// <summary>
	/// エンジンの初期化とウィンドウ生成。プログラム起動時に1度だけ呼ぶ。
	/// </summary>
	/// <param name="config">ウィンドウ設定(サイズ・タイトル・ImGui設定・アスペクト比など)</param>
	/// <param name="initialScene">シーンの設定</param>
	static void Initialize(const WindowConfig& config, SceneFactory initialScene);

	/// <summary>
	/// エンジンの初期化とウィンドウ生成。最初のシーンを名前で渡す（resources/scenes/<名前>.scene.json）
	/// <para>REGISTER_SCENE(名前) したクラスがあればそれ、無ければ DefaultScene（Entity だけで動くシーン）で開く</para>
	/// <para>エディタ（Debug）は、前回最後に開いていたシーンから始まる（覚えていなければ firstScene）</para>
	/// </summary>
	/// <param name="config">ウィンドウ設定(サイズ・タイトル・ImGui設定・アスペクト比など)</param>
	/// <param name="firstScene">製品版で最初に出るシーンの名前（例: "Title"）</param>
	static void Initialize(const WindowConfig& config, const std::string& firstScene);

	/// <summary>
	/// メッセージ処理。falseが返ったらループを抜ける。
	/// </summary>
	static bool ProcessMessage();

	/// <summary>
	/// フレームの開始処理。毎フレーム最初に呼ぶ。
	/// </summary>
	static void BeginFrame();

	/// <summary>
	/// フレームの終了処理。毎フレーム最後に呼ぶ。
	/// </summary>
	static void EndFrame();

	/// <summary>
	/// エンジンの終了処理。プログラム終了時に1度だけ呼ぶ。
	/// </summary>
	static void Finalize();

	// ===== ゲッター =====
	static WindowManager* GetWindowManager() { return &instance_->windowManager_; }

private:
	~Engine() = default;
	Engine(const Engine&) = delete;
	Engine& operator=(const Engine&) = delete;
	// シングルトン
	friend struct std::default_delete<Engine>;
	static std::unique_ptr<Engine> instance_;

	WindowManager windowManager_;
	std::chrono::high_resolution_clock::time_point lastTime_;    // 前フレームの時刻
	std::chrono::high_resolution_clock::time_point updateStart_; // 更新処理の計測用
};