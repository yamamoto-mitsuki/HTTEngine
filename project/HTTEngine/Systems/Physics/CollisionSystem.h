#pragma once
#include <functional>
#include <span>
#include <vector>

#include "HTTEngine/Collision/Collision.h"
#include "HTTEngine/Components/Physics/ColliderComponent.h"
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Geometry/AABB.h"
#include "HTTEngine/Math/Geometry/Capsule.h"
#include "HTTEngine/Math/Geometry/Ray.h"
#include "HTTEngine/Math/Geometry/Sphere.h"

// 前方宣言
class Camera;
struct TransformComponent;


/// <summary>
/// 当たり方の段階
/// </summary>
enum class HitPhase {
	Enter, // 当たり始めた（このフレームから重なった）
	Stay,  // 当たっている（前のフレームから重なったまま）
	Exit,  // 離れた（前のフレームまで重なっていた）
};

/// <summary>
/// レイキャストで当たった物
/// </summary>
struct RaycastHit {
	Handle<Entity> entity; // 当たったEntity
	Vector3 point;         // 当たった位置（ワールド座標）
	float distance = 0.0f; // レイの始点から当たった位置までの距離
};

/// <summary>
/// 当たり1件。self から見て、other と当たった
/// </summary>
struct CollisionHit {
	Handle<Entity> self;    // 自分
	Handle<Entity> other;   // 相手（Exit のときは、相手がもう消えていることがある。そのときは EntityManager::Get が nullptr を返す）
	HitPhase phase;         // 当たり始め・当たっている・離れた
	bool isTrigger = false; // どちらかが Is Trigger（すり抜ける当たり。Script には OnTrigger〜、そうでなければ OnCollision〜）
	Vector3 normal = {0.0f, 0.0f, 0.0f}; // 相手から自分へ押し出す向き（長さ1。ぶつかった面の向き）。Exit は離れる前のフレームの向き
	Vector3 selfVelocity = {0.0f, 0.0f, 0.0f};  // 自分の速さ。物理でぶつかったときは、ぶつかる直前の速さ（Rigidbody が無ければ0）
	Vector3 otherVelocity = {0.0f, 0.0f, 0.0f}; // 相手の速さ（同じ）
};

/// <summary>
/// ワールド座標に直した Collider の形（CollisionSystem が判定に使い、PhysicsSystem が押し戻しに使う）
/// </summary>
struct WorldCollider {
	Handle<Entity> entity;                       // 持ち主
	ColliderShape shape = ColliderShape::Box;    // 形
	Sphere sphere{};                             // shape が Sphere のとき使う
	AABB box{};                                  // shape が Box のとき使う
	Capsule capsule{};                           // shape が Capsule のとき使う（いつも立っている。start が下の端の球の中心、end が上の端の球の中心）
	ExtrudedPolygon polygon{};                   // shape が Polygon のとき使う（box には、それを囲む箱が入る）
	bool isTrigger = false;                      // すり抜ける（押し戻さない）

	// 形を move だけずらす（向き・大きさは変えない。動かした物の形を、Transform から作り直さずに合わせる）
	void Translate(const Vector3& move) {
		sphere.center += move;
		box.min += move;
		box.max += move;
		capsule.start += move;
		capsule.end += move;
		polygon.origin += move;
	}
};

// どのEntityを調べるか（true を返したEntityだけ調べる）。空なら全部
// レイヤーの代わりに「相手が持っている Component」で絞る：[](Handle<Entity> e) { return EntityManager::Get<Wall>(e) != nullptr; }
using CollisionFilter = std::function<bool(Handle<Entity>)>;

/// <summary>
/// 形を動かして調べる（Cast・Overlap）ときの決まり
/// <para>調べる形の持ち主・その親子とは、いつも当たらない（自分の Collider・持っている剣の Collider に止められないように）</para>
/// </summary>
struct ShapeQuery {
	CollisionFilter filter;       // 調べる相手を絞る（空なら全部）
	Collision::AxisLock lock;     // その軸へは押し出さない（当たった面の向き・押し出すずれから、その軸の分を抜く）
	bool includeTriggers = false; // Is Trigger の Collider も相手にするか（物を動かすときは、すり抜ける物を相手にしない）
};

/// <summary>
/// Cast で最初に当たった物
/// </summary>
struct ShapeCastHit {
	Handle<Entity> entity;               // 当たった Entity
	Vector3 normal = {0.0f, 0.0f, 0.0f}; // 相手から、動かした形へ押し出す向き（長さ1。当たった面の向き。床なら、だいたい (0, 1, 0)）
	Vector3 point = {0.0f, 0.0f, 0.0f};  // 当たった所（止まった所での、動かした形の表面の、相手に一番近い所。だいたいの位置）
	float fraction = 0.0f;               // 動きの何割まで進めるか（0〜1。ここまでは何にも重ならない）
	float distance = 0.0f;               // 進める距離（fraction × 動きの長さ）
};

