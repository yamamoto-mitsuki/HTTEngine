#pragma once
#include <string>
#include <vector>
#include <functional>

struct NotificationButton {
	std::string label;            // ボタンのテキスト
	std::function<void()> action; // クリック時の処理
};

class GameNotification {
public:
	/// <summary>
	/// 初期化。Engine::Initialize()内で1度だけ呼ぶ。
	/// </summary>
	static void Initialize();

	/// <summary>
	/// テキストのみのトースト通知を送る
	/// </summary>
	static void Send(const std::string& title, const std::string& message = "");

	/// <summary>
	/// ボタン付きトースト通知を送る（最大2ボタン）
	/// </summary>
	static void Send(const std::string& title, const std::string& message, const std::vector<NotificationButton>& buttons);

	/// <summary>
	/// ファイルを開く
	/// </summary>
	static NotificationButton OpenFileButton(const std::string& label, const std::wstring& filePath);
};