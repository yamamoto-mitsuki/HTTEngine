#ifdef USE_IMGUI
#include "CurveEditor.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include <externals/magic_enum/magic_enum.hpp>

#include "HTTEngine/Curve/CurveManager.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/Editor/Keymap/EditorKeymap.h"
#include "HTTEngine/Math/MathUtility.h"

namespace {
constexpr float kKeySize = 5.0f;           // キーの印の大きさ（ピクセル）
constexpr float kHandleSize = 4.0f;        // ハンドルの先の丸の大きさ
constexpr float kHitDistance = 9.0f;       // キー・ハンドルをつかめる距離
constexpr float kOnCurveDistance = 6.0f;   // ダブルクリックを「線の上」とみなす距離
constexpr float kCurveThickness = 2.0f;    // カーブの線の太さ
constexpr float kGridMinPixels = 56.0f;    // 目盛りの間隔の下限（ピクセル）
constexpr int kMaxGridLines = 128;         // 目盛りの線の数の上限
constexpr float kZoomStep = 1.15f;         // ホイール1目盛りの拡大率
constexpr float kFrameMargin = 0.12f;      // 全体を見るときの余白（見る幅に対する割合）
constexpr float kMinViewSpan = 0.001f;     // 見る幅の下限
constexpr float kViewPrecision = 0.00001f; // 見る幅の下限（見ている場所の大きさに対する割合。これより狭いと float で両端が同じ値になる）
constexpr float kPresetWidth = 130.0f;     // Ease Preset の欄の幅

constexpr float kGridAlpha = 0.25f; // 目盛りの線の濃さ（灰色の文字の色に掛ける）
constexpr float kAxisAlpha = 0.7f;  // 0 の線の濃さ

// 背景・目盛り・キーはテーマの色を使う（エディタの色はリニアなので、暗い色を IM_COL32 で書くと明るく出る）
constexpr ImU32 kCurveColor = IM_COL32(90, 200, 255, 255);    // カーブの線（UI Screen の枠と同じ水色）
constexpr ImU32 kOutsideColor = IM_COL32(90, 200, 255, 90);   // カーブの端より外の線
constexpr ImU32 kSelectedColor = IM_COL32(255, 160, 40, 255); // 選んでいるキーとハンドル（選んだ物の枠と同じ橙）
constexpr ImU32 kHandleColor = IM_COL32(255, 160, 40, 170);   // ハンドルの線

// ===== そのキーの入りのハンドルを使っているか =====
// 区間の補間は左のキーで決まるので、1つ前のキーを見る
bool HasInHandle(const std::vector<CurveKey>& keys, int index) { return index > 0 && keys[index - 1].interp == CurveInterp::Bezier; }

// ===== そのキーの出のハンドルを使っているか =====
bool HasOutHandle(const std::vector<CurveKey>& keys, int index) { return index + 1 < static_cast<int>(keys.size()) && keys[index].interp == CurveInterp::Bezier; }

// ===== 見ている範囲を守る =====
// 幅が 0 になると割り算ができず、float で表せないほど狭いと両端が同じ値になる
void ClampViewAxis(float& min, float& max) {
	const float center = std::clamp((min + max) * 0.5f, -Curve::kMaxValue, Curve::kMaxValue);
	const float minHalf = (std::max)(kMinViewSpan, std::abs(center) * kViewPrecision) * 0.5f;
	const float half = std::clamp((max - min) * 0.5f, minHalf, Curve::kMaxValue);
	min = center - half;
	max = center + half;
}
} // namespace

