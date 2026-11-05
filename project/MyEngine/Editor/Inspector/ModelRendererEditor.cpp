#include "ModelRendererEditor.h"

#include <externals/imgui/imgui.h>

#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/Editor/Widgets/EditorWidgets.h"
#include "MyEngine/Graphics/Model/ModelManager.h"


namespace {
//=============================================================================
// モデルのマテリアルを写す
//=============================================================================
// モデルの1つ目のメッシュの mtl / glTF の値を render.material に写す（上書きの値を、モデルの見た目から始められるように）
// 写せたら true（モデルが無い・mtl が無いなら false）
bool CopyModelMaterial(ModelRendererComponent& render) {
	// --- 早期リターン（モデルが無い・メッシュが無い）---
	const ModelManager::ModelAsset* asset = ModelManager::GetModelAsset(render.modelHandle);
	if (asset == nullptr || asset->meshes.empty()) {
		return false;
	}
	const ModelManager::MtlMaterial* mtl = ModelManager::GetMtlMaterial(render.modelHandle, asset->meshes.front().materialName);
	if (mtl == nullptr) {
		return false;
	}
	// --- 写す（不透明度 d と切り抜きは MaterialParams に無い・mtl に無いので、そのまま）---
	render.material.ambient = mtl->ambient;
	render.material.diffuse = mtl->diffuse;
	render.material.specular = mtl->specular;
	render.material.emissive = mtl->emissive;
	render.material.shininess = mtl->shininess;
	render.material.metallic = mtl->metallic;
	render.material.roughness = mtl->roughness;
	return true;
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
	ui.AssetField("Texture", render.textureHandle, AssetType::Texture, Tip("(none) なら、モデルのマテリアルのテクスチャを使う"));

	// --- 描画設定（enumはmagic_enumが名前を作る）---
	EditorWidgets::EnumCombo("Shading", render.shadingType);
	EditorWidgets::EnumCombo("Blend", render.blendMode);
	EditorWidgets::EnumCombo("Rasterizer", render.rasterizerType);
	EditorWidgets::EnumCombo("Depth", render.depthMode);
	EditorWidgets::EnumCombo("Billboard", render.billboard);
	ImGui::Checkbox("Receive Decals", &render.receivesDecals);
	ImGui::SetItemTooltip("外すと、デカールの箱の中にあっても、このモデルには塗られない（不透明なときだけ効く）");

	// --- 色（sRGB の 0〜1）---
	ImGui::ColorEdit4("Color", &render.color.x);
	ImGui::DragFloat("Intensity", &render.intensity, 0.01f, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SetItemTooltip("明るさの倍率（色はそのまま）。1 より大きくすると HDR の明るさになり、ブルーム（Threshold 1）で光る");

	// --- マテリアルの調整（Override を入れると、モデルの mtl / glTF の値の代わりにこの値を使う）---
	if (ImGui::TreeNode("Material")) {
		ImGui::Checkbox("Override", &render.overrideMaterial);
		ImGui::SetItemTooltip("入れると、下の値をモデルの mtl / glTF の値の代わりに使う\n外すと、mtl に無いメッシュだけ下の値を使う（下の値を触ると、自動で入る）");
		ImGui::SameLine();
		// モデルの値から調整を始められるように、1つ目のメッシュの値を写して上書きを入れる
		if (ImGui::Button("Copy From Model") && CopyModelMaterial(render)) {
			render.overrideMaterial = true;
		}
		ImGui::SetItemTooltip("モデルの1つ目のメッシュの mtl / glTF の値を、下に写す");

		// ===== 値（どのシェーディングで使うかは、マウスを乗せると出る）=====
		bool isEdited = false; // どれか1つでも触ったか
		isEdited |= ImGui::ColorEdit3("Ambient", &render.material.ambient.x);
		ImGui::SetItemTooltip("影の所の明るさ（PBR で、環境光（IBL）が無いときだけ使う）");
		isEdited |= ImGui::ColorEdit3("Diffuse", &render.material.diffuse.x);
		ImGui::SetItemTooltip("拡散反射の色（今のシェーダーでは使っていない。色は Color で付ける）");
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
		// 切り抜きは mtl に無いので、Override に関係なくいつも効く
		ImGui::DragFloat("Alpha Cutoff", &render.material.alphaCutoff, 0.005f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("これ以下のアルファのピクセルを捨てる（Override に関係なく効く）");
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