#include "LightEditor.h"

#include <externals/imgui/imgui.h>

#include "HTTEngine/Editor/Widgets/EditorWidgets.h"

namespace {
constexpr float kMaxIntensity = 100.0f; // 強さの上限（ドラッグで行き過ぎないように）
constexpr float kMaxShadowDistance = 1000.0f; // 影を出す距離の上限（m）

// 平行光源の影
void DrawDirectionalShadow(DirectionalShadowParams& shadow) {
	ImGui::SeparatorText("Shadow");
	ImGui::PushID("shadow");
	ImGui::Checkbox("Cast Shadows", &shadow.castsShadows);
	ImGui::SetItemTooltip("影を出す（ModelRenderer・PrimitiveのCast Shadowsが入っている、不透明な物が影を落とす）");
	EditorWidgets::EnumCombo("Map Size", shadow.mapSize);
	ImGui::SetItemTooltip("ライトから見た深度の絵の大きさ。大きいほど影の縁が細かいが重い");
	ImGui::DragFloat("Distance", &shadow.distance, 0.1f, 1.0f, kMaxShadowDistance, "%.1f m", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SetItemTooltip("カメラからこの距離まで影を出す（最後の1割で薄くなって消える）。短いほど、同じMap Sizeで影が細かい");
	ImGui::DragFloat("Strength", &shadow.strength, 0.01f, 0.0f, 1.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SetItemTooltip("影の濃さ。1で平行光源の光が全く届かない。Lambert・Half Lambertは環境光が無いので、1だと真っ黒になる");
	ImGui::DragFloat("Bias", &shadow.bias, 0.001f, 0.0f, 1.0f, "%.3f m", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SetItemTooltip("深度を比べる前に引く距離（m）。自分の影が縞になる（シャドウアクネ）なら上げる。影が物から浮く（ピーターパン）なら下げる");
	ImGui::PopID();
}

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
	DrawDirectionalShadow(light.shadow);
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