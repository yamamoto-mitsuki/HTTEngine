#pragma once
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

#include <externals/imgui/imgui.h>
#include <externals/magic_enum/magic_enum.hpp>

#include "HTTEngine/Core/EnumTipTable.h"
#include "HTTEngine/String/FileNameRule.h"
#include "HTTEngine/Editor/Widgets/ImeInput.h"

/// <summary>
/// Hierarchy・Inspectorなど、エディタのウィンドウで共通に使うImGuiの部品
/// <para>ImGuiManagerは USE_IMGUI の中にしか無いので、Releaseでもコンパイルされるウィンドウから使える場所に置く</para>
/// </summary>
namespace EditorWidgets {

// std::string版InputTextのコールバック
inline int InputTextCallback(ImGuiInputTextCallbackData* data) {
	// 文字が増えて入れ物が足りなくなったら、std::stringを伸ばして付け替える（ImGui公式の misc/cpp/imgui_stdlib と同じ仕組み）
	if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
		auto* text = static_cast<std::string*>(data->UserData);
		text->resize(static_cast<size_t>(data->BufTextLen));
		data->Buf = text->data();
	} else if (data->EventFlag == ImGuiInputTextFlags_CallbackAlways) {
		// 日本語の変換が始まったら、選んでいた文字を先に消す（メモ帳などと同じ。変換中の文字がその場所に出る）
		if (ImeInput::IsComposing() && data->HasSelection()) {
			const int start = (std::min)(data->SelectionStart, data->SelectionEnd);
			data->DeleteChars(start, std::abs(data->SelectionEnd - data->SelectionStart));
		}
	}
	return 0;
}

// ファイル名の入力欄のコールバック。使えない文字（FileNameRule.h）は、打っても入らない
inline int FileNameCallback(ImGuiInputTextCallbackData* data) {
	if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter) {
		return IsFileNameChar(data->EventChar) ? 0 : 1; // 1 を返すと、その文字は入らない
	}
	return InputTextCallback(data); // 文字が増えたら std::string を伸ばす
}

/// <summary>
/// ファイル名の入力欄（std::string 版）。英数字と _ - しか打てない（シーン・フォルダの名前など）
/// </summary>
inline bool InputFileName(const char* label, std::string& text, ImGuiInputTextFlags flags = 0) {
	flags |= ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_CallbackCharFilter;
	return ImGui::InputText(label, text.data(), text.capacity() + 1, flags, FileNameCallback, &text);
}

/// <summary>
/// std::string をそのまま編集する入力欄。長さの上限が無いので、日本語でも途中で切れない
/// </summary>
inline bool InputText(const char* label, std::string& text, ImGuiInputTextFlags flags = 0) {
	flags |= ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_CallbackAlways;
	return ImGui::InputText(label, text.data(), text.capacity() + 1, flags, InputTextCallback, &text);
}

/// <summary>
/// std::string を編集する、何行もある入力欄（Enter で改行）。size が 0 の向きは自動（横は欄の幅、縦は4行くらい）
/// </summary>
inline bool InputTextMultiline(const char* label, std::string& text, const ImVec2& size = ImVec2(0.0f, 0.0f), ImGuiInputTextFlags flags = 0) {
	flags |= ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_CallbackAlways;
	return ImGui::InputTextMultiline(label, text.data(), text.capacity() + 1, size, flags, InputTextCallback, &text);
}

/// <summary>
/// 大きな「＋」付きのボタン。labelの「##」より前が＋の右に出る文字（"##create" なら＋だけ）
/// <para>sizeの考え方はImGui::Buttonと同じ（0なら中身に合わせる、-FLT_MINなら横幅いっぱい）</para>
/// </summary>
inline bool PlusButton(const char* label, ImVec2 size = ImVec2(0.0f, 0.0f)) {
	const ImGuiStyle& style = ImGui::GetStyle();
	const char* textEnd = std::strstr(label, "##");
	if (!textEnd) {
		textEnd = label + std::strlen(label);
	}
	const bool hasText = (textEnd != label);
	const float iconSize = ImGui::GetFontSize() * 0.7f;    // ＋の縦横の長さ
	const float iconTextGap = ImGui::GetFontSize() * 0.5f; // ＋と文字の間隔（文字の大きさに合わせる。18pxのフォントなら9px）
	if (size.x == 0.0f) {
		const float textWidth = hasText ? iconTextGap + ImGui::CalcTextSize(label, textEnd).x : 0.0f;
		size.x = style.FramePadding.x * 2.0f + iconSize + textWidth;
	}

	// ボタンそのものは文字無しで作り、上に＋と文字を自分で描く
	ImGui::PushID(label);
	const bool pressed = ImGui::Button("##plusButton", size);
	ImGui::PopID();

	const ImVec2 min = ImGui::GetItemRectMin();
	const ImVec2 max = ImGui::GetItemRectMax();
	const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
	const float half = iconSize * 0.5f;
	const float thickness = (std::max)(2.0f, ImGui::GetFontSize() * 0.12f); // 線の太さ（文字より太くして目立たせる）
	// 文字があれば左寄せ、無ければ真ん中に＋を描く
	const float centerX = hasText ? min.x + style.FramePadding.x + half : (min.x + max.x) * 0.5f;
	const float centerY = (min.y + max.y) * 0.5f;
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddLine(ImVec2(centerX - half, centerY), ImVec2(centerX + half, centerY), color, thickness);
	drawList->AddLine(ImVec2(centerX, centerY - half), ImVec2(centerX, centerY + half), color, thickness);
	if (hasText) {
		const ImVec2 textPos(centerX + half + iconTextGap, centerY - ImGui::GetFontSize() * 0.5f);
		drawList->AddText(textPos, color, label, textEnd);
	}
	return pressed;
}

