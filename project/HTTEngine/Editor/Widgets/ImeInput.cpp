#include "ImeInput.h"

#include <algorithm>
#include <cfloat>
#include <string>
#include <vector>

#include <Windows.h>
#include <imm.h>

#include <externals/imgui/imgui.h>
#include <externals/imgui/imgui_internal.h>

#pragma comment(lib, "imm32.lib")

namespace {
// 変換中の文字
struct Composition {
	std::string text;    // 変換中の文字（ImGuiに合わせてUTF-8）
	int cursor = 0;      // IMEのカーソルの位置（textの何バイト目か）
	int targetBegin = 0; // 変換の対象になっている文節（textの何バイト目から何バイト目か。無ければ同じ値）
	int targetEnd = 0;
};

Composition composition;
bool isComposing = false;     // IMEが変換中（WM_IME_STARTCOMPOSITION 〜 WM_IME_ENDCOMPOSITION）
bool enterUsedByIme = false;  // IMEが使ったEnter（離すまでImGuiに渡さない）
bool escapeUsedByIme = false; // IMEが使ったEsc（同上）

// UTF-16の先頭count文字が、UTF-8で何バイトになるか（IMEは位置をUTF-16の文字数で教えてくる）
int Utf8Length(const std::wstring& text, int count) {
	count = std::clamp(count, 0, static_cast<int>(text.size()));
	if (count == 0) {
		return 0;
	}
	return WideCharToMultiByte(CP_UTF8, 0, text.data(), count, nullptr, 0, nullptr, nullptr);
}

// IMEから変換中の文字を読む
Composition ReadComposition(HWND hwnd) {
	Composition result;
	HIMC context = ImmGetContext(hwnd);
	if (!context) {
		return result;
	}

	// --- 文字（UTF-16）。大きさはバイト数で返ってくる ---
	const LONG bytes = ImmGetCompositionStringW(context, GCS_COMPSTR, nullptr, 0);
	if (bytes > 0) {
		std::wstring wide(static_cast<size_t>(bytes) / sizeof(wchar_t), L'\0');
		ImmGetCompositionStringW(context, GCS_COMPSTR, wide.data(), static_cast<DWORD>(bytes));
		const int length = static_cast<int>(wide.size());

		// --- 1文字ごとの状態（入力中・変換の対象・変換済み など）---
		std::vector<BYTE> attributes(wide.size(), ATTR_INPUT);
		ImmGetCompositionStringW(context, GCS_COMPATTR, attributes.data(), static_cast<DWORD>(attributes.size()));
		// --- カーソルの位置（何文字目か。戻り値がそのまま位置）---
		const LONG cursor = ImmGetCompositionStringW(context, GCS_CURSORPOS, nullptr, 0);

		// --- UTF-8に直す ---
		result.text.resize(static_cast<size_t>(Utf8Length(wide, length)));
		WideCharToMultiByte(CP_UTF8, 0, wide.data(), length, result.text.data(), static_cast<int>(result.text.size()), nullptr, nullptr);
		result.cursor = Utf8Length(wide, static_cast<int>(cursor));

		// --- 変換の対象の文節（スペースで変換したときに、今選んでいる部分）---
		int begin = -1;
		int end = -1;
		for (int i = 0; i < length; ++i) {
			if (attributes[i] == ATTR_TARGET_CONVERTED || attributes[i] == ATTR_TARGET_NOTCONVERTED) {
				if (begin < 0) {
					begin = i;
				}
				end = i + 1;
			}
		}
		if (begin >= 0) {
			result.targetBegin = Utf8Length(wide, begin);
			result.targetEnd = Utf8Length(wide, end);
		}
	}
	ImmReleaseContext(hwnd, context);
	return result;
}

// 変換中の文字を捨てる
void CancelComposition(HWND hwnd) {
	if (HIMC context = ImmGetContext(hwnd)) {
		ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
		ImmReleaseContext(hwnd, context);
	}
}

// ImGuiが入力欄のカーソルの位置をIMEに教えるときに呼ぶ関数（入力欄が変わった・カーソルが動いたときだけ呼ばれる）
// ImGuiの標準は候補の一覧の「左上」をカーソルの行の上端に合わせるので、変換中の文字に重なるおそれがある。
// 行の範囲（rcArea）を避けて、その下に出すように頼む
void SetImeData(ImGuiContext*, ImGuiViewport* viewport, ImGuiPlatformImeData* data) {
	HWND hwnd = static_cast<HWND>(viewport->PlatformHandleRaw);
	if (!hwnd || !data->WantVisible) {
		return;
	}
	HIMC context = ImmGetContext(hwnd);
	if (!context) {
		return;
	}
	const LONG x = static_cast<LONG>(data->InputPos.x - viewport->Pos.x);
	const LONG top = static_cast<LONG>(data->InputPos.y - viewport->Pos.y);
	const LONG bottom = top + static_cast<LONG>(data->InputLineHeight);

	// Windowsの変換窓（出さないようにしているが、出してしまうIMEのために場所だけ合わせておく）
	COMPOSITIONFORM compositionForm = {};
	compositionForm.dwStyle = CFS_FORCE_POSITION;
	compositionForm.ptCurrentPos = {x, top};
	ImmSetCompositionWindow(context, &compositionForm);

	// 候補の一覧：カーソルの行（top〜bottom）を避けて、行のすぐ下に出す
	CANDIDATEFORM candidateForm = {};
	candidateForm.dwIndex = 0;
	candidateForm.dwStyle = CFS_EXCLUDE;
	candidateForm.ptCurrentPos = {x, bottom};
	candidateForm.rcArea = {x, top, x + 1, bottom};
	ImmSetCandidateWindow(context, &candidateForm);

	ImmReleaseContext(hwnd, context);
}

// 入力欄の背景の色。FrameBgは半透明なので、ウィンドウの背景に重ねた色を作って不透明にする（下の文字を隠すため）
ImU32 GetFieldBackgroundColor() {
	const ImGuiStyle& style = ImGui::GetStyle();
	const ImVec4& frame = style.Colors[ImGuiCol_FrameBg];
	const ImVec4& window = style.Colors[ImGuiCol_WindowBg];
	const float alpha = frame.w;
	return ImGui::GetColorU32(ImVec4(window.x + (frame.x - window.x) * alpha, window.y + (frame.y - window.y) * alpha, window.z + (frame.z - window.z) * alpha, 1.0f));
}
} // namespace

