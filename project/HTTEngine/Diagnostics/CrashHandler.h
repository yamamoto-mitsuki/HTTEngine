#pragma once
#include <Windows.h>

// クラッシュ時Dumpファイルを出力する
LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception) noexcept; // この関数は必ず例外を投げない