//=============================================================================
// 描く
//=============================================================================
bool CurveEditor::Draw(const char* id, Curve& curve, float canvasHeight, ComponentUI& ui, float fieldsWidth) {
	ImGui::PushID(id);
	const Curve original = curve;
	// Undo でキーが減ったときに、消えたキーを選んだままにしない
	if (selectedKey_ >= static_cast<int>(curve.GetKeys().size())) {
		selectedKey_ = -1;
		drag_ = DragTarget::None;
	}
	DrawToolbar(curve);
	if (fieldsWidth > 0.0f) {
		// --- 絵の右に数値の欄 ---
		DrawCanvas(curve, ImGui::GetContentRegionAvail().x - fieldsWidth - ImGui::GetStyle().ItemSpacing.x, canvasHeight);
		ImGui::SameLine();
		ImGui::BeginChild("##keyFields", ImVec2(fieldsWidth, canvasHeight));
		DrawKeyFields(curve, ui);
		ImGui::EndChild();
	} else {
		DrawCanvas(curve, ImGui::GetContentRegionAvail().x, canvasHeight);
		DrawKeyFields(curve, ui);
	}
	FinishEdit(curve);
	ImGui::PopID();
	return curve != original;
}

std::optional<CurveEdit> CurveEditor::TakeFinishedEdit() { return std::exchange(finishedEdit_, std::nullopt); }

