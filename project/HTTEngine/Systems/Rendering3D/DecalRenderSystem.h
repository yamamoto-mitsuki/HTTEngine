#pragma once
#include <cstdint>

// 前方宣言
class Camera;

/// <summary>
/// DecalRendererComponent を持つ Entity の箱に、マテリアルで塗る（Renderer::DrawDecal）
/// <para>マテリアルを選んでいなければ、エンジンの既定のマテリアル（白い画像を貼る）で描く</para>
/// </summary>
class DecalRenderSystem {
public:
	// DecalRendererComponent を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// 描画リクエストを積む（EngineSystems::Draw から。停止中も描く）
	/// </summary>
	static void Draw();

	/// <summary>
	/// 箱の形を線で描く（エディタの Scene ビューだけ）
	/// </summary>
	static void DrawGizmos(Camera* camera);

	// エンジンの既定のデカールのマテリアル（初めて呼んだときに読む。シェーダーを読んだ後に呼ぶ）
	static uint32_t GetDefaultMaterial();
};
