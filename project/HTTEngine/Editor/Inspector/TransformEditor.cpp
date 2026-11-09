#include "TransformEditor.h"

#include <array>
#include <cmath>

#include <externals/imgui/imgui.h>

#include "HTTEngine/ECS/EntityManager.h"

namespace {
constexpr float kMinRatioBase = 1e-4f; // Scale をまとめて変えるとき、動かした軸の元の値がこれより小さければ、倍ではなく同じ量を足す

//=============================================================================
// X・Y・Z をまとめて変える
//=============================================================================
// before から value へ、動かした軸（ドラッグ・入力で変わるのは1つだけ）を見つけて、残りの2つも合わせる
// keepRatio（Scale）：同じ「倍」にする（形を保つ。Unity の Constrain Proportions と同じ）
// 元が 0 の軸を動かしたときは倍が出せないので、同じ「量」を足す（全部 0 にした後でも、また大きくできるように）
// それ以外（Position・Rotation）：同じ「量」を足す
void ApplyLinked(const Vector3& before, Vector3& value, bool keepRatio) {
	const std::array<float, 3> old = {before.x, before.y, before.z};
	std::array<float, 3> now = {value.x, value.y, value.z};
	// --- 動かした軸を探す（無ければ何もしない）---
	int moved = -1;
	for (int axis = 0; axis < 3; ++axis) {
		if (now[axis] != old[axis]) {
			moved = axis;
			break;
		}
	}
	if (moved < 0) {
		return;
	}
	// --- 残りの2つを合わせる ---
	const bool useRatio = keepRatio && std::abs(old[moved]) >= kMinRatioBase;
	const float ratio = useRatio ? now[moved] / old[moved] : 1.0f; // 何倍になったか
	const float delta = now[moved] - old[moved];                   // どれだけ増えたか
	for (int axis = 0; axis < 3; ++axis) {
		if (axis != moved) {
			now[axis] = useRatio ? old[axis] * ratio : old[axis] + delta;
		}
	}
	value = {now[0], now[1], now[2]};
}

// 1行分：左に「XYZ」ボタン（押すたびに、X・Y・Z をまとめて変えるかを切り替える）、右に DragFloat3。値が変わったら true
// ボタンの分だけ DragFloat3 を短くして、項目名の位置をほかの Component の行とそろえる
bool DrawVector3Row(const char* label, Vector3& value, float speed, bool& isLinked, bool keepRatio) {
	ImGui::PushID(label);
	// --- まとめている間は、押した色にしておく（今どちらか見て分かるように）---
	const bool wasLinked = isLinked;
	if (wasLinked) {
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
	}
	if (ImGui::Button("XYZ")) {
		isLinked = !isLinked;
	}
	if (wasLinked) {
		ImGui::PopStyleColor();
	}
	ImGui::SetItemTooltip(keepRatio ? "押すと X・Y・Z を一緒に変える（形を保ったまま、同じ倍にする）" : "押すと X・Y・Z を一緒に変える（同じだけ足す）");

	// --- 数値 ---
	const float buttonWidth = ImGui::GetItemRectSize().x + ImGui::GetStyle().ItemInnerSpacing.x;
	ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
	ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - buttonWidth);
	const Vector3 before = value;
	const bool changed = ImGui::DragFloat3(label, &value.x, speed);
	if (changed && isLinked) {
		ApplyLinked(before, value, keepRatio);
	}
	ImGui::PopID();
	return changed;
}
} // namespace

void TransformEditor::DrawComponent(TransformComponent& transform) const {
	DrawVector3Row("Position", transform.translation, 0.05f, isLinked_[0], false);

	// 回転は中ではクォータニオンで持っているので、見せるときだけオイラー角（度）に直す（GetEulerAngles：最後に決めた角度に一番近い表し方）
	// 何も触っていないフレームは、Component を書き換えない（書き換えると、Undo の対象に入ってしまう）
	// 角度は度のまま持っているので、触っていない軸の数字は1文字も変わらない（前はラジアンとの往復で、少しずれることがあった）
	Vector3 euler = GetEulerAngles(transform);
	if (DrawVector3Row("Rotation", euler, 0.5f, isLinked_[1], false)) {
		SetEulerAngles(transform, euler);
	}

	DrawVector3Row("Scale", transform.scale, 0.01f, isLinked_[2], true);

	if (ImGui::Button("Reset")) {
		transform.translation = {0.0f, 0.0f, 0.0f};
		SetEulerAngles(transform, {0.0f, 0.0f, 0.0f});
		transform.scale = {1.0f, 1.0f, 1.0f};
	}
	// --- 計算結果（読み取り専用）---
	// EntityManager::UpdateTransformsが作った、親の行列まで掛けた結果。親子が繋がっているかの確認に使う
	const Matrix4x4& world = transform.worldMatrix;
	ImGui::TextDisabled("World Position: %.3f, %.3f, %.3f", world.m[3][0], world.m[3][1], world.m[3][2]);
	const Quaternion& q = transform.rotation; // 中に入っている回転そのもの（x・y・z は 回す軸 × sin(角度/2)、w は cos(角度/2)）
	ImGui::TextDisabled("Quaternion: %.3f, %.3f, %.3f, %.3f", q.x, q.y, q.z, q.w);
	if (ImGui::TreeNode("World Matrix")) {
		for (int row = 0; row < 4; ++row) {
			ImGui::Text("%8.3f %8.3f %8.3f %8.3f", world.m[row][0], world.m[row][1], world.m[row][2], world.m[row][3]);
		}
		ImGui::TreePop();
	}
}