//=============================================================================
// 上の並び
//=============================================================================
// ===== プリセットと Frame =====
void CurveEditor::DrawToolbar(Curve& curve) {
	// キーを選んでいて次のキーがあれば、その区間だけに入れる。それ以外は全体
	const bool isSegment = selectedKey_ >= 0 && selectedKey_ + 1 < static_cast<int>(curve.GetKeys().size());
	ImGui::SetNextItemWidth(kPresetWidth);
	if (ImGui::BeginCombo("##easePreset", "Ease Preset", ImGuiComboFlags_HeightLarge)) {
		for (const Ease::Type type : magic_enum::enum_values<Ease::Type>()) {
			const std::string name(magic_enum::enum_name(type));
			if (ImGui::Selectable(name.c_str())) {
				ApplyPreset(curve, type);
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("Ease と同じ形を入れる（キーを選んでいれば次のキーまでの区間に、選んでいなければ全体に）");
	ImGui::SameLine();
	if (ImGui::Button("Frame")) {
		needsFrame_ = true;
	}
	ImGui::SetItemTooltip("%s", EditorKeymap::WithShortcut("全体が見える範囲にする", EditorAction::Frame).c_str());
	ImGui::SameLine();
	ImGui::TextDisabled("%s", isSegment ? "Preset: Segment" : "Preset: Whole");
}

// ===== プリセットを入れる =====
void CurveEditor::ApplyPreset(Curve& curve, Ease::Type type) {
	BeginEdit(curve);
	if (selectedKey_ >= 0 && selectedKey_ + 1 < static_cast<int>(curve.GetKeys().size())) {
		curve.ApplyEaseToSegment(selectedKey_, type);
		return;
	}
	curve.ApplyEaseToAll(type);
	selectedKey_ = -1;
	needsFrame_ = true;
}

//=============================================================================
// カーブの絵
//=============================================================================
// ===== 全体 =====
void CurveEditor::DrawCanvas(Curve& curve, float canvasWidth, float canvasHeight) {
	// ボタンを置くと、つまんでいる間 IsAnyItemActive が true になる（保存と Undo の区切りに使う）
	canvasMin_ = ImGui::GetCursorScreenPos();
	canvasSize_ = ImVec2((std::max)(canvasWidth, 1.0f), (std::max)(canvasHeight, 1.0f));
	ImGui::InvisibleButton("canvas", canvasSize_, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
	ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY); // ホイールを、ウィンドウのスクロールより先に受け取る

	// --- 操作（IsItem〜 は直前の項目＝上のボタンを見るので、間に項目を置かない）---
	if (needsFrame_) {
		FrameAll(curve);
	}
	HandleView();
	HandleMouse(curve);
	HandleKeyboard(curve);

	// --- 描く（絵の外にはみ出さないように切る）---
	const ImVec2 canvasMax(canvasMin_.x + canvasSize_.x, canvasMin_.y + canvasSize_.y);
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->PushClipRect(canvasMin_, canvasMax, true);
	drawList->AddRectFilled(canvasMin_, canvasMax, ImGui::GetColorU32(ImGuiCol_FrameBg));
	DrawGridLines(drawList, true);
	DrawGridLines(drawList, false);
	DrawCurve(drawList, curve);
	DrawKeys(drawList, curve);
	// --- マウスの時間での値（カーブの計算の確認にも使える）---
	if (ImGui::IsItemHovered()) {
		const float time = ToCurve(ImGui::GetIO().MousePos).x;
		const std::string text = std::format("time {:.3f}  value {:.3f}", time, curve.Evaluate(time));
		drawList->AddText(ImVec2(canvasMax.x - ImGui::CalcTextSize(text.c_str()).x - 6.0f, canvasMin_.y + 4.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), text.c_str());
	}
	drawList->PopClipRect();
}

// ===== 拡大縮小と移動 =====
void CurveEditor::HandleView() {
	const ImGuiIO& io = ImGui::GetIO();
	// --- 移動（中ボタン・Alt + 左ボタン）---
	const bool isPanning = ImGui::IsItemActive() && (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || (io.KeyAlt && ImGui::IsMouseDown(ImGuiMouseButton_Left)));
	if (isPanning) {
		const Vector2 delta = {-io.MouseDelta.x * (viewMax_.x - viewMin_.x) / canvasSize_.x, io.MouseDelta.y * (viewMax_.y - viewMin_.y) / canvasSize_.y};
		viewMin_ += delta;
		viewMax_ += delta;
	}
	// --- 拡大縮小（マウスの下の場所が動かないように、マウスを中心に広げる）---
	if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) {
		const float zoom = std::pow(kZoomStep, -io.MouseWheel);
		const Vector2 center = ToCurve(io.MousePos);
		if (!io.KeyShift) {
			viewMin_.x = center.x + (viewMin_.x - center.x) * zoom;
			viewMax_.x = center.x + (viewMax_.x - center.x) * zoom;
		}
		if (!io.KeyCtrl) {
			viewMin_.y = center.y + (viewMin_.y - center.y) * zoom;
			viewMax_.y = center.y + (viewMax_.y - center.y) * zoom;
		}
	}
	ClampViewAxis(viewMin_.x, viewMax_.x);
	ClampViewAxis(viewMin_.y, viewMax_.y);
}

// ===== マウス（選ぶ・つまむ・足す）=====
void CurveEditor::HandleMouse(Curve& curve) {
	const ImGuiIO& io = ImGui::GetIO();
	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		drag_ = DragTarget::None;
	}
	if (!ImGui::IsMousePosValid()) {
		return;
	}
	const Vector2 mouse = ToCurve(io.MousePos);

	// --- 押した ---
	if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt) {
		const Hit hit = FindHit(curve, io.MousePos);
		selectedKey_ = hit.key;
		drag_ = hit.target;
		if (hit.target == DragTarget::None) {
			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
				AddKeyAt(curve, mouse, io.MousePos.y);
			}
		} else {
			const CurveKey& key = curve.GetKeys()[hit.key];
			Vector2 position = {key.time, key.value};
			if (hit.target == DragTarget::InHandle) {
				position += key.inHandle;
			} else if (hit.target == DragTarget::OutHandle) {
				position += key.outHandle;
			}
			dragOffset_ = position - mouse;
		}
	}

	// --- つまんで動かす（押した場所から少し動いてから。クリックだけでは値を変えない）---
	if (drag_ == DragTarget::None || !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
		return;
	}
	const Vector2 target = mouse + dragOffset_;
	BeginEdit(curve);
	curve = *editBefore_;
	const CurveKey& key = editBefore_->GetKeys()[editKey_];
	switch (drag_) {
	case DragTarget::Key: {
		CurveKey moved = key;
		moved.time = target.x;
		moved.value = target.y;
		selectedKey_ = curve.SetKey(editKey_, moved);
		break;
	}
	case DragTarget::InHandle:
		curve.SetHandle(editKey_, CurveHandleSide::In, target - Vector2{key.time, key.value});
		break;
	case DragTarget::OutHandle:
		curve.SetHandle(editKey_, CurveHandleSide::Out, target - Vector2{key.time, key.value});
		break;
	case DragTarget::None:
		break;
	}
}

