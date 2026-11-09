#pragma once
#include <string_view>

//=============================================================================
// エディタで作る・名前を変えるファイルの名前の決まり（シーン・フォルダ・Project ウィンドウの Rename）
//=============================================================================
// 使えるのは英数字と _ - だけ。日本語のファイル名は、まだ全部のアセットで読めない（Audio.md 8章）ので、打てなくしている
// 読めるようにするときは、ここを広げれば全部の入力欄が一緒に変わる

// 1文字が使えるか（入力欄で1文字ずつ見る）
inline bool IsFileNameChar(unsigned int c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '-'; }

// 名前が使えるか（空はだめ）
inline bool IsValidFileName(std::string_view name) {
	if (name.empty()) {
		return false;
	}
	for (const char c : name) {
		if (!IsFileNameChar(static_cast<unsigned char>(c))) {
			return false;
		}
	}
	return true;
}

// アセットとして読めるパスか（UTF-8 のパスを渡す）
// 今は英数字・記号（ASCII）だけ。日本語などが入っていると Manager が読めずに止まることがある（Audio.md 8章）ので、Inspector の一覧に出さない
inline bool IsLoadableAssetPath(std::string_view path) {
	for (const char c : path) {
		if (static_cast<unsigned char>(c) >= 0x80) {
			return false; // UTF-8 で 0x80 以上 ＝ ASCII ではない文字の一部
		}
	}
	return true;
}