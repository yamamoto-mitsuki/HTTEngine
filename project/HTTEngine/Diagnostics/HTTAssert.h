#pragma once
#include <cassert>
#include <string>
#include <filesystem>

#include <windows.h>
#include <shellapi.h>

#include "HTTEngine/UI/GameNotification.h"
#include "HTTEngine/String/ConvertString.h"
#include "HTTEngine/Diagnostics/LogManager.h"


namespace HTTAssertDetail {
// パスからファイル名だけ取り出す
inline std::string FileName(const char* path) { return std::filesystem::path(path).filename().string(); }

// Assert失敗時の通知を出す。「logを開く」ボタンを押したときだけlogを開く。
inline void NotifyAssertFailure(const std::string& condition, const char* file, int line, const char* func, const std::string& message = "") {
	// ===== logに残す =====
	std::string detail = "[" + FileName(file) + "::" + func + " " + std::to_string(line) + "]" + " " + "AssertError: " + condition;
	if (!message.empty()) {
		detail += " - " + message;
	}
	LogManager::Error(detail);
	LogManager::Flush();

	// ===== 通知を出す =====
	std::string logPath = LogManager::GetLogFilePath();
	std::wstring absoluteLogPath = std::filesystem::absolute(logPath).wstring();
	// メッセージ
	std::string body = "\n" + FileName(file) + "::" + func + " " + std::to_string(line);
	if (!message.empty()) {
		body += "\n" + message;
	}

	GameNotification::Send(
	    "AssertError: " + condition, body,
	    {
	        {"logを開く", [absoluteLogPath]() {
		         std::wstring command = L"/c code \"" + absoluteLogPath + L"\"";
		         ShellExecuteW(nullptr, L"open", L"cmd.exe", command.c_str(), nullptr, SW_HIDE);
	        }}
        });
#ifdef NDEBUG
	std::abort();
#endif
}
} // namespace HTTAssertDetail

#define HTT_ASSERT_IMPL(condition)                                                                                                                                                                      \
	do {                                                                                                                                                                                               \
		if (!(condition)) {                                                                                                                                                                            \
			HTTAssertDetail::NotifyAssertFailure(#condition, __FILE__, __LINE__, __func__);                                                                                                             \
			assert(condition);                                                                                                                                                                         \
		}                                                                                                                                                                                              \
	} while (0)

#define HTT_ASSERT_MSG_IMPL(condition, message)                                                                                                                                                         \
	do {                                                                                                                                                                                               \
		if (!(condition)) {                                                                                                                                                                            \
			HTTAssertDetail::NotifyAssertFailure(#condition, __FILE__, __LINE__, __func__, message);                                                                                                    \
			assert(condition);                                                                                                                                                                         \
		}                                                                                                                                                                                              \
	} while (0)

// 通知でAssertを知らせる
#define HTT_ASSERT(condition)              HTT_ASSERT_IMPL(condition)
// 通知、logファイルに第2引数の文字列を書き出す
#define HTT_ASSERT_MSG(condition, message) HTT_ASSERT_MSG_IMPL(condition, message)