// ===== キーボード =====
void CurveEditor::HandleKeyboard(Curve& curve) {
	if (!ImGui::IsItemHovered() && !ImGui::IsItemFocused()) {
		return;
	}
	if (EditorKeymap::IsTriggered(EditorAction::Frame)) {
		needsFrame_ = true;
	}
	if (selectedKey_ >= 0 && EditorKeymap::IsTriggered(EditorAction::Delete)) {
		RemoveSelectedKey(curve);
	}
}

// ===== 全体を見る =====
void CurveEditor::FrameAll(const Curve& curve) {
	needsFrame_ = false;
	const std::vector<CurveKey>& keys = curve.GetKeys();
	if (keys.empty()) {
		viewMin_ = {0.0f, 0.0f};
		viewMax_ = {1.0f, 1.0f};
		return;
	}
	// --- 値はハンドルの先まで入れる（ベジェは制御点の内側を通るので、これではみ出さない）---
	Vector2 low = {keys.front().time, keys.front().value};
	Vector2 high = {keys.back().time, keys.front().value};
	for (int i = 0; i < static_cast<int>(keys.size()); ++i) {
		const CurveKey& key = keys[i];
		const float inValue = key.value + (HasInHandle(keys, i) ? key.inHandle.y : 0.0f);
		const float outValue = key.value + (HasOutHandle(keys, i) ? key.outHandle.y : 0.0f);
		low.y = (std::min)({low.y, key.value, inValue, outValue});
		high.y = (std::max)({high.y, key.value, inValue, outValue});
	}
	const Vector2 margin = {(std::max)(high.x - low.x, kMinViewSpan) * kFrameMargin, (std::max)(high.y - low.y, kMinViewSpan) * kFrameMargin};
	viewMin_ = low - margin;
	viewMax_ = high + margin;
}

// ===== マウスの下にある物 =====
CurveEditor::Hit CurveEditor::FindHit(const Curve& curve, const ImVec2& mouse) const {
	const std::vector<CurveKey>& keys = curve.GetKeys();
	Hit hit;
	float nearest = kHitDistance * kHitDistance;
	const auto check = [&](DragTarget target, int index, const Vector2& position) {
		const ImVec2 screen = ToScreen(position);
		const float distance = (screen.x - mouse.x) * (screen.x - mouse.x) + (screen.y - mouse.y) * (screen.y - mouse.y);
		if (distance <= nearest) {
			nearest = distance;
			hit = {target, index};
		}
	};
	// --- ハンドルは選んでいるキーの分だけ（キーを後に調べるので、長さ 0 のハンドルがキーの邪魔をしない）---
	if (selectedKey_ >= 0) {
		const CurveKey& key = keys[selectedKey_];
		const Vector2 position = {key.time, key.value};
		if (HasInHandle(keys, selectedKey_)) {
			check(DragTarget::InHandle, selectedKey_, position + key.inHandle);
		}
		if (HasOutHandle(keys, selectedKey_)) {
			check(DragTarget::OutHandle, selectedKey_, position + key.outHandle);
		}
	}
	for (int i = 0; i < static_cast<int>(keys.size()); ++i) {
		check(DragTarget::Key, i, {keys[i].time, keys[i].value});
	}
	return hit;
}

// ===== キーを足す =====
void CurveEditor::AddKeyAt(Curve& curve, const Vector2& position, float mouseY) {
	BeginEdit(curve);
	const float curveY = ToScreen({position.x, curve.Evaluate(position.x)}).y;
	const bool isOnCurve = !curve.GetKeys().empty() && std::abs(curveY - mouseY) <= kOnCurveDistance;
	selectedKey_ = isOnCurve ? curve.InsertKeyOnCurve(position.x) : curve.AddKey(position.x, position.y);
	drag_ = DragTarget::None; // この操作は「足す」だけ（editBefore_ には足したキーが無いので、続けてつまめない）
}

// ===== 選んでいるキーを消す =====
void CurveEditor::RemoveSelectedKey(Curve& curve) {
	BeginEdit(curve);
	curve.RemoveKey(selectedKey_);
	selectedKey_ = -1;
	drag_ = DragTarget::None;
}

