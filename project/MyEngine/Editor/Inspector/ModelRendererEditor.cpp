#include "ModelRendererEditor.h"

#include <format>
#include <string>

#include <externals/imgui/imgui.h>

#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/Editor/Widgets/EditorWidgets.h"
#include "MyEngine/Graphics/Model/ModelManager.h"
#include "MyEngine/Math/Color.h"


namespace {
//=============================================================================
// 読み込んだマテリアル
//=============================================================================
// ===== 1つを上書きの値（render.material）に写す =====
// 上書きの値を、モデルの見た目から始められるように。読み込んだ色はリニア、Inspector の欄は sRGB なので直して写す
// 切り抜きは読み込んだマテリアルに無い値なので、そのまま
void CopyImportedMaterial(const ModelManager::ImportedMaterial& imported, MaterialParams& params) {
	params.ambient = LinearToSrgb(imported.ambient);
	params.baseColor = LinearToSrgb(imported.baseColor); // A（不透明度）はそのまま
	params.specular = LinearToSrgb(imported.specular);
	params.emissive = LinearToSrgb(imported.emissive);
	params.shininess = imported.shininess;
	params.metallic = imported.metallic;
	params.roughness = imported.roughness;
}

// ===== 一覧（番号・色の見本・名前・テクスチャの有無・Copy）=====
// 番号はマテリアルの枠の番号（メッシュが使うマテリアルの番号）。後で枠ごとに .material を選べるようにするときは、この行に欄が並ぶ
void DrawImportedMaterials(ModelRendererComponent& render) {
	const ModelManager::ModelAsset* asset = ModelManager::GetModelAsset(render.modelHandle);
	if (asset == nullptr) {
		ImGui::TextDisabled("(モデルが選ばれていない)");
		return;
	}
	for (size_t index = 0; index < asset->materials.size(); ++index) {
		const ModelManager::ImportedMaterial& imported = asset->materials[index];
		ImGui::PushID(static_cast<int>(index));
		// --- 色の見本（読むだけ。見本の色は sRGB で見せる）---
		const Vector4 swatch = LinearToSrgb(imported.baseColor);
		ImGui::ColorButton("##baseColor", ImVec4(swatch.x, swatch.y, swatch.z, swatch.w), ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_AlphaPreviewHalf);
		ImGui::SameLine();
		// --- 番号・名前・テクスチャの有無 ---
		const std::string label = std::format("{}  {}{}", index, imported.name, imported.baseColorTexturePath.empty() ? "" : "  (Texture)");
		ImGui::TextUnformatted(label.c_str());
		if (!imported.baseColorTexturePath.empty()) {
			ImGui::SetItemTooltip("%s", imported.baseColorTexturePath.c_str());
		}
		ImGui::SameLine();
		// --- 上書きの値に写して、Override を入れる ---
		if (ImGui::SmallButton("Copy")) {
			CopyImportedMaterial(imported, render.material);
			render.overrideMaterial = true;
		}
		ImGui::SetItemTooltip("このマテリアルの値を下の上書きの値に写して、Override を入れる");
		ImGui::PopID();
	}
}
} // namespace

