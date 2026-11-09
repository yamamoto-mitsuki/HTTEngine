#pragma once
#include <string_view>

#include "HTTEngine/Components/Rendering3D/PrimitiveRendererComponent.h"
#include "HTTEngine/Math/Color.h"

// 前方宣言
class Camera;
class ComponentUI;
class IBLEnvironment;

/// <summary>
/// PrimitiveRendererComponent を持つ Entity を、その形（球・箱）で描く
/// <para>形は Renderer の DrawSphere・DrawOBB を使う（頂点はエンジンがコードで作る。モデルのファイルは要らない）</para>
/// <para>見た目（PrimitiveMaterial）の見せ方と、描く Config への入れ方も持つ（Elastic Surface も同じ物を使う）</para>
/// </summary>
class PrimitiveRenderSystem {
public:
	// PrimitiveRendererComponent を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release() {}

	/// <summary>
	/// 描画リクエストを積む（WindowManager::DrawAll から。停止中も描く）
	/// </summary>
	/// <param name="environment">環境光（PBR に要る。nullptr なら PBR の形は Half Lambert で描かれる）</param>
	static void Draw(Camera* camera, IBLEnvironment* environment);

	// ===== 見た目（PrimitiveMaterial）。Primitive Renderer・Elastic Surface で共通 =====

	/// <summary>
	/// PrimitiveMaterial の見せ方（Inspector と保存）。持っている Component の見せ方（Describe〇〇）の中から呼ぶ
	/// </summary>
	/// <param name="keyPrefix">保存の名前の頭に付ける言葉。1つの Component が2つ持つとき、名前が重ならないように（"post" なら "postColor"。"" なら "color"）</param>
	static void DescribeMaterial(ComponentUI& ui, PrimitiveMaterial& material, std::string_view keyPrefix);

	/// <summary>
	/// 描く Config（MeshConfig・SphereConfig・OBBConfig）に、見た目の値を入れる
	/// <para>光沢などは material の中を指す（写さない）ので、material は Renderer の Draw〇〇 を呼び終わるまで消さない</para>
	/// </summary>
	template<class TConfig> static void ApplyMaterial(TConfig& config, const PrimitiveMaterial& material, IBLEnvironment* environment) {
		config.color = ApplyIntensity(material.color, material.intensity); // 明るさの倍率はリニアで掛ける（Color.h）
		config.textureHandle = material.textureHandle;
		config.uvTransform.scale = {material.uvScale.x, material.uvScale.y, 1.0f}; // UV の Scale は「テクスチャ1枚の大きさ」（大きいほど絵が大きく、くり返しが減る）
		config.shadingType = material.shadingType;
		config.material = &material.params;
		config.env = environment;
		config.receivesDecals = material.receivesDecals;
		// --- 透け方：奥行きを書かない（TestNoWrite）物は、Renderer が半透明として、不透明な物の後にカメラから遠い順で描く ---
		switch (material.surface) {
		case SurfaceType::Opaque:
			config.blendMode = BlendMode::Normal;
			config.depthMode = DepthMode::TestWrite;
			break;
		case SurfaceType::Transparent:
			config.blendMode = BlendMode::Normal;
			config.depthMode = DepthMode::TestNoWrite;
			break;
		case SurfaceType::Additive:
			config.blendMode = BlendMode::Add;
			config.depthMode = DepthMode::TestNoWrite;
			break;
		}
	}
};