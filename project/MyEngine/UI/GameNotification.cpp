#include "MyEngine/UI/GameNotification.h"

#include <string_view>
#include <format>

// shellapi.h は Windows.h の型（HWND など）を使うので、先に Windows.h を読む
// （前は LogManager.h の中の spdlog が Windows.h を読んでいたので、たまたま通っていた）
#include <Windows.h>
#include <shellapi.h>

#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Notifications.h>
#pragma comment(lib, "windowsapp.lib")

#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/String/ConvertString.h"

using namespace winrt::Windows::UI::Notifications;
using namespace winrt::Windows::Data::Xml::Dom;
// アプリID
static constexpr std::wstring_view kAppId = L"MyEngine";
// XMLで特別な意味を持つ文字を回避する
static std::wstring EscapeXml(const std::wstring& input) {
	std::wstring out;
	out.reserve(input.size());
	for (wchar_t c : input) {
		switch (c) {
		case L'&':
			out += L"&amp;";
			break;

		case L'<':
			out += L"&lt;";
			break;

		case L'>':
			out += L"&gt;";
			break;

		case '"':
			out += L"&quot;";
			break;

		case L'\'':
			out += L"&apos;";
			break;

		default:
			out += c;
			break;
		}
	}
	return out;
}

//=============================================================================
// 初期化
//=============================================================================
void GameNotification::Initialize() {
	std::wstring regPath = L"SOFTWARE\\Classes\\AppUserModelId\\";
	regPath += kAppId;

	HKEY hKey;
	LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, regPath.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);

	if (result == ERROR_SUCCESS) {
		// 表示名を登録
		const wchar_t* displayName = kAppId.data();
		RegSetValueExW(hKey, L"DisplayName", 0, REG_SZ, reinterpret_cast<const BYTE*>(displayName), static_cast<DWORD>((wcslen(displayName) + 1) * sizeof(wchar_t)));
		RegCloseKey(hKey);
	}
}

//=============================================================================
// ボタンなし
//=============================================================================
void GameNotification::Send(const std::string& title, const std::string& message) { Send(title, message, {}); }

//=============================================================================
// ボタンあり
//=============================================================================
void GameNotification::Send(const std::string& title, const std::string& message, const std::vector<NotificationButton>& buttons) {
	try {
		// ===== 1.XMLを作成 =====
		// ボタンがある場合はactions要素を追加
		std::wstring actionsXML;
		for (size_t i = 0; i < buttons.size() && i < 2; ++i) {
			actionsXML += std::format(LR"(<action content="{}" arguments="btn{}" actionType="foreground"/>)", EscapeXml(ConvertString(buttons[i].label)), i);
		}

		std::wstring xml = std::format(
		    LR"(<toast>
              <visual>
                <binding template="ToastGeneric">
                  <text>{}</text>
                  <text>{}</text>
                </binding>
              </visual>
              {}
            </toast>)",
		    EscapeXml(ConvertString(title)), EscapeXml(ConvertString(message)), actionsXML.empty() ? L"" : L"<actions>" + actionsXML + L"</actions>");

		// ===== 2.ToastNotificationを作成 =====
		XmlDocument doc;
		doc.LoadXml(xml);
		ToastNotification toast(doc);
		ToastNotifier notifier = ToastNotificationManager::CreateToastNotifier(kAppId);

		// ===== 3.ボタンのコールバックを登録 =====
		if (!buttons.empty()) {
			auto capturedButtons = buttons; // コピー
			auto token = toast.Activated(
			    winrt::Windows::Foundation::TypedEventHandler<ToastNotification, winrt::Windows::Foundation::IInspectable>(
			        [capturedButtons](ToastNotification const&, winrt::Windows::Foundation::IInspectable const& args) {
				        auto activatedArgs = args.try_as<ToastActivatedEventArgs>();
				        if (!activatedArgs)
					        return;

				        std::wstring argument{activatedArgs.Arguments()};
				        for (size_t i = 0; i < capturedButtons.size() && i < 2; ++i) {
					        if (argument == std::format(L"btn{}", i)) {
						        if (capturedButtons[i].action) {
							        capturedButtons[i].action();
						        }
						        break;
					        }
				        }
			        }));
		}
		// ===== 3.Windowsに送る =====
		notifier.Show(toast);
	}

	catch (const winrt::hresult_error& ex) {
		LogManager::Log("Failed to send notification: " + std::to_string(ex.code()) + " - " + ConvertString(ex.message().c_str()));
	}
	LogManager::Log("Sent notification: " + title + " - " + message);
}

//=============================================================================
// ボタンを押したとき、メッセージ付きでファイルを開く
//=============================================================================
NotificationButton GameNotification::OpenFileButton(const std::string& label, const std::wstring& filePath) {
	return {label, [filePath]() { ShellExecuteW(nullptr, L"open", filePath.c_str(), nullptr, nullptr, SW_SHOW); }};
}