// ===== 目盛り =====
// 線の数に上限を付ける（float の足し算で進めると、大きな場所では値が進まず止まらなくなるので、番号から計算する）
void CurveEditor::DrawGridLines(ImDrawList* drawList, bool isTime) const {
	const float min = isTime ? viewMin_.x : viewMin_.y;
	const float max = isTime ? viewMax_.x : viewMax_.y;
	const float pixels = isTime ? canvasSize_.x : canvasSize_.y;
	const float step = MathUtility::CalcNiceStep(kGridMinPixels * (max - min) / pixels);
	const float first = std::ceil(min / step);
	const float lineHeight = ImGui::GetTextLineHeight();
	const ImU32 gridColor = ImGui::GetColorU32(ImGuiCol_TextDisabled, kGridAlpha);
	const ImU32 axisColor = ImGui::GetColorU32(ImGuiCol_TextDisabled, kAxisAlpha);
	const ImU32 labelColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
	for (int i = 0; i < kMaxGridLines; ++i) {
		const float position = (first + static_cast<float>(i)) * step;
		if (position > max) {
			break;
		}
		const ImVec2 screen = ToScreen({position, position});
		const ImVec2 start = isTime ? ImVec2(screen.x, canvasMin_.y) : ImVec2(canvasMin_.x, screen.y);
		const ImVec2 end = isTime ? ImVec2(screen.x, canvasMin_.y + canvasSize_.y) : ImVec2(canvasMin_.x + canvasSize_.x, screen.y);
		const bool isZero = std::abs(position) < step * 0.5f;
		drawList->AddLine(start, end, isZero ? axisColor : gridColor);
		const ImVec2 labelPosition(start.x + 3.0f, isTime ? end.y - lineHeight : start.y);
		drawList->AddText(labelPosition, labelColor, std::format("{:.6g}", isZero ? 0.0f : position).c_str());
	}
}

// ===== カーブの線 =====
void CurveEditor::DrawCurve(ImDrawList* drawList, const Curve& curve) const {
	const std::vector<CurveKey>& keys = curve.GetKeys();
	if (keys.empty()) {
		return;
	}
	// --- 端より外（値は端のキーのまま）---
	drawList->AddLine(ToScreen({viewMin_.x, keys.front().value}), ToScreen({keys.front().time, keys.front().value}), kOutsideColor, kCurveThickness);
	drawList->AddLine(ToScreen({keys.back().time, keys.back().value}), ToScreen({viewMax_.x, keys.back().value}), kOutsideColor, kCurveThickness);
	// --- 区間ごと ---
	for (size_t i = 0; i + 1 < keys.size(); ++i) {
		const CurveKey& a = keys[i];
		const CurveKey& b = keys[i + 1];
		const Vector2 start = {a.time, a.value};
		const Vector2 end = {b.time, b.value};
		switch (a.interp) {
		case CurveInterp::Constant:
			drawList->AddLine(ToScreen(start), ToScreen({b.time, a.value}), kCurveColor, kCurveThickness);
			drawList->AddLine(ToScreen({b.time, a.value}), ToScreen(end), kCurveColor, kCurveThickness);
			break;
		case CurveInterp::Linear:
			drawList->AddLine(ToScreen(start), ToScreen(end), kCurveColor, kCurveThickness);
			break;
		case CurveInterp::Bezier:
			drawList->AddBezierCubic(ToScreen(start), ToScreen(start + a.outHandle), ToScreen(end + b.inHandle), ToScreen(end), kCurveColor, kCurveThickness);
			break;
		}
	}
}

