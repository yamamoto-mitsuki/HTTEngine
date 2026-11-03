#include "MyEngine/ECS/ComponentSerializer.h"

#include <algorithm>
#include <cstdint>

#include "MyEngine/Components/Core/FolderComponent.h"
#include "MyEngine/Components/Core/PrefabInstanceComponent.h"
#include "MyEngine/Components/Rendering3D/ModelRendererComponent.h"
#include "MyEngine/Components/Core/TransformComponent.h"
#include "MyEngine/Components/Lighting/LightComponent.h"
#include "MyEngine/Scene/Prefab/PrefabManager.h"
#include "MyEngine/String/ConvertString.h"

// エンジンのComponentの「保存する項目」。書き方は COMPONENT(...) の中身と同じ
// Inspectorは Editor/Inspector の手書きのEditorのまま（度で見せる・Resetボタンなど、細かく作り込んでいるため）
// ここは保存の名前を決めるだけなので、ラベルはメンバ変数の名前にそろえる
// Componentに項目を足したら、ここにも1行足す（足し忘れると、その項目は保存されず、Stopで初期値に戻る）

namespace {
//=============================================================================
// Transform
//=============================================================================
void DescribeTransform(ComponentUI& ui, TransformComponent& value) {
	ui.Field("translation", value.translation);
	// 回転は、中はクォータニオンだが、ファイルには Inspector と同じオイラー角（度。Z → X → Y の順）で書く（人が読める・手で直せる）
	// Inspector の角度（eulerHint）は度のまま持っているので、保存と読み込みを何回くり返しても数字はずれない
	Vector3 euler = GetEulerAngles(value); // 書くときは、この値がファイルへ。読むときは、ファイルの値で上書きされる
	const Vector3 before = euler;
	ui.Field("rotation", euler);
	if (euler != before) {
		SetEulerAngles(value, euler); // 読んだときだけ変わる（書くときは Component を何も変えない）
	}
	ui.Field("scale", value.scale);
	// worldMatrix は毎フレーム計算する結果なので書かない
}

//=============================================================================
// フォルダ
//=============================================================================
// 項目は無い（"Folder": {} と書かれているだけで、フォルダだと分かる）
void DescribeFolder(ComponentUI& /*ui*/, FolderComponent& /*value*/) {}

//=============================================================================
// Prefab から置いた物の印
//=============================================================================
void DescribePrefabInstance(ComponentUI& ui, PrefabInstanceComponent& value) {
	const FixedString<256> before = value.path;
	ui.Field("path", value.path);
	// 読んだときだけ（path が変わったとき）番号を引く。書くときは何も変えない（Transform の回転と同じやり方）
	// Play の退避から戻すときもここを通るので、番号はいつも path に合う
	if (value.path != before) {
		value.prefab = value.path.Empty() ? 0 : PrefabManager::Load(ToPath(value.path.ToString()));
	}
}

//=============================================================================
// ModelRenderer
//=============================================================================
void DescribeModelRenderer(ComponentUI& ui, ModelRendererComponent& value) {
	ui.Field("enabled", value.enabled);
	// --- 使うファイル（パスで書く。番号は起動するたびに変わる）---
	ui.AssetField("model", value.modelHandle, AssetType::Model);
	ui.AssetField("texture", value.textureHandle, AssetType::Texture); // 未選択＝モデルのマテリアルのテクスチャ
	// --- 描画の設定（enumは名前で書く）---
	ui.Field("shadingType", value.shadingType);
	ui.Field("blendMode", value.blendMode);
	ui.Field("rasterizerType", value.rasterizerType);
	ui.Field("depthMode", value.depthMode);
	ui.Field("billboard", value.billboard);
	// --- 色（sRGB。保存の形は前と同じ [r, g, b, a]）---
	ui.ColorField("color", value.color);
	ui.Field("intensity", value.intensity); // 明るさの倍率（前のファイルには無い＝1 のまま）
	// --- マテリアル ---
	ui.Field("overrideMaterial", value.overrideMaterial);
	ui.ColorField("ambient", value.material.ambient);
	ui.ColorField("diffuse", value.material.diffuse);
	ui.ColorField("specular", value.material.specular);
	ui.ColorField("emissive", value.material.emissive);
	ui.Field("shininess", value.material.shininess);
	ui.Field("metallic", value.material.metallic);
	ui.Field("roughness", value.material.roughness);
	ui.Field("alphaCutoff", value.material.alphaCutoff);
	// --- UV ---
	ui.Field("uvTranslation", value.uvTransform.translation);
	ui.Field("uvRotation", value.uvTransform.rotation);
	ui.Field("uvScale", value.uvTransform.scale);
}

//=============================================================================
// ライト
//=============================================================================
// ギズモの表示（3種類で共通）
void DescribeGizmo(ComponentUI& ui, LightGizmoFlags& gizmo) {
	ui.Field("showIcon", gizmo.showIcon);
	ui.Field("showRange", gizmo.showRange);
}

void DescribeDirectionalLight(ComponentUI& ui, DirectionalLightComponent& value) {
	ui.ColorField("color", value.color); // 見た目の色（sRGB）。リニアへの変換は LightSystem が集めるときに行う
	ui.Field("intensity", value.intensity);
	DescribeGizmo(ui, value.gizmo);
}

void DescribePointLight(ComponentUI& ui, PointLightComponent& value) {
	ui.ColorField("color", value.color);
	ui.Field("intensity", value.intensity);
	ui.Field("radius", value.radius);
	ui.Field("decay", value.decay);
	DescribeGizmo(ui, value.gizmo);
}

void DescribeSpotLight(ComponentUI& ui, SpotLightComponent& value) {
	ui.ColorField("color", value.color);
	ui.Field("intensity", value.intensity);
	ui.Field("range", value.range);
	ui.Field("decay", value.decay);
	ui.Field("outerAngle", value.outerAngle); // 度
	ui.Field("innerAngle", value.innerAngle); // 度
	DescribeGizmo(ui, value.gizmo);
}
} // namespace

//=============================================================================
// エンジンのComponentを登録する（Engine::Initialize から1回。Releaseでも呼ぶ）
// 名前はInspectorの見出しと同じにしておく（ファイルを開いたときに、どのComponentか分かるように）
//=============================================================================
void ComponentSerializer::RegisterEngineComponents() {
	Register<TransformComponent>("Transform", &DescribeTransform);
	Register<FolderComponent>("Folder", &DescribeFolder);
	Register<PrefabInstanceComponent>("Prefab Instance", &DescribePrefabInstance);
	Register<ModelRendererComponent>("Model Renderer", &DescribeModelRenderer);
	Register<DirectionalLightComponent>("Directional Light", &DescribeDirectionalLight);
	Register<PointLightComponent>("Point Light", &DescribePointLight);
	Register<SpotLightComponent>("Spot Light", &DescribeSpotLight);
}