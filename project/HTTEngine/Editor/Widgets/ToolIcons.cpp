#ifdef USE_IMGUI
#include "ToolIcons.h"

#include <algorithm>
#include <cmath>

#include "HTTEngine/Math/MathUtility.h"

namespace {
constexpr float kButtonAspect = 1.35f; // ボタンの横の長さ（高さの何倍か）
constexpr float kIconScale = 0.62f;    // 絵の大きさ（ボタンの高さの何倍か）

// ===== p から dir（長さ1）の向きへ尖った、矢汁の頭（三角）を描く。length は頭の長さ =====
void AddArrowHead(ImDrawList* drawList, const ImVec2& p, const ImVec2& dir, float length, ImU32 color) {
	const ImVec2 side(-dir.y, dir.x); // dir に直角な向き
	const float width = length * 0.6f;
	const ImVec2 tip(p.x + dir.x * length * 0.5f, p.y + dir.y * length * 0.5f);
	const ImVec2 back(p.x - dir.x * length * 0.5f, p.y - dir.y * length * 0.5f);
	drawList->AddTriangleFilled(tip, ImVec2(back.x + side.x * width, back.y + side.y * width), ImVec2(back.x - side.x * width, back.y - side.y * width), color);
}

// ===== from から to へ線を引いて、to の所に矢印の頭を描く =====
void AddArrow(ImDrawList* drawList, const ImVec2& from, const ImVec2& to, float headLength, float thickness, ImU32 color) {
	const float dx = to.x - from.x;
	const float dy = to.y - from.y;
	const float length = std::sqrt(dx * dx + dy * dy);
	if (length <= 0.0f) {
		return;
	}
	const ImVec2 dir(dx / length, dy / length);
	// 線は頭の付け根まで（頭の先から線がはみ出さないように）
	const ImVec2 lineEnd(to.x - dir.x * headLength * 0.5f, to.y - dir.y * headLength * 0.5f);
	drawList->AddLine(from, lineEnd, color, thickness);
	AddArrowHead(drawList, to, dir, headLength, color);
}

// center のまわりの円の一部（角度は +X から時計回り。ImGui の画面は Y が下向きなので）を描いて、終わりに矢印の頭を付ける
void AddArcArrow(ImDrawList* drawList, const ImVec2& center, float radius, float startAngle, float endAngle, float headLength, float thickness, ImU32 color) {
	drawList->PathArcTo(center, radius, startAngle, endAngle - headLength * 0.4f / radius); // 頭の付け根まで
	drawList->PathStroke(color, ImDrawFlags_None, thickness);
	// 円の接線（角度が増える向き）＝(-sin, cos)
	const ImVec2 end(center.x + std::cos(endAngle) * radius, center.y + std::sin(endAngle) * radius);
	AddArrowHead(drawList, end, ImVec2(-std::sin(endAngle), std::cos(endAngle)), headLength, color);
}
} // namespace