/// <summary>
/// Overlap で重なっていた物1つ
/// </summary>
struct ShapeOverlap {
	Handle<Entity> entity;             // 重なっている Entity
	Vector3 push = {0.0f, 0.0f, 0.0f}; // 調べた形を、この相手から外へ出すずれ（CalcPushOut と同じ。足すと、ちょうど触れた所になる）
};


/// <summary>
/// ColliderComponent を持つEntity同士の重なりを調べて、「当たりの一覧」を作る
/// <para>押し戻しはここではしない（Rigidbody を PhysicsSystem が、Character Controller を CharacterControllerSystem が動かす。どちらも Cast・CalcPushOut でここに聞く）。
/// ゲームは Script の OnTrigger〜 / OnCollision〜、または SYSTEM の中で GetHits を読んで、消す・音を鳴らすなどを決める</para>
/// <para>1フレームの順番：SYSTEM（前のフレームの当たりを読む）→ ワールド行列 → 形を集め直す（SyncWorld）→ キャラを動かす → Rigidbody の押し戻し
/// （どちらもぶつかった組を ReportImpact で知らせる）→ 破棄の反映 → ここで判定 → 描画</para>
/// <para>一覧などの中身は CollisionSystem.cpp の中だけに置く（このヘッダはゲームのComponentのファイルがみんな読むので、軽くしておく）</para>
/// </summary>
class CollisionSystem {
public:
	// ColliderComponent を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// 全部の Collider の重なりを調べて、当たりの一覧を作り直す
	/// <para>EntityManager::FlushDestroy の後に呼ぶ（消すと決めたEntityは、もう当たらない）</para>
	/// <para>触れているだけ（kContactOffset より近い）の物も当たっている扱い（Rigidbody は床にちょうど触れた所で止まるので）</para>
	/// </summary>
	static void Update();

	/// <summary>
	/// このEntityの当たり（self がこのEntityの物だけ）。無ければ空
	/// <para>中身は次の Update まで変わらない（SYSTEM の中で読む分には気にしなくてよい）</para>
	/// </summary>
	static std::span<const CollisionHit> GetHits(Handle<Entity> entity);

	// ===== その場で調べる（SYSTEMの中で、何回呼んでもよい）=====
	// 調べる相手の形は、最後に集めた物。再生中は、キャラを動かす前の SyncWorld（その後に動いた Rigidbody は動く前の位置）、Script の Update の中は前のフレームの Update
	// Play・シーン切り替えの直後の Script の Update の中は、作り直す前の Entity の形しか無いので、何にも当たらない

	/// <summary>
	/// レイを飛ばして、一番近くで当たった Collider を探す（壁で線を止める・地面を指す）
	/// </summary>
	/// <param name="ray">始点と向き（向きの長さは1でなくてよい。中で長さ1にする）</param>
	/// <param name="maxDistance">どこまで調べるか（始点からの距離）</param>
	/// <param name="hit">当たった物が入る（当たらなければ変えない）</param>
	/// <param name="filter">調べる相手を絞る（空なら全部）</param>
	/// <returns>当たったか。始点が Collider の中なら、距離0で当たる</returns>
	static bool Raycast(const Ray& ray, float maxDistance, RaycastHit& hit, const CollisionFilter& filter = {});

	/// <summary>
	/// 球を start から end まで動かしたときに触れた Collider を全部集める（速く動く物の攻撃。1フレームで飛び越えてもすり抜けない）
	/// </summary>
	/// <param name="capsule">動いた跡（start → end を半径radius で太らせた形）</param>
	/// <param name="out">触れたEntityが入る（最初に空にする。並びは決まっていない）</param>
	/// <param name="filter">調べる相手を絞る（空なら全部）</param>
	static void SweepSphere(const Capsule& capsule, std::vector<Handle<Entity>>& out, const CollisionFilter& filter = {});

	/// <summary>
	/// 形を motion だけ動かしたときに、最初に当たる Collider を探す（物を動かす係が、壁で止める・面に沿わせるのに使う）
	/// <para>どの形でも同じ書き方：少しずつ進めて CalcPushOut で重なりを見て、重なった区間を半分ずつ狭める。新しい形は CalcPushOut を書けば、ここでも使える</para>
	/// <para>動かす前から重なっている相手は、近づく向きなら 0 で当たる（それ以上入らない）。離れる向きなら当たらない（中から出られる）</para>
	/// </summary>
	/// <param name="shape">動かす形（ワールド座標。CalcWorldCollider で作る）</param>
	/// <param name="motion">動き（ワールド座標）</param>
	/// <param name="hit">最初に当たった物が入る（当たらなければ変えない）</param>
	/// <param name="query">調べる相手の決まり</param>
	/// <returns>当たったか</returns>
	static bool Cast(const WorldCollider& shape, const Vector3& motion, ShapeCastHit& hit, const ShapeQuery& query = {});