//=============================================================================
// Inspectorの中身
//=============================================================================
void ModelRendererEditor::DrawComponent(ModelRendererComponent& render) const {
	ImGui::Checkbox("Enabled", &render.enabled);

	// --- どのモデル・テクスチャを使うか（resources以下のファイルから選ぶ。ComponentUIのアセット欄と同じ物）---
	ComponentUI& ui = GetInspectorUI();
	ui.AssetField("Model", render.modelHandle, AssetType::Model);
	ui.AssetField("Texture", render.textureHandle, AssetType::Texture, Tip("(none) なら、モデルのマテリアルのテクスチャを使う（選ぶと全部のマテリアルのテクスチャを差し替える。マテリアルの色は掛かる）"));

	// --- 描画設定（enumはmagic_enumが名前を作る）---
	EditorWidgets::EnumCombo("Shading", render.shadingType);
	EditorWidgets::EnumCombo("Blend", render.blendMode);
	EditorWidgets::EnumCombo("Rasterizer", render.rasterizerType);
	EditorWidgets::EnumCombo("Depth", render.depthMode);
	EditorWidgets::EnumCombo("Billboard", render.billboard);
	ImGui::Checkbox("Receive Decals", &render.receivesDecals);
	ImGui::SetItemTooltip("外すと、デカールの箱の中にあっても、このモデルには塗られない（不透明なときだけ効く）");

	// --- 色（sRGB の 0〜1。物ごとの色で、マテリアルの色に掛ける）---
	ImGui::ColorEdit4("Color", &render.color.x);
	ImGui::SetItemTooltip("このモデルだけの色合い。マテリアルの色（Base Color）とテクスチャに掛ける（白ならマテリアルの色のまま）");
	ImGui::DragFloat("Intensity", &render.intensity, 0.01f, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SetItemTooltip("明るさの倍率（色はそのまま）。1 より大きくすると HDR の明るさになり、ブルーム（Threshold 1）で光る");

	// --- マテリアル（読み込んだマテリアルの一覧と、Override を入れたときに代わりに使う値）---
	if (ImGui::TreeNode("Material")) {
		// ===== 読み込んだマテリアル =====
		ImGui::SeparatorText("Model Materials");
		DrawImportedMaterials(render);

		// ===== 上書き =====
		ImGui::Separator();
		ImGui::Checkbox("Override", &render.overrideMaterial);
		ImGui::SetItemTooltip("入れると、下の値を読み込んだマテリアルの値の代わりに使う（全部のメッシュに同じ値。テクスチャは読み込んだ物のまま）\n色の違うマテリアルを持つモデルでは、全部が同じ色になる（下の値を触ると、自動で入る）");

		// ===== 値（どのシェーディングで使うかは、マウスを乗せると出る）=====
		bool isEdited = false; // どれか1つでも触ったか
		isEdited |= ImGui::ColorEdit4("Base Color", &render.material.baseColor.x);
		ImGui::SetItemTooltip("物の色（読み込んだマテリアルの色の代わり。Color とテクスチャに掛ける）。A は不透明度");
		isEdited |= ImGui::ColorEdit3("Ambient", &render.material.ambient.x);
		ImGui::SetItemTooltip("影の所の明るさ（PBR で、環境光（IBL）が無いときだけ使う）");
		isEdited |= ImGui::ColorEdit3("Specular", &render.material.specular.x);
		ImGui::SetItemTooltip("ハイライトの色（Phong・BlinnPhong。黒ならハイライト無し）");
		isEdited |= ImGui::ColorEdit3("Emissive", &render.material.emissive.x);
		ImGui::SetItemTooltip("自己発光の色（Unlit 以外。ライトが無くても光る）");
		isEdited |= ImGui::DragFloat("Shininess", &render.material.shininess, 0.5f, 0.0f, 256.0f);
		ImGui::SetItemTooltip("ハイライトの鋭さ（Phong・BlinnPhong。大きいほど小さく鋭い）");
		isEdited |= ImGui::DragFloat("Metallic", &render.material.metallic, 0.005f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("金属っぽさ（PBR。0=プラスチック・木 1=金属）");
		isEdited |= ImGui::DragFloat("Roughness", &render.material.roughness, 0.005f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("表面の粗さ（PBR。0=鏡のよう 1=ざらざら）");
		// 値を触ったら、すぐ見た目に出るように上書きを入れる（入れ忘れて「効かない」とならないように）
		if (isEdited) {
			render.overrideMaterial = true;
		}
		// 切り抜きは読み込んだマテリアルに無いので、Override に関係なくいつも効く
		ImGui::DragFloat("Alpha Cutoff", &render.material.alphaCutoff, 0.005f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("これ以下のアルファ（マテリアルの A × テクスチャの A。Color の A は入らない）のピクセルを捨てる（Override に関係なく効く）");
		ImGui::TreePop();
	}

	// --- テクスチャのUVをずらす・回す（Vector3の先頭アドレスを渡してx,yだけ触る）---
	if (ImGui::TreeNode("UV Transform")) {
		ImGui::DragFloat2("Offset", &render.uvTransform.translation.x, 0.01f);
		ImGui::DragFloat2("Tiling", &render.uvTransform.scale.x, 0.01f);
		ImGui::DragFloat("Rotate", &render.uvTransform.rotation.z, 0.01f);
		ImGui::TreePop();
	}
}
