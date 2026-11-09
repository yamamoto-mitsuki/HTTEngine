#include "HTTEngine/String/ConvertString.h"

#include <cctype>
#include <algorithm>

#include <windows.h>

std::wstring ConvertString(const std::string& str) {
	if (str.empty()) {
		return std::wstring();
	}

	auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), NULL, 0);
	if (sizeNeeded == 0) {
		return std::wstring();
	}
	std::wstring result(sizeNeeded, 0);
	MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), &result[0], sizeNeeded);
	return result;
}

std::string ConvertString(const std::wstring& str) {
	if (str.empty()) {
		return std::string();
	}

	auto sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0, NULL, NULL);
	if (sizeNeeded == 0) {
		return std::string();
	}
	std::string result(sizeNeeded, 0);
	WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded, NULL, NULL);
	return result;
}

std::string ToUtf8(const std::filesystem::path& path) {
	// Windows のパスの中身は UTF-16（wstring）なので、上の wstring → UTF-8 をそのまま使う
	return ConvertString(path.generic_wstring());
}

std::filesystem::path ToPath(const std::string& utf8) {
	// UTF-8 → wstring（UTF-16）にしてから path にする。wstring から作る path は、文字の読み方を間違えない
	return std::filesystem::path(ConvertString(utf8));
}

std::string ToPathKey(const std::filesystem::path& path) {
	// lexically_normal：「.」「..」を消して一番短い形にする（ファイルを見に行かず、文字だけで直すので、無いファイルでも動く）
	return ToUtf8(path.lexically_normal());
}

std::string ToLowerAscii(std::string_view text) {
	std::string result(text);
	// unsigned char で渡す（char のまま渡すと、日本語の UTF-8 の文字（負の値）で std::tolower が壊れる）
	std::ranges::transform(result, result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return result;
}