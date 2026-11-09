#pragma once
#include <vector>
#include <string>
#include <string_view>

#include "HTTEngine/Components/Gameplay/AttackTableComponent.h"
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"

/// <summary>
/// 敵の今の様子（AttackTableSystem::GetStatus）。読むたびに作る写しなので、持ち続けても壊れないが、値は古くなる
/// </summary>
struct AttackTableStatus {
	bool isRunning = false;                               // 動き始めている（再生中で、表が選ばれている）
	bool isEnabled = false;                               // AI が止められていない
	AttackTableState state = AttackTableState::Selecting; // 状態
	AttackStage stage = AttackStage::None;                // 攻撃の段階（Attacking の間だけ）
	int phase = 0;                                        // 今のフェーズ（0 から）
	std::string phaseName;                                // 今のフェーズの名前
	std::string attackName;                               // 今の攻撃の名前（攻撃していなければ空）
	std::string lastAttackName;                           // 最後に選んだ攻撃の名前（1つも選んでいなければ空）
	float time = 0.0f;                                    // 今の段階・状態に入ってからの秒
	float duration = 0.0f;                                // 今の段階・状態の長さ（秒。Selecting はフェーズの Min Idle Seconds、Defeated は 0）

	// 残りの秒
	float GetRemaining() const { return (duration > time) ? duration - time : 0.0f; }
};

/// <summary>
/// 攻撃1行を今使えるかの結果（確認の画面用）
/// </summary>
struct AttackCheck {
	bool isPhaseOk = false;         // 今のフェーズで使える
	bool isDistanceOk = false;      // 距離が範囲の中
	bool isCooldownOk = false;      // クールタイムが終わっている
	bool isRepeatOk = false;        // 続けて使いすぎていない
	float cooldownRemaining = 0.0f; // クールタイムの残りの秒
	int repeatCount = 0;            // 今何回続けて使っているか
	float probability = 0.0f;       // 今選ぶなら、選ばれる確率（0〜1）
};


/// <summary>
/// 攻撃表の AI を動かす係（状態マシン・フェーズ・攻撃の抽選・段階の進行）
/// <para>流れ：Selecting → Attacking（Windup → Active → Recovery）→ Selecting。怯み・フェーズの移行・撃破が、待ちと攻撃を途中で止める</para>
/// <para>Selecting の間は、フェーズの待ちの部品（Idle Part）を動かす。Min Idle Seconds が過ぎて、使える攻撃があれば選ぶ</para>
/// <para>タイマー・クールタイム・部品の実体は、ここが敵ごとに持つ（再生が始まって最初の Update で作り、Stop で Entity と一緒に消える）</para>
/// <para>時間は Time::GetDeltaTime() だけで進める。0 の間（一時停止・ポーズ・時間停止・ヒットストップ）は、攻撃部品も含めて何も呼ばない</para>
/// </summary>
class AttackTableSystem {
public:
	// AttackTableComponent を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// 全部の敵を1フレーム進める。シーンの Update・Script・SYSTEM の後、ワールド行列の計算の前に呼ぶ（攻撃部品が動かした位置を、そのフレームの描画に使うため）
	/// </summary>
	/// <param name="isGameRunning">再生中か（一時停止中も true。そのときは deltaTime が 0 なので止まる）</param>
	/// <param name="deltaTime">経過秒（Time::GetDeltaTime）</param>
	static void Update(bool isGameRunning, float deltaTime);

	// ===== 外から指示する（ゲーム・Timeline）=====
	/// <summary>
	/// 怯ませる。攻撃の途中なら止める。怯んでいる間にもう一度呼んだら、残りの長い方
	/// </summary>
	/// <returns>怯んだか（撃破・フェーズの移行の間と、まだ動いていないときは false）</returns>
	static bool Stagger(Handle<Entity> entity, float seconds);

	// AI を止める・動かす（止めている間はタイマーも止まり、攻撃の途中ならそこから続く）
	static void SetEnabled(Handle<Entity> entity, bool isEnabled);

	/// <summary>
	/// 次の攻撃を名前で決める（条件を見ずに、次に選ぶときに必ずこの攻撃になる）
	/// <para>フェーズの Min Idle Seconds は待たない（演出の後に、すぐ大技を出すため）。攻撃の途中なら、終わってすぐ次がこの攻撃</para>
	/// </summary>
	/// <returns>その名前の攻撃があったか</returns>
	static bool RequestAttack(Handle<Entity> entity, std::string_view attackName);

	// ===== 外から読む（ゲームの判定・確認の画面）=====
	static AttackTableStatus GetStatus(Handle<Entity> entity);
	// 攻撃表の行ごとの結果を out に入れる（行の順。まだ動いていなければ空）
	static void GetAttackChecks(Handle<Entity> entity, std::vector<AttackCheck>& out);
	// 相手との距離（相手が居なければ無限大）。敵の位置の高さも含めた 3D の距離
	static float CalcTargetDistance(Handle<Entity> entity);
	// 敵が1体でも動いているか（再生中は、攻撃表の行を増やす・消すのを止める）
	static bool IsAnyRunning();

private:
	// Entity が消える直前（EntityManager から呼ばれる）
	static void OnEntityDestroyed(Handle<Entity> entity);
};