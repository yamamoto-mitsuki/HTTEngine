#pragma once
#include <filesystem>


/// <summary>
/// Code ウィンドウ（ソースコードを読むウィンドウ。Scene・Game と同じように、好きな所へドッキングして置ける）
/// <para>開き方：Inspector の Component の見出しを右クリック → Open in Code Window ／ Project ウィンドウでコードのファイルをダブルクリック</para>
/// <para>読むだけ（書き換えは Visual Studio で。Open in Visual Studio）。ファイルが書き換わったら、自動で読み直す</para>
/// <para>開いたファイルはタブで並ぶ。色分け（キーワード・コメント・文字列・数・#include など）、行番号、検索（Ctrl+F）</para>
/// </summary>
class CodeWindow {
public:
	// コード用の等幅フォント（Consolas。日本語は BIZ UDゴシックなどで出す）を足す。ImGuiManager::Initialize から、ふだんのフォントの後に1回
	static void Initialize();

	// 毎フレーム1回（ImGuiManager::Begin から）
	static void Draw();

	// ファイルを開く（もう開いていれば、そのタブを前に出す）。line（1から）を渡すと、その行まで送る
	static void Open(const std::filesystem::path& path, int line = 0);

	// Code ウィンドウで読めるファイルか（拡張子で見る：.cpp .h .hlsl .hlsli .json .txt .md など）
	static bool IsCodeFile(const std::filesystem::path& path);

	// .cpp を開くアプリ（Windows の「既定のアプリ」。ふつうは Visual Studio）で開く
	static void OpenExternal(const std::filesystem::path& path);
};