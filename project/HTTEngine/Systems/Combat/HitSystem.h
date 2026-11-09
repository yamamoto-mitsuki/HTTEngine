#pragma once
#include <cstdint>
#include <span>
#include <vector>

#include "HTTEngine/Combat/HitInfo.h"
#include "HTTEngine/Components/Combat/HitboxComponent.h"
#include "HTTEngine/Components/Combat/HurtboxComponent.h"
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Geometry/Capsule.h"
#include "HTTEngine/Math/Geometry/HitShape.h"

// 前方宣言
class Camera;
struct TransformComponent;


/// <summary>
/// ジャスト判定で見つかった攻撃（HitSystem::FindJust・TryConsumeJust）
/// </summary>
struct JustCandidate {
	Handle<Entity> hitbox;     // その攻撃の Hitbox の Entity
	Handle<Entity> instigator; // 撃った本人
	uint32_t attackSerial = 0; // その攻撃の回の番号（Warning の間なら、これから始まる回の番号）
	float timeToActive = 0.0f; // Active になるまでの秒（Active なら 0）
};


/// <summary>
/// 攻撃の当たり（当たりの層）。Hitbox の窓を時間で進め、Active の Hitbox と Hurtbox の重なりを調べて「当たりの知らせ」を作る
/// <para>調べる順：段階を進める → 攻撃の回が変わったら記録を空にする → 当たり（動いた跡も調べる＝すり抜けない）→ ダメージの層に聞く → 記録して知らせる</para>
/// <para>受ける本体を決める・受けるかを決めるのはダメージの層（DamageSystem が Initialize で答え方を登録する）。登録が無ければ、Hurtbox の Entity が受けて、いつも受ける</para>
/// <para>1フレームの順番：…→ 破棄の反映 → CollisionSystem::Update → DamageSystem::Update → ここ。知らせは次のフレームの Script（OnHitDealt・OnHitTaken）・SYSTEM が読む</para>
/// </summary>
class HitSystem {
public:
	// Hitbox・Hurtbox を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// 窓を進めて、当たりを調べる（再生中だけ。時間が止まっている間は何もしない）
	/// </summary>
	/// <param name="isGameRunning">再生中か（停止中は実行中の値を全部消す）</param>
	/// <param name="deltaTime">経過秒（Time::GetDeltaTime。ヒットストップ・一時停止の間は 0）</param>
	static void Update(bool isGameRunning, float deltaTime);

	// ===== 窓を開け閉めする（攻撃部品・Timeline・Script・SYSTEM から。今のフレームの Update から効く）=====
	// 予告を始める（warningSeconds 秒たっても Warning のまま。Active へは呼ぶ側が進める。0 ならずっと）
	static void Warn(Handle<Entity> hitbox, float warningSeconds);
	// 予告を始めて、warningSeconds 秒後に Active にし、さらに activeSeconds 秒後に Off にする（0 ならずっと Active）
	static void Warn(Handle<Entity> hitbox, float warningSeconds, float activeSeconds);
	// すぐ当たる窓を開ける（いつも新しい攻撃の回＝当てた相手の記録が空になる）。activeSeconds 秒後に Off（0 ならずっと）
	static void Activate(Handle<Entity> hitbox, float activeSeconds = 0.0f);
	// 窓を閉じる
	static void Deactivate(Handle<Entity> hitbox);

	// ===== 当たりの知らせ（最後の Update で当たった物。次の Update まで変わらない）=====
	static std::span<const HitInfo> GetAllHits();
	// entity が当てた物（Hitbox の Entity か撃った本人が entity）を out に入れる（最初に空にする）
	static void GetHitsDealt(Handle<Entity> entity, std::vector<HitInfo>& out);
	// receiver が受けた物を out に入れる（最初に空にする）
	static void GetHitsTaken(Handle<Entity> receiver, std::vector<HitInfo>& out);

	// ===== ジャスト判定（Script の Update から。位置は前のフレームの終わりの物）=====
	/// <summary>
	/// 受ける本体の Hurtbox が、有効なジャスト判定の範囲に入っている攻撃を探す（記録はしない）
	/// <para>有効なのは「Warning で Active まで Just Lead Time 秒以内（Warning の長さが 0 ならずっと）」か「Active」の Hitbox。
	/// 当てる相手のレイヤーに入っていて、その攻撃の回でまだ当たっていない・よけていない物だけ。いくつもあれば Active に一番近い物</para>
	/// </summary>
	static bool FindJust(Handle<Entity> receiver, JustCandidate& out);
	/// <summary>
	/// FindJust で見つかった攻撃を「よけた」と記録する（同じ攻撃の回では、もうジャストにならない・もう当たらない）
	/// </summary>
	/// <returns>見つかったか（見つからなければ何も記録しない）</returns>
	static bool TryConsumeJust(Handle<Entity> receiver, JustCandidate& out);

	// ===== 形（ワールド座標）=====
	// Hitbox の形をワールドに置く（Transform のワールド行列を使う）
	static WorldHitShape CalcWorldShape(const HitboxComponent& hitbox, const TransformComponent& transform);
	// Hurtbox をワールドのカプセルにする（球は start と end が同じカプセル）
	static Capsule CalcWorldHurtbox(const HurtboxComponent& hurtbox, const TransformComponent& transform);
	// Hurtbox の受ける本体（ダメージの層に聞く。答える係が無ければ hurtbox 自身）
	static Handle<Entity> FindReceiver(Handle<Entity> hurtbox);

	// ===== 答える係（ダメージの層が Initialize で登録する。当たりの層はダメージの層の型を知らない）=====
	// 受ける本体を決める
	using ReceiverFinder = Handle<Entity> (*)(Handle<Entity> hurtbox);
	// 当たり1件を受けるか決めて、ダメージの欄を書く。false なら断った（記録しない）
	using HitResolver = bool (*)(HitInfo& hit);
	static void SetReceiverFinder(ReceiverFinder finder);
	static void SetHitResolver(HitResolver resolver);

	// ===== ギズモ（エディタ用）=====
	// Hitbox の形（段階で色が変わる）・ジャスト判定の範囲・Hurtbox を線で描く
	static void DrawGizmos(Camera* camera);
	// 線を出すか（メニューバーの View から切り替える）
	static bool& GetShowGizmos();

private:
	// Entity が消える直前（EntityManager から呼ばれる）。実行中の値を片付ける
	static void OnEntityDestroyed(Handle<Entity> entity);
};