// ===== キーとハンドルの印 =====
void CurveEditor::DrawKeys(ImDrawList* drawList, const Curve& curve) const {
	const std::vector<CurveKey>& keys = curve.GetKeys();
	const ImU32 keyColor = ImGui::GetColorU32(ImGuiCol_Text);
	// --- 選んでいるキーのハンドル ---
	if (selectedKey_ >= 0) {
		const CurveKey& key = keys[selectedKey_];
		const Vector2 position = {key.time, key.value};
		const auto drawHandle = [&](const Vector2& handle) {
			drawList->AddLine(ToScreen(position), ToScreen(position + handle), kHandleColor, 1.0f);
			drawList->AddCircleFilled(ToScreen(position + handle), kHandleSize, kSelectedColor);
		};
		if (HasInHandle(keys, selectedKey_)) {
			drawHandle(key.inHandle);
		}
		if (HasOutHandle(keys, selectedKey_)) {
			drawHandle(key.outHandle);
		}
	}
	// --- キー（ひし形）---
	for (int i = 0; i < static_cast<int>(keys.size()); ++i) {
		const ImVec2 center = ToScreen({keys[i].time, keys[i].value});
		const ImVec2 top(center.x, center.y - kKeySize);
		const ImVec2 right(center.x + kKeySize, center.y);
		const ImVec2 bottom(center.x, center.y + kKeySize);
		const ImVec2 left(center.x - kKeySize, center.y);
		drawList->AddQuadFilled(top, right, bottom, left, (i == selectedKey_) ? kSelectedColor : keyColor);
	}
}

//=============================================================================
// 数値の入力
//=============================================================================
void CurveEditor::DrawKeyFields(Curve& curve, ComponentUI& ui) {
	if (selectedKey_ < 0) {
		ui.Label("キーを選ぶと、ここで数値を打てる");
		return;
	}
	const std::vector<CurveKey>& keys = curve.GetKeys();
	const CurveKey key = keys[selectedKey_];
	CurveKey edited = key;
	CurveManager::DescribeKey(ui, edited, HasInHandle(keys, selectedKey_), HasOutHandle(keys, selectedKey_));
	if (edited != key) {
		BeginEdit(curve);
		curve = *editBefore_;
		if (edited.inHandle != key.inHandle) {
			curve.SetHandle(editKey_, CurveHandleSide::In, edited.inHandle);
		} else if (edited.outHandle != key.outHandle) {
			curve.SetHandle(editKey_, CurveHandleSide::Out, edited.outHandle);
		} else {
			// 触っていないハンドルは操作の前の値を使う（時間を動かして区間が一度縮んでも、戻せば元の長さへ戻る）
			const CurveKey& base = editBefore_->GetKeys()[editKey_];
			edited.inHandle = base.inHandle;
			edited.outHandle = base.outHandle;
			selectedKey_ = curve.SetKey(editKey_, edited);
		}
	}
	if (ImGui::SmallButton("Delete Key")) {
		RemoveSelectedKey(curve);
	}
	ImGui::SetItemTooltip("選んでいるキーを消す（Delete）");
}

//=============================================================================
// 座標の変換
//=============================================================================
// ===== カーブの座標 → 画面のピクセル（値は上が大きいので、y を逆にする）=====
ImVec2 CurveEditor::ToScreen(const Vector2& position) const {
	return ImVec2(canvasMin_.x + (position.x - viewMin_.x) / (viewMax_.x - viewMin_.x) * canvasSize_.x, canvasMin_.y + (viewMax_.y - position.y) / (viewMax_.y - viewMin_.y) * canvasSize_.y);
}

// ===== 画面のピクセル → カーブの座標 =====
Vector2 CurveEditor::ToCurve(const ImVec2& screen) const {
	return {viewMin_.x + (screen.x - canvasMin_.x) / canvasSize_.x * (viewMax_.x - viewMin_.x), viewMax_.y - (screen.y - canvasMin_.y) / canvasSize_.y * (viewMax_.y - viewMin_.y)};
}

//=============================================================================
// 1回の操作
//=============================================================================
// ===== 始める（もう始まっていれば何もしない）=====
void CurveEditor::BeginEdit(const Curve& curve) {
	if (!editBefore_.has_value()) {
		editBefore_ = curve;
		editKey_ = selectedKey_;
	}
}

// ===== どの項目も操作していなければ、終わりにする =====
void CurveEditor::FinishEdit(const Curve& curve) {
	if (!editBefore_.has_value() || ImGui::IsAnyItemActive()) {
		return;
	}
	if (*editBefore_ != curve) {
		finishedEdit_ = CurveEdit{std::move(*editBefore_), curve};
	}
	editBefore_.reset();
}
#endif