// ===== 説明の吹き出し =====
inline constexpr float kTooltipWrapEm = 32.0f; // 吹き出しの文字を折り返す幅（文字の大きさの何倍か。長い説明が画面の端まで1行に伸びないように）

// 折り返す吹き出しを始める。true のときだけ中身を描いて、EndWrappedTooltip を呼ぶ
inline bool BeginWrappedTooltip() {
	if (!ImGui::BeginTooltip()) {
		return false;
	}
	ImGui::PushTextWrapPos(ImGui::GetFontSize() * kTooltipWrapEm);
	return true;
}
inline void EndWrappedTooltip() {
	ImGui::PopTextWrapPos();
	ImGui::EndTooltip();
}

/// <summary>
/// 直前の項目にマウスを乗せていたら、text を折り返した吹き出しで出す（text が nullptr なら何もしない）
/// <para>flags は足す条件（押せない項目の上でも出すなら ImGuiHoveredFlags_AllowWhenDisabled）</para>
/// </summary>
inline void ItemTooltip(const char* text, ImGuiHoveredFlags flags = 0) {
	if (text != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | flags) && BeginWrappedTooltip()) {
		ImGui::TextUnformatted(text);
		EndWrappedTooltip();
	}
}

/// <summary>
/// enum の選択肢の一覧（説明付き）。Inspector の enum の項目と EnumCombo の中身
/// <para>names は選択肢の名前、tips は選択肢の説明（names と同じ並び。無い物は nullptr）、tooltip は項目そのものの説明（無ければ nullptr）</para>
/// <para>閉じているときは「項目の説明」と「今の選択肢の説明」、開いているときは「マウスを乗せた選択肢の説明」を出す</para>
/// <para>選び直したら index を変えて true</para>
/// </summary>
inline bool EnumComboWithTips(const char* label, size_t& index, std::span<const std::string_view> names, std::span<const char* const> tips, const char* tooltip = nullptr) {
	// string_view は末尾に0があるとは限らないので、ImGuiに渡す前に std::string にする
	const std::string current = index < names.size() ? std::string(names[index]) : std::string("?");
	if (!ImGui::BeginCombo(label, current.c_str())) {
		// --- 閉じた欄 ---（開いている間は出さない。選択肢の説明とぶつからないように）
		const char* currentTip = (index < tips.size()) ? tips[index] : nullptr;
		if ((tooltip != nullptr || currentTip != nullptr) && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && BeginWrappedTooltip()) {
			if (tooltip != nullptr) {
				ImGui::TextUnformatted(tooltip);
			}
			if (currentTip != nullptr) {
				if (tooltip != nullptr) {
					ImGui::Separator();
				}
				ImGui::Text("%s：%s", current.c_str(), currentTip);
			}
			EndWrappedTooltip();
		}
		return false;
	}

	// --- 開いた一覧 ---
	bool changed = false;
	for (size_t i = 0; i < names.size(); ++i) {
		const std::string name(names[i]);
		if (ImGui::Selectable(name.c_str(), i == index)) {
			index = i;
			changed = true;
		}
		ItemTooltip((i < tips.size()) ? tips[i] : nullptr);
		if (i == index) {
			ImGui::SetItemDefaultFocus(); // 開いたときに今の選択へスクロールする
		}
	}
	ImGui::EndCombo();
	return changed;
}

/// <summary>
/// enumをコンボボックスで選ばせる。選択肢の名前はmagic_enumが型から作り、説明は enum の型の EnumTips（EnumTips.h）を出す
/// <para>tooltip は項目そのものの説明（無ければ nullptr）</para>
/// </summary>
template<typename E> bool EnumCombo(const char* label, E& value, const char* tooltip = nullptr) {
	static_assert(std::is_enum_v<E>, "EnumCombo は enum 専用です");
	constexpr auto names = magic_enum::enum_names<E>();
	static constexpr auto tips = MakeEnumTipTable<E>(); // 選択肢の説明（names と同じ並び）
	size_t index = magic_enum::enum_index(value).value_or(0);
	if (!EnumComboWithTips(label, index, names, tips, tooltip)) {
		return false;
	}
	value = magic_enum::enum_value<E>(index);
	return true;
}

} // namespace EditorWidgets