//=============================================================================
// 初期化
//=============================================================================
void ImeInput::Initialize() { ImGui::GetPlatformIO().Platform_SetImeDataFn = SetImeData; }

//=============================================================================
// ウィンドウのメッセージ
//=============================================================================
bool ImeInput::HandleMessage(unsigned int message, std::uintptr_t wparam) {
	switch (message) {
	case WM_IME_STARTCOMPOSITION:
		isComposing = true;
		break;

	case WM_IME_ENDCOMPOSITION:
		isComposing = false;
		// IMEによっては、確定に使ったEnterのWM_KEYDOWNが、変換の終わりより後に届く。
		// そのとき押されたままのキーは「IMEが使ったキー」として、離すまでImGuiに渡さない
		enterUsedByIme |= (GetKeyState(VK_RETURN) & 0x8000) != 0;
		escapeUsedByIme |= (GetKeyState(VK_ESCAPE) & 0x8000) != 0;
		break;

	case WM_KILLFOCUS:
		isComposing = false;
		enterUsedByIme = false;
		escapeUsedByIme = false;
		break;

	case WM_KEYDOWN:
	case WM_KEYUP: {
		if (wparam != VK_RETURN && wparam != VK_ESCAPE) {
			break;
		}
		bool& usedByIme = (wparam == VK_RETURN) ? enterUsedByIme : escapeUsedByIme;
		if (message == WM_KEYDOWN && isComposing) {
			usedByIme = true; // 変換中に押された＝IMEの確定・取り消しに使うキー
		}
		if (usedByIme) {
			if (message == WM_KEYUP) {
				usedByIme = false; // 離したら、次からは普通にImGuiへ渡す
			}
			return true;
		}
		break;
	}
	}
	return false;
}

//=============================================================================
// 毎フレーム
//=============================================================================
void ImeInput::NewFrame() {
	composition = {};
	HWND hwnd = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
	if (!hwnd) {
		return;
	}
	if (ImGui::GetIO().WantTextInput) {
		composition = ReadComposition(hwnd);
	} else if (isComposing) {
		// 入力欄から外れたのに変換中の文字が残っていたら捨てる（ほかの入力欄やカメラ操作に持ち越さない）
		CancelComposition(hwnd);
	}
}

void ImeInput::DrawComposition() {
	if (composition.text.empty()) {
		return;
	}
	// 入力欄のカーソルの位置。ImGuiはIMEに教えるために、この値を毎フレーム作っている（imgui_internal.h）
	const ImGuiPlatformImeData& ime = ImGui::GetCurrentContext()->PlatformImeData;
	if (!ime.WantVisible) {
		return; // カーソルを出している入力欄が無い
	}

	ImFont* font = ImGui::GetFont();
	const float fontSize = ime.InputLineHeight;
	const char* text = composition.text.c_str();
	auto widthTo = [&](int end) { return font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text, text + end).x; }; // 先頭からendバイト目までの幅
	const float width = widthTo(static_cast<int>(composition.text.size()));
	const ImVec2 origin(ime.InputPos.x + 1.0f, ime.InputPos.y); // InputPosは「カーソルの1ピクセル左・行の上端」
	const float bottom = origin.y + fontSize;
	ImDrawList* drawList = ImGui::GetForegroundDrawList(ImGui::GetMainViewport()); // 全ウィンドウより手前に描く

	// --- 背景：入力欄と同じ色で塗って、カーソルより後ろの文字とカーソルを隠す ---
	drawList->AddRectFilled(ImVec2(origin.x - 1.0f, origin.y), ImVec2(origin.x + width + 1.0f, bottom), GetFieldBackgroundColor());

	// --- 変換の対象の文節は、選択の色で塗る ---
	const bool hasTarget = composition.targetEnd > composition.targetBegin;
	const float targetLeft = origin.x + widthTo(composition.targetBegin);
	const float targetRight = origin.x + widthTo(composition.targetEnd);
	if (hasTarget) {
		drawList->AddRectFilled(ImVec2(targetLeft, origin.y), ImVec2(targetRight, bottom), ImGui::GetColorU32(ImGuiCol_TextSelectedBg));
	}

	// --- 文字と下線（全体は細い線、変換の対象は太い線。WindowsのIMEと同じ見せ方）---
	const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
	drawList->AddText(font, fontSize, origin, textColor, text, text + composition.text.size());
	drawList->AddLine(ImVec2(origin.x, bottom - 1.0f), ImVec2(origin.x + width, bottom - 1.0f), textColor, 1.0f);
	if (hasTarget) {
		drawList->AddLine(ImVec2(targetLeft, bottom - 1.0f), ImVec2(targetRight, bottom - 1.0f), textColor, 2.0f);
	} else {
		// 打っている途中なら、変換中の文字の中にカーソルを出す
		const float cursorX = origin.x + widthTo(composition.cursor);
		drawList->AddLine(ImVec2(cursorX, origin.y), ImVec2(cursorX, bottom), ImGui::GetColorU32(ImGuiCol_InputTextCursor), 1.0f);
	}
}

bool ImeInput::IsComposing() { return !composition.text.empty(); }