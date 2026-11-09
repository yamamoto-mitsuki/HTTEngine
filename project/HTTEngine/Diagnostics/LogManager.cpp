#include "HTTEngine/Diagnostics/LogManager.h"

#include <format>
#include <chrono>
#include <filesystem>

// spdlog の include はこの .cpp だけに置く（LogManager.h は前方宣言だけ）
#include <externals/spdlog/spdlog.h>
#include <externals/spdlog/async.h>
#include <externals/spdlog/sinks/msvc_sink.h>
#include <externals/spdlog/sinks/basic_file_sink.h>
#ifdef USE_IMGUI
#include <externals/imgui/imgui.h>
#endif

#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/String/ConvertString.h"

// 静的メンバ変数
std::unique_ptr<LogManager> LogManager::instance_ = nullptr;


namespace {
// std::source_locationのファイル名を分かりやすいように直す
inline std::string FileNameOnly(std::string_view path) {
	auto pos = path.find_last_of("/\\");
	if (pos == std::string::npos) {
		// 見つからなかったら
		return std::string(path);
	}

	return std::string(path.substr(pos + 1));
}

// std::source_locationの関数名を分かりやすいように直す
// void __cdecl LogManager::Initialize(void) → LogManager::Initialize にする
inline std::string FuncNameOnly(std::string_view name) {

	// --- 1.引数部分を削除する ---
	auto end = name.find('(');
	std::string_view reName = (end == std::string::npos) ? name : name.substr(0, end); // '(' が見つかるか

	// --- 2.  void __cdecl 部分を削除 ---
	auto pos = reName.rfind(' '); // rfind...文字列の最後にある ' ' を走査
	if (pos != std::string::npos) {
		reName = reName.substr(pos + 1);
	}

	return std::string(reName);
}
} // namespace

//=============================================================================
// 初期化
//=============================================================================
void LogManager::Initialize() {
	// インスタンス生成
	HTT_ASSERT_MSG(instance_ == nullptr, "LogManager::Initialize()が2回以上呼び出されています");
	instance_ = std::make_unique<LogManager>(ConstructorKey());

	// フォルダ生成
	std::filesystem::create_directory("logs");
	// ログファイル名を現在時刻で決定
	auto now = std::chrono::system_clock::now();
	// 秒単位にする
	auto nowSec = std::chrono::time_point_cast<std::chrono::seconds>(now);
	// 日本時間（PCの設定時間）に変換
	std::chrono::zoned_time localTime{std::chrono::current_zone(), nowSec};
	// 時間でファイル作成
	std::string dateStr = std::format("{:%Y_%m-%d_%H-%M}", localTime);
	std::string filePath = "logs/" + dateStr + ".log";
	instance_->logFilePath_ = filePath;

	// 非同期スレッドプールの初期化
	// 第1引数: キューサイズ（8192件）
	// 第2引数: バックグラウンドスレッド数
	spdlog::init_thread_pool(8192, 1);

	// シンクの生成
	// basic_file_sink    : ファイルに書き込む
	// msvc_sink          : OutputDebugString に出力（Visual Studioの出力ウィンドウ）
	auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(filePath, true);
	auto msvcSink = std::make_shared<spdlog::sinks::msvc_sink_mt>();

	// フォーマット: [時刻] [レベル] メッセージ
	fileSink->set_pattern("[%H:%M:%S] [%l] %v");
	msvcSink->set_pattern("[%l] %v");

	// 非同期ロガーの生成（複数シンクをまとめて渡す）
	instance_->logger_ = std::make_shared<spdlog::async_logger>(
	    "engine", spdlog::sinks_init_list{fileSink, msvcSink}, spdlog::thread_pool(),
	    spdlog::async_overflow_policy::block // キューが溢れたらブロック（ログを捨てない）
	);
	instance_->logger_->set_level(spdlog::level::trace);

	// spdlogのグローバルロガーに登録
	spdlog::register_logger(instance_->logger_);

	Log("Initialized");
}

//=============================================================================
// 終了処理
//=============================================================================
void LogManager::Shutdown() {
	if (instance_->logger_) {
		instance_->logger_->flush();
	}
	spdlog::shutdown(); // スレッドプールを停止
	instance_.reset(); // ロガーとImGui用のバッファを捨てる（この後の Log は何もしない）
}

//=============================================================================
// 即時フラッシュ
//=============================================================================
void LogManager::Flush() {
	if (instance_ && instance_->logger_) {
		instance_->logger_->flush();
	}
}

