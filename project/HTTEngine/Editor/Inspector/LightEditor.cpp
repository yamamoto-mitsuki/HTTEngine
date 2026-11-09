#include "LightEditor.h"

#include <externals/imgui/imgui.h>

namespace {
constexpr float kMaxIntensity = 100.0f; // 強さの上限（ドラッグで行き過ぎないように）

// このライトのギズモの表示（全体のON / OFFはメニューバーの View）
// PushIDで名前の縄張りを作る。そうしないと、同じ区画の中に "Range" が2つ（スポットライトの距離とこのチェック）できて、
// ImGuiが「同じIDの項目が2つある」と怒る（ImGuiは項目を見た目ではなく「名前から作ったID」で区別している）
void DrawGizmoFlags(LightGizmoFlags& gizmo) {
	ImGui::PushID("gizmo");
	ImGui::TextDisabled("Gizmo");
	ImGui::SameLine();
	ImGui::Checkbox("Icon", &gizmo.showIcon);
	ImGui::SameLine();
	ImGui::Checkbox("Range", &gizmo.showRange);
	ImGui::PopID();
}
} // namespace


//=============================================================================
// 平行光源
//=============================================================================
void DirectionalLightEditor::DrawComponent(DirectionalLightComponent& light) const {
	ImGui::ColorEdit3("Color", &light.color.x);
	ImGui::DragFloat("Intensity", &light.intensity, 0.01f, 0.0f, kMaxIntensity);
	DrawGizmoFlags(light.gizmo); // Iconは太陽の絵、Rangeは向きの線
}


//=============================================================================
// ポイントライト
//=============================================================================
void PointLightEditor::DrawComponent(PointLightComponent& light) const {
	ImGui::ColorEdit3("Color", &light.color.x);
	ImGui::DragFloat("Intensity", &light.intensity, 0.01f, 0.0f, kMaxIntensity);
	ImGui::DragFloat("Radius", &light.radius, 0.05f, 0.0f, 1000.0f);
	ImGui::DragFloat("Decay", &light.decay, 0.01f, 0.0f, 10.0f);
	DrawGizmoFlags(light.gizmo);
}

//=============================================================================
// スポットライト
//=============================================================================
void SpotLightEditor::DrawComponent(SpotLightComponent& light) const {
	ImGui::ColorEdit3("Color", &light.color.x);
	ImGui::DragFloat("Intensity", &light.intensity, 0.01f, 0.0f, kMaxIntensity);
	ImGui::DragFloat("Range", &light.range, 0.05f, 0.0f, 1000.0f);
	ImGui::DragFloat("Decay", &light.decay, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Outer Angle", &light.outerAngle, 0.1f, 0.0f, SpotLightComponent::kMaxAngle);
	ImGui::DragFloat("Inner Angle", &light.innerAngle, 0.1f, 0.0f, light.outerAngle); // 外側より大きくできないようにする
	DrawGizmoFlags(light.gizmo);
}