	/// <summary>
	/// 形に今重なっている Collider を全部集める（入り込まれたときに外へ出す量を知る）
	/// </summary>
	/// <param name="out">重なっている物と、そこから出すずれが入る（最初に空にする。並びは決まっていない）</param>
	static void Overlap(const WorldCollider& shape, std::vector<ShapeOverlap>& out, const ShapeQuery& query = {});

	/// <summary>
	/// 調べる相手の形を、今の Transform で集め直す（Update の 1 と同じ。当たりの一覧は作らない）
	/// <para>物を動かす係の前に呼ぶ（Script・アニメ・Timeline がこのフレームに動かした物を、動かした後の位置で調べる）</para>
	/// </summary>
	static void SyncWorld();

	/// <summary>
	/// 調べる相手の形のうち、entity とその子の物を move だけずらす（動かした係が呼ぶ。次に動かす物が、動いた後の位置で調べる）
	/// </summary>
	static void MoveCollider(Handle<Entity> entity, const Vector3& move);

	// 全部の当たり（1組につき、両方から見た2件が入る。self の順に並んでいる）
	static std::span<const CollisionHit> GetAllHits();

	/// <summary>
	/// 物理でぶつかった1組を知らせる（PhysicsSystem が押し戻すときに呼ぶ。ゲームからは呼ばない）
	/// <para>次の Update で当たりの一覧に入る。跳ね返って同じフレームのうちに離れた組も、当たり始め（Enter）になる</para>
	/// <para>同じ組がそのフレームに何回ぶつかっても、最初の1回だけ覚える（ぶつかる直前の速さを残すため）</para>
	/// </summary>
	/// <param name="a">押し戻された方</param>
	/// <param name="b">押し戻した相手</param>
	/// <param name="normal">b から a へ押し出す向き（長さ1）</param>
	/// <param name="velocityA">a のぶつかる直前の速さ</param>
	/// <param name="velocityB">b のぶつかる直前の速さ（動かない壁なら0）</param>
	static void ReportImpact(Handle<Entity> a, Handle<Entity> b, const Vector3& normal, const Vector3& velocityA, const Vector3& velocityB);

	/// <summary>
	/// Collider を、ワールド座標の球・箱・カプセル・多角形にする（Transform のワールド行列を使うので、UpdateTransforms の後に呼ぶ）
	/// <para>Entityの回転と大きさも掛かる（箱・カプセルは回らない。大きさだけ軸ごとに掛かる。多角形は Z の回転だけ効く）</para>
	/// <para>多角形は同じ Entity の Polygon Shape の点を使う（無ければ点 0 個＝何にも当たらない）</para>
	/// </summary>
	static WorldCollider CalcWorldCollider(Handle<Entity> entity, const ColliderComponent& collider, const TransformComponent& transform);

	/// <summary>
	/// カプセルを「線分の上で相手に一番近い点に置いた、同じ半径の球」に置き換える（両方カプセルなら両方）。球・箱はそのまま
	/// <para>カプセルは線分を半径で太らせた形なので、重なっているかは、この球と同じになる。球・箱の判定と押し出しをそのまま使える（PhysicsSystem も使う）</para>
	/// </summary>
	static void ReplaceCapsulesWithSpheres(WorldCollider& a, WorldCollider& b);

	/// <summary>
	/// a を b から外へ出すずれ（a に push を足すと、ちょうど触れた所になる）。重なっていなければ false
	/// <para>形の組み合わせ（球・箱・カプセル・多角形）で、使う関数を選ぶ。カプセルは ReplaceCapsulesWithSpheres で球にしてから出す（多角形が相手のときだけ、線分の上で一番深い所を探す）</para>
	/// <para>多角形は球・カプセルとだけ当たる。箱 × 多角形・多角形 × 多角形は、いつも false（当たらない）</para>
	/// </summary>
	/// <param name="a">出す方（コピー。カプセルを球に置き換えるので）</param>
	/// <param name="b">出される相手（コピー）</param>
	/// <param name="lock">その軸へは押さない（Freeze Position）。その分、ほかの軸で押す</param>
	/// <param name="push">ずれが入る（重なっていなければ変えない）</param>
	static bool CalcPushOut(WorldCollider a, WorldCollider b, Collision::AxisLock lock, Vector3* push);

	// Collider の線を描く（エディタ用。当たっている物は色を変える）
	static void DrawGizmos(Camera* camera);

	// 線を出すか（メニューバーの View から切り替える）
	static bool& GetShowGizmos();

	// この距離まで近づいたら「当たっている」にする（Unity の Contact Offset と同じ考え）
	static constexpr float kContactOffset = 0.01f;
};