#include "HTTEngine/Diagnostics/CrashHandler.h"

#include <array>
#include <string>

#include <dbghelp.h>
#include <strsafe.h>

#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/UI/GameNotification.h"

#pragma comment(lib, "DbgHelp.lib")


LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception) noexcept {
	// 時刻を取得して、時刻を名前にいれたファイルを作成。Dumpsディレクトリ以下に出力
	SYSTEMTIME time;
	GetLocalTime(&time);
	std::array<wchar_t, MAX_PATH> filePath{};
	CreateDirectory(L"./Dumps", nullptr);
	StringCchPrintfW(filePath.data(), filePath.size(), L"./Dumps/%04d-%02d%02d-%02d%02d.dmp", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute);
	HANDLE dumpFileHandle = CreateFile(filePath.data(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
	// 設定情報を入力
	MINIDUMP_EXCEPTION_INFORMATION minidumpInformation{0};
	minidumpInformation.ThreadId = GetCurrentThreadId();
	minidumpInformation.ExceptionPointers = exception;
	minidumpInformation.ClientPointers = TRUE;
	// Dumpを出力。MiniDumpNormalは最低限の情報を出力するフラグ
	MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dumpFileHandle, MiniDumpNormal, &minidumpInformation, nullptr, nullptr);
	// 他に関連付けられているSEH例外ハンドラがあれば実行。通常はプロセスを終了
	return EXCEPTION_EXECUTE_HANDLER;
}