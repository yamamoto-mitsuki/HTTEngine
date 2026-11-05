#pragma once
#include <cstdint>

#include "MyEngine/Math/Vector3.h"

/// <summary>
/// 床の予告の描き手が、描く物ごとに Shader へ送る値（gDrawData。Shader の Decal/HitboxTelegraph.hlsli と同じ並び。16 バイトずつ）
/// <para>形の値は WorldHitShape（Entity の大きさを掛けた後）。予告の Shader はこれで、当たりと同じ符号付き距離を計算する</para>
/// </summary>
struct HitboxTelegraphData {
	Vector3 origin = {0.0f, 0.0f, 0.0f};  // 形の中心（ワールド。底の真ん中）
	float radius = 0.0f;                  // 円・扇の外側の半径
	Vector3 right = {1.0f, 0.0f, 0.0f};   // 形のローカル +X（長さ 1）
	float innerRadius = 0.0f;             // 円・扇の内側の半径
	Vector3 up = {0.0f, 1.0f, 0.0f};      // 形のローカル +Y（長さ 1）
	float halfAngle = 0.0f;               // 扇の開きの半分（ラジアン。円は π）
	Vector3 forward = {0.0f, 0.0f, 1.0f}; // 形のローカル +Z（長さ 1）
	float length = 0.0f;                  // 線の長さ
	float width = 0.0f;                   // 線の幅
	float height = 0.0f;                  // 形の高さ
	uint32_t shapeType = 0;               // 形の種類（HitShapeType の番号）
	uint32_t phase = 0;                   // 段階（HitboxPhase の番号）
	float progress = 0.0f;                // Warning の進み具合（0→1。Active の間は 1、Warning の長さが 0 なら 0 のまま）
	float remaining = 0.0f;               // Warning の残りの秒
	float phaseTime = 0.0f;               // 今の段階に入ってからの秒
	float padA = 0.0f;
};
static_assert(sizeof(HitboxTelegraphData) == 96, "HLSLのHitboxTelegraph（Decal/HitboxTelegraph.hlsli）と大きさが違います");


/// <summary>
/// HitboxTelegraphComponent を持つ Entity の Hitbox の形を、床にデカールとして描く（Renderer::DrawDecal）
/// <para>マテリアルを選んでいなければ、エンジンの既定のマテリアル（赤い塗り・縁・進み具合）で描く</para>
/// </summary>
class HitboxTelegraphSystem {
public:
	// HitboxTelegraphComponent を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// 描画リクエストを積む（EngineSystems::Draw から。停止中も描く＝Inspector で Phase を Warning にすると、置きながら見られる）
	/// </summary>
	static void Draw();

	// エンジンの既定の予告のマテリアル（初めて呼んだときに読む。シェーダーを読んだ後に呼ぶ）
	static uint32_t GetDefaultMaterial();
};
