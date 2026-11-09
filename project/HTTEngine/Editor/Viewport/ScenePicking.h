#pragma once
#include <array>
#include <vector>

#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Geometry/AABB.h"
#include "HTTEngine/Math/Geometry/Ray.h"
#include "HTTEngine/Math/Matrix4x4.h"

// 前方宣言
class Camera;

/// <summary>
/// 見た目の箱1つ（ローカル座標の箱と、それをワールドへ置く行列）
/// </summary>
struct PickBounds {
	AABB local;       // ローカル座標の箱（モデルならメッシュの頂点が全部入る箱、Spriteなら仮）
	Matrix4x4 matrix; // ローカル → ワールド

	/// <summary>
	/// 箱の8つの角をワールド座標で返す（番号は AABB::GetCorners と同じ）
	/// </summary>
	std::array<Vector3, 8> GetWorldCorners() const;
};

/// <summary>
/// Sceneビューのクリックで、どのEntityを指したか調べる
/// <para>見る物: Model Renderer（三角形1枚づつ）・Sprite Renderer（板）・Polygon Renderer（多角形を押し出した形）・ライトとカメラ（置いてある所の小さい球）・Collider（線を出している時だけ）</para>
/// </summary>
namespace ScenePicking {

/// <summary>
/// レイに当たった物のうち、一番手前のEntity（何にも当たらなければ無効な Handle）
/// </summary>
/// <param name="ray">ワールド座標のレイ（Camera::ScreenPointToRay で作った物）</param>
/// <param name="camera">見ているカメラ（ビルボードの板の向きに使う）</param>
/// <returns></returns>
Handle<Entity> Pick(const Ray& ray, const Camera& camera);

/// <summary>
/// Entityの見た目の箱をoutに足す（モデルはメッシュごと。Spriteは板。どちらも無ければ足さない）
/// <para>選んだ物の枠を描く・Fキーで取るのに使う<para>
/// </summary>
void CollectBounds(Handle<Entity> entity, const Camera& camera, std::vector<PickBounds>& out);
}