//=============================================================================
// 描く
//=============================================================================
void ToolIcons::Draw(ImDrawList* drawList, ToolIcon icon, const ImVec2& center, float size, ImU32 color) {
	const float h = size * 0.5f; // 半分の長さ
	const float thickness = (std::max)(1.5f, size * 0.11f); // 線の太さ
	const float head = size * 0.3f; // 矢印の頭の長さ
	const auto at = [&center, size](float x, float y) { return ImVec2(center.x + x * size, center.y + y * size); }; // 真ん中からのずれ（size に対する割合）

	switch (icon) {
	case ToolIcon::Play:
		drawList->AddTriangleFilled(at(-0.32f, -0.42f), at(-0.32f, 0.42f), at(0.42f, 0.0f), color);
		break;
	case ToolIcon::Stop:
		drawList->AddRectFilled(at(-0.34f, -0.34f), at(0.34f, 0.34f), color, size * 0.06f);
		break;
	case ToolIcon::Pause:
		drawList->AddRectFilled(at(-0.33f, -0.38f), at(-0.09f, 0.38f), color, size * 0.04f);
		drawList->AddRectFilled(at(0.09f, -0.38f), at(0.33f, 0.38f), color, size * 0.04f);
		break;
	case ToolIcon::Restart:
		// 上の真ん中から少し右で始まり、時計回りにほぼ1周（左上で終わって矢印）
		AddArcArrow(drawList, center, h * 0.78f, -MathUtility::kPi * 0.35f, MathUtility::kPi * 1.25f, head, thickness, color);
		break;

	// ===== ギズモの道具 =====
	case ToolIcon::Move:
		// 真ん中から上下左右へ4本の矢印
		AddArrow(drawList, center, at(0.0f, -0.48f), head, thickness, color);
		AddArrow(drawList, center, at(0.0f, 0.48f), head, thickness, color);
		AddArrow(drawList, center, at(-0.48f, 0.0f), head, thickness, color);
		AddArrow(drawList, center, at(0.48f, 0.0f), head, thickness, color);
		break;
	case ToolIcon::Rotate:
		// 上半分と下半分に、同じ向きに回る矢印を2本（Restart の1本と見分けられるように）
		AddArcArrow(drawList, center, h * 0.8f, MathUtility::kPi * 1.1f, MathUtility::kPi * 1.9f, head, thickness, color);
		AddArcArrow(drawList, center, h * 0.8f, MathUtility::kPi * 0.1f, MathUtility::kPi * 0.9f, head, thickness, color);
		break;
	case ToolIcon::Scale:
		// 外の四角（線）・左下の小さい四角（塗り）・右上へ伸びる矢印
		drawList->AddRect(at(-0.44f, -0.44f), at(0.44f, 0.44f), color, 0.0f, ImDrawFlags_None, thickness * 0.7f);
		drawList->AddRectFilled(at(-0.44f, 0.06f), at(-0.06f, 0.44f), color);
		AddArrow(drawList, at(-0.06f, 0.06f), at(0.3f, -0.3f), head * 0.9f, thickness, color);
		break;

	// ===== ギズモの向き =====
	case ToolIcon::Local: {
		// 傾いた2本の軸（物の向きに合わせて傾いている、の意味）
		const ImVec2 origin = at(-0.2f, 0.3f);
		AddArrow(drawList, origin, at(0.38f, 0.1f), head, thickness, color);
		AddArrow(drawList, origin, at(-0.4f, -0.28f), head, thickness, color);
		drawList->AddCircleFilled(origin, thickness * 1.2f, color);
		break;
	}
	case ToolIcon::World:
		// 地球（円・縦の楕円・横の線）
		drawList->AddCircle(center, h * 0.86f, color, 0, thickness * 0.8f);
		drawList->AddEllipse(center, ImVec2(h * 0.38f, h * 0.86f), color, 0.0f, 0, thickness * 0.8f);
		drawList->AddLine(at(-0.43f, 0.0f), at(0.43f, 0.0f), color, thickness * 0.8f);
		break;
	case ToolIcon::Snap: {
		// 磁石（U の字）。下の丸い所と、上へ伸びる2本の足
		const float legX = size * 0.28f;
		const float bendY = center.y + size * 0.04f;
		drawList->PathArcTo(ImVec2(center.x, bendY), legX, 0.0f, MathUtility::kPi); // 右から下を通って左へ（Y が下向きなので、0〜π が下半分）
		drawList->PathStroke(color, ImDrawFlags_None, thickness * 1.6f);
		drawList->AddLine(ImVec2(center.x - legX, bendY), at(-0.28f, -0.42f), color, thickness * 1.6f);
		drawList->AddLine(ImVec2(center.x + legX, bendY), at(0.28f, -0.42f), color, thickness * 1.6f);
		break;
	}
	}
}

//=============================================================================
// ボタン
//=============================================================================
bool ToolIcons::Button(const char* id, ToolIcon icon, bool selected, const char* tooltip, ImU32 iconColor) {
	// 選んでいない時は目立たない色（入力欄と同じ）、選んでいる時は押している時の色
	ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(selected ? ImGuiCol_ButtonActive : ImGuiCol_FrameBg));
	ImGui::PushID(id);
	const bool pressed = ImGui::Button("##toolIcon", ImVec2(GetButtonWidth(), ImGui::GetFrameHeight()));
	ImGui::PopID();
	ImGui::PopStyleColor();
	if (tooltip != nullptr) {
		ImGui::SetItemTooltip("%s", tooltip); // 押せない（BeginDisabled）ボタンでも出る
	}

	// ボタンの真ん中に絵を描く。GetColorU32 は、押せない間の薄さ（Disabled の透明度）も掛けてくれる
	const ImVec2 min = ImGui::GetItemRectMin();
	const ImVec2 max = ImGui::GetItemRectMax();
	const ImU32 color = (iconColor != 0) ? ImGui::GetColorU32(iconColor) : ImGui::GetColorU32(ImGuiCol_Text);
	Draw(ImGui::GetWindowDrawList(), icon, ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f), ImGui::GetFrameHeight() * kIconScale, color);
	return pressed;
}

float ToolIcons::GetButtonWidth() { return std::floor(ImGui::GetFrameHeight() * kButtonAspect); }
#endif