//=============================================================================
// INFOレベルでログを出力する
//=============================================================================
void LogManager::Log(const std::string& message, const std::source_location& loc) {
	// Shutdown の後（instance_ が無いとき）は何もしない
	if (!instance_) {
		return;
	}
	if (instance_->logger_) {
		instance_->logger_->info(std::format("[{}:{}] [{}] ", FileNameOnly(loc.file_name()), loc.line(),
								 FuncNameOnly(loc.function_name())) + message);
	}

#ifdef USE_IMGUI
	std::lock_guard<std::mutex> lock(instance_->bufferMutex_);
	instance_->logBuffer_.push_back({std::format("[{}] ", FuncNameOnly(loc.function_name())) + message, LogLevel::Info});
	if (static_cast<int>(instance_->logBuffer_.size()) > kMaxLines) {
		instance_->logBuffer_.erase(instance_->logBuffer_.begin());
	}
	instance_->scrollToBottom_ = true;
#endif
}

//=============================================================================
// WARNレベルでログを出力する
//=============================================================================
void LogManager::Warning(const std::string& message, const std::source_location& loc) {
	// Shutdown の後（instance_ が無いとき）は何もしない
	if (!instance_) {
		return;
	}
	if (instance_->logger_) {
		instance_->logger_->warn(std::format("[{}:{}] [{}] ", FileNameOnly(loc.file_name()), loc.line(), 
								 FuncNameOnly(loc.function_name())) + message);
	}

#ifdef USE_IMGUI
	std::lock_guard<std::mutex> lock(instance_->bufferMutex_);
	instance_->logBuffer_.push_back({std::format("[{}] ", FuncNameOnly(loc.function_name())) + message, LogLevel::Warning});
	if (static_cast<int>(instance_->logBuffer_.size()) > kMaxLines) {
		instance_->logBuffer_.erase(instance_->logBuffer_.begin());
	}
	instance_->scrollToBottom_ = true;
#endif
}

//=============================================================================
// ERRORレベルでログを出力する
//=============================================================================
void LogManager::Error(const std::string& message, const std::source_location& loc) {
	// Shutdown の後（instance_ が無いとき）は何もしない
	if (!instance_) {
		return;
	}
	if (instance_->logger_) {
		instance_->logger_->error(std::format("[{}:{}] [{}] ", FileNameOnly(loc.file_name()), loc.line(), 
								 FuncNameOnly(loc.function_name())) + message);
	}

#ifdef USE_IMGUI
	std::lock_guard<std::mutex> lock(instance_->bufferMutex_);
	instance_->logBuffer_.push_back({std::format("[{}] ",FuncNameOnly(loc.function_name())) + message, LogLevel::Error});
	if (static_cast<int>(instance_->logBuffer_.size()) > kMaxLines) {
		instance_->logBuffer_.erase(instance_->logBuffer_.begin());
	}
	instance_->scrollToBottom_ = true;
#endif
}

#ifdef USE_IMGUI
//=============================================================================
// ImGuiログウィンドウの描画
//=============================================================================
void LogManager::DrawImGuiWindow() {
	ImGui::Begin("Console");
	// ---ツールバー ---
	if (ImGui::Button("Clear")) {
		std::lock_guard<std::mutex> lock(instance_->bufferMutex_);
		instance_->logBuffer_.clear();
	}
	ImGui::SameLine();
	ImGui::Checkbox("Auto Scroll", &instance_->autoScroll_);

	ImGui::Separator();

	// --- ログ本文 ---
	ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
	// 描画中だけロックしてコピーする（描画中ずっとロックしない）
	std::vector<LogEntry> snapshot;
	{
		std::lock_guard<std::mutex> lock(instance_->bufferMutex_);
		snapshot = instance_->logBuffer_;
	}

	for (const auto& entry : snapshot) {
		ImVec4 color;
		bool hasColor = true;
		switch (entry.level) {
		case LogLevel::Warning:
			color = ImVec4(1.0f, 0.9f, 0.2f, 1.0f);
			break; // 黄
		case LogLevel::Error:
			color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
			break; // 赤
		case LogLevel::Info:
			hasColor = false;
			break; // 白（デフォルト）
		}

		if (hasColor) {
			ImGui::PushStyleColor(ImGuiCol_Text, color);
		}
		ImGui::TextUnformatted(entry.message.c_str());
		if (hasColor) {
			ImGui::PopStyleColor();
		}
	}

	if (instance_->scrollToBottom_ && instance_->autoScroll_) {
		ImGui::SetScrollHereY(1.0f);
		instance_->scrollToBottom_ = false;
	}

	ImGui::EndChild();
	ImGui::End();
}
#endif