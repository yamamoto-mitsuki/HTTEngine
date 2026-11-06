#pragma once
#include <memory>
#include <string>
#include <vector>
#include <fstream>
#include <source_location>
#include <mutex>

// ===== spdlog の前方宣言 =====
// spdlog はヘッダーだけで使う形（実装も全部ヘッダーにある）。ここで include すると、
// LogManager.h を読む全部の .cpp が spdlog の実装を持ってしまう（Release の /GL では 1 つで約 6MB 増える）
// ここでは型の名前だけ知らせて、spdlog の include は LogManager.cpp だけに置く
// （std::shared_ptr は、中身の分からない型（不完全型）のままでも持てる）
namespace spdlog {
class async_logger;
}

/// <summary>
/// Log管理クラス
/// <para>呼び出すと自動で ファイル名, 行数, 呼び出した箇所の関数名 も出力される</para>
/// </summary>
class LogManager {
public:
	// ===== Passkey Idiom =====
	class ConstructorKey {
	private:
		ConstructorKey() = default;
		friend class LogManager;
	};
	explicit LogManager(ConstructorKey) {} // 鍵を受け取るコンストラクタ

	/// <summary>
	/// 初期化。Engine::Initialize()内で1度だけ呼ぶ。
	/// </summary>
	static void Initialize();

	/// <summary>
	/// 終了処理。全シンクをフラッシュしてスレッドを止める。
	/// </summary>
	static void Shutdown();

	/// <summary>
	/// バッファを即時フラッシュする。
	/// <para>assertの直前など、強制終了前に確実にログを書き出したいときに呼ぶ。</para>
	/// </summary>
	static void Flush(); // logger_ の中身（spdlog の型）を使うので、本体は LogManager.cpp

	/// <summary>
	/// 現在のログファイルパスを返す
	/// </summary>
	static const std::string& GetLogFilePath() { return instance_->logFilePath_; }

	/// <summary>
	/// INFOレベルでログを出力する。
	/// </summary>
	static void Log(const std::string& message, const std::source_location& loc = std::source_location::current());

	 /// <summary>
	/// WARNレベルでログを出力する。ImGuiで黄色表示される。
	/// </summary>
	static void Warning(const std::string& message, const std::source_location& loc = std::source_location::current());

	/// <summary>ERRORレベルでログを出力する。
	/// ImGuiで赤色表示される。
	/// </summary>
	static void Error(const std::string& message, const std::source_location& loc = std::source_location::current());

#ifdef USE_IMGUI
	/// <summary>
	/// ImGuiのログウィンドウを描画する。
	/// </summary>
	static void DrawImGuiWindow();
#endif

private:
	// シングルトン
	friend struct std::default_delete<LogManager>;
	~LogManager() = default;
	LogManager(const LogManager&) = delete;
	LogManager& operator=(const LogManager&) = delete;
	static std::unique_ptr<LogManager> instance_;

	// spdlogの非同期ロガー本体
	std::shared_ptr<spdlog::async_logger> logger_;

	std::ofstream logStream_;
	std::string logFilePath_;

#ifdef USE_IMGUI
	// ImGui表示用の重さ（色分けに使う。spdlog の level_enum をヘッダーに出さないため、自分の型にする）
	enum class LogLevel {
		Info,    // 白
		Warning, // 黄
		Error,   // 赤
	};

	// ImGui表示用バッファ
	struct LogEntry {
		std::string message;
		LogLevel level;
	};
	static constexpr int kMaxLines = 500;

	std::vector<LogEntry> logBuffer_;
	std::mutex bufferMutex_; // 非同期スレッドからも書き込まれるので排他制御
	bool scrollToBottom_ = false; // 鍵を受け取るコンストラクタは 0 で埋めてくれないので、初期値を書く
	bool autoScroll_ = false;
#endif
};
