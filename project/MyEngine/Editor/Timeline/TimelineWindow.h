#pragma once
#include <filesystem>

/// <summary>
/// Timeline ウィンドウ（.timeline のファイルを時間の表で直す。Unity の Timeline と同じ考え）
/// <para>開く：Hierarchy で Timeline Component の付いた Entity を選ぶ（プレビューできる）/ Project で .timeline をダブルクリック（同じファイルを再生する Entity
/// があれば、それでプレビューできる）</para> <para>直した物は、操作が終わるたびにファイルへ書いて Undo に積む（.curve と同じ）</para>
/// <para>プレビュー：停止中に再生位置を動かすと、その時間の値を画面に出す。値を書くのは描画の要求を積む間だけ（TimelineSystem::RequestPreview）。Inspector・保存・Undo は元の値を見る</para>
/// </summary>
class TimelineWindow {
public:
	// 毎フレーム1回（ImGuiManager::Begin から）
	static void Draw();

	// ファイルを開いて、ウィンドウを前に出す（Project のダブルクリック・Inspector のボタンから）
	static void Open(const std::filesystem::path& filePath);
};