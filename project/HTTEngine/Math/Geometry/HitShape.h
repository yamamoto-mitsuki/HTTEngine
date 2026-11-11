#pragma once
#include <type_traits>

#include "HTTEngine/Core/EnumTips.h"
#include "HTTEngine/Math/Geometry/Capsule.h"
#include "HTTEngine/Math/Geometry/Sphere.h"
#include "HTTEngine/Math/Matrix4x4.h"
#include "HTTEngine/Math/Vector2.h"
#include "HTTEngine/Math/Vector3.h"

// 攻撃の形（形の層）。当たり判定・ジャスト判定の範囲・床の予告の図形が、この1つの値と1つの式（符号付き距離）を使う
// 押し合う形（Collider）とは別。押し戻さないので、扇のような「物理では意味の無い形」も持てる


/// <summary>
/// 攻撃の形の種類
/// <para>円・扇・線は「床に置く形」：ローカルの XZ の面に描いた形を、底から上（+Y）へ Height だけ伸ばした柱。Entity を回すと一緒に回る</para>
/// </summary>
enum class HitShapeType {
	Circle,  // 円（Inner Radius を入れるとドーナツ）
	Sector,  // 扇（前 +Z を真ん中に Angle 度開く。Inner Radius を入れると扇のドーナツ）
	Line,    // 線（幅 Width の長方形。中心から前 +Z へ Length だけ伸びる）
	Sphere,  // 球（空中の攻撃・弾）
	Capsule, // カプセル（中心から前 +Z へ Length の線分を、Radius で太らせた形。剣を振る・太いレーザー）
};

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(HitShapeType) {
	using enum HitShapeType;
	return MakeEnumTips<HitShapeType>({
	    {Circle, "床に置く円（Inner Radius を入れるとドーナツ）"},
	    {Sector, "床に置く扇（前 +Z を真ん中に Angle 度開く。Inner Radius を入れると扇のドーナツ）"},
	    {Line, "床に置く線（幅 Width の長方形。中心から前 +Z へ Length だけ伸びる）"},
	    {Sphere, "空中の球（空中の攻撃・弾）"},
	    {Capsule, "空中のカプセル（中心から前 +Z へ Length の線分を、Radius で太らせた形。剣を振る・太いレーザー）"},
	});
}


/// <summary>
/// 攻撃の形（Component に持たせる値。位置は持たない。付けた Entity の Transform から取る）
/// </summary>
struct HitShape {
	HitShapeType type = HitShapeType::Circle; // 種類
	Vector3 center = {0.0f, 0.0f, 0.0f};      // Entity の位置からのずれ（ローカル。床に置く形は底の真ん中）
	float radius = 2.0f;                      // 円・扇の外側の半径／球・カプセルの半径
	float innerRadius = 0.0f;                 // 円・扇の内側の半径（0 なら穴なし）
	float angle = 90.0f;                      // 扇の開き（度。0〜360。360 なら円と同じ）
	float length = 4.0f;                      // 線・カプセルの長さ（中心から前 +Z へ）
	float width = 1.0f;                       // 線の幅
	float height = 2.0f;                      // 床に置く形（円・扇・線）の高さ（底から上 +Y へ）

	bool operator==(const HitShape&) const = default;
};
static_assert(std::is_trivially_copyable_v<HitShape>);

// 床に置く形（円・扇・線）か。床の予告の図形を描けるのはこれだけ
constexpr bool IsFloorShape(HitShapeType type) { return type == HitShapeType::Circle || type == HitShapeType::Sector || type == HitShapeType::Line; }


/// <summary>
/// ワールドに置いた攻撃の形（判定・ギズモ・床の予告はこれを使う）
/// <para>大きさは Entity の大きさを掛けた後（円・扇の半径は X と Z の大きい方、線の幅は X、長さは Z、高さは Y、球は一番大きい軸）</para>
/// </summary>
struct WorldHitShape {
	HitShapeType type = HitShapeType::Circle;
	Vector3 origin = {0.0f, 0.0f, 0.0f};  // 形の中心（ワールド。床に置く形は底の真ん中）
	Vector3 right = {1.0f, 0.0f, 0.0f};   // 形のローカル +X（長さ1）
	Vector3 up = {0.0f, 1.0f, 0.0f};      // 形のローカル +Y（長さ1）
	Vector3 forward = {0.0f, 0.0f, 1.0f}; // 形のローカル +Z（長さ1）
	float radius = 0.0f;                  // 外側の半径（球・カプセルは半径）
	float innerRadius = 0.0f;             // 内側の半径
	float halfAngle = 0.0f;               // 扇の開きの半分（ラジアン。0〜π）
	float length = 0.0f;                  // 線・カプセルの長さ
	float width = 0.0f;                   // 線の幅
	float height = 0.0f;                  // 床に置く形の高さ

	/// <summary>
	/// HitShape を Entity のワールド行列で置く
	/// </summary>
	static WorldHitShape Make(const HitShape& shape, const Matrix4x4& worldMatrix);

	// ===== 座標 =====
	Vector3 ToLocal(const Vector3& world) const; // ワールドの点 → 形のローカル（origin が原点、向きは right・up・forward）
	Vector3 ToWorld(const Vector3& local) const; // 形のローカルの点 → ワールド

	// ===== 符号付き距離（形の外なら表面までの距離、中ならマイナス。全部の判定・描画の元）=====
	/// <summary>
	/// 床の面（ローカルの XZ）の中での符号付き距離。床に置く形だけ（球・カプセルは、その面で切った断面）
	/// <para>床の予告の図形はこれで塗る（シェーダーに同じ式を書く）</para>
	/// </summary>
	float CalcSignedDistance2D(const Vector2& localXZ) const;
	// 3D の符号付き距離（ワールドの点）
	float CalcSignedDistance(const Vector3& world) const;
	// 表面から外へ向かう向き（符号付き距離の傾き。長さ1）
	Vector3 CalcNormal(const Vector3& world) const;

	// ===== 重なり（margin だけ形を太らせて調べる。ジャスト範囲は margin ＞ 0）=====
	// 球と重なるか
	bool Overlaps(const Sphere& sphere, float margin = 0.0f) const;
	/// <summary>
	/// カプセル（線分を半径で太らせた形）と重なるか。動いた球の跡（すり抜けない判定）にも使う
	/// </summary>
	/// <param name="nearest">線分の上で、形に一番近い点が入る（要らなければ nullptr）</param>
	bool Overlaps(const Capsule& capsule, float margin = 0.0f, Vector3* nearest = nullptr) const;

	// 線分の上で、形への符号付き距離が一番小さい点（Overlaps の中身。並べて調べてから、谷を細かく探す）
	Vector3 FindNearestOnSegment(const Vector3& start, const Vector3& end, float sampleStep) const;
};
