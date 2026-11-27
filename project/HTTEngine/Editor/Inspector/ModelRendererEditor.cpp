#include "ModelRendererEditor.h"

#include <format>
#include <string>

#include <externals/imgui/imgui.h>

#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/Editor/Widgets/EditorWidgets.h"
#include "HTTEngine/Graphics/Model/ModelManager.h"
#include "HTTEngine/Math/Color.h"


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
	params.emissiveIntensity = imported.emissiveIntensity; // 強さは色ではないので、そのまま
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
		// --- 番号・名前・テクスチャの有無（マウスを乗せると、テクスチャのパスが出る）---
		const bool hasBaseColorTexture = !imported.baseColorTexturePath.empty();
		const bool hasEmissiveTexture = !imported.emissiveTexturePath.empty();
		const std::string label = std::format("{}  {}{}{}", index, imported.name, hasBaseColorTexture ? "  (Texture)" : "", hasEmissiveTexture ? "  (Emissive)" : "");
		ImGui::TextUnformatted(label.c_str());
		if (hasBaseColorTexture || hasEmissiveTexture) {
			const std::string paths = std::format("{}{}{}", imported.baseColorTexturePath, (hasBaseColorTexture && hasEmissiveTexture) ? "\n" : "", imported.emissiveTexturePath);
			ImGui::SetItemTooltip("%s", paths.c_str());
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
	ImGui::Checkbox("Cast Shadows", &render.castsShadows);
	ImGui::SetItemTooltip("外すと、このモデルは平行光源の影を落とさない（影を受けるのはそのまま。DepthがTestWriteのときだけ落とす）");

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
		ImGui::SetItemTooltip("自己発光の色（Unlit 以外。ライトが無くても光る）。モデルに自己発光のテクスチャがあれば、それに掛ける");
		isEdited |= ImGui::DragFloat("Emissive Intensity", &render.material.emissiveIntensity, 0.01f, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("自己発光の強さ（Emissive の色に掛ける）。1 より大きくすると HDR の明るさになり、ブルーム（Threshold 1）で光る");
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

	// --- ディゾルブ（ノイズの画像で一部だけ消す）---
	if (ImGui::TreeNode("Dissolve")) {
		DissolveParams& dissolve = render.dissolve;
		ImGui::DragFloat("Amount", &dissolve.amount, 0.005f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("消えた割合（0：全部見える 1：全部消える）。画像の暗い所から消える。UV の無いメッシュは一度に全部消える");
		ImGui::DragFloat("Edge Width", &dissolve.edgeWidth, 0.002f, 0.0f, 1.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("光る境目の幅（画像の値の幅）。0 で光らない");
		ImGui::ColorEdit3("Edge Color", &dissolve.edgeColor.x);
		ImGui::SetItemTooltip("境目の色（Unlit でも光る）");
		ImGui::DragFloat("Edge Intensity", &dissolve.edgeIntensity, 0.01f, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("境目の明るさ（Edge Color に掛ける）。1 より大きくすると HDR の明るさになり、ブルーム（Threshold 1）で光る");
		ImGui::DragInt("Scale", &dissolve.scale, 0.05f, 1, 64, "%d", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SetItemTooltip("画像を UV の 0〜1 に何回並べるか。整数なので、筒を開いた UV の端（u 0 と 1）で模様がつながる（継ぎ目なしの画像のとき）");
		ui.AssetField("Noise Texture", dissolve.noiseTextureHandle, AssetType::NoiseTexture,
		              Tip("模様の画像（灰色。暗い所から消える）。Textures/Noise のフォルダ（ゲームの resources とエンジンの Resources）から選ぶ。(none) なら既定の雲の模様"));
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
