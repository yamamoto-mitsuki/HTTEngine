#pragma once
#include <vector>
#include <string>
#include <string_view>

#include "HTTEngine/Components/Gameplay/AttackTableComponent.h"
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"

// ===== 今の敵の様子（読むたびに作る写しなので、持ち続けても壊れないが値は古くなる） =====
struct AttackTableStatus {
	bool isRunning = false;                               // 動き始めている
	bool isEnabled = false;                               // 止める・動かす
	AttackTableState state = AttackTableState::Selecting; // 状態
	AttackStage stage = AttackStage::None;                // 今のステップの種類
	int phase = 0;                                        // 今のフェーズ（0から）
	int step = -1;                                        // 今のステップの番号（0から。攻撃していなければ -1）
	std::string phaseName;                                // 今のフェーズの名前
	std::string attackName;                               // 今の攻撃の名前
	std::string stepName;                                 // 今のステップの名前
	std::string lastAttackName;                           // 最後に選んだ攻撃の名前
	float time = 0.0f;                                    // 今のステップ・状態に入ってからの秒
	float duration = 0.0f;                                // 今のステップ・状態の長さ

	// 残りの秒
	float GetRemaining() const { return (duration > time) ? duration - time : 0.0f; }
};

// ===== 攻撃1行を今使えるかの結果（確認用）=====
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
/// 攻撃表のAIを動かす
/// </summary>
class AttackTableSystem {
public:
	static void Initialize();
	static void Release();

	/// <summary>
	/// 全部の敵を1フレーム進める。シーンの Update・Script・SYSTEM の後、ワールド行列の計算の前に呼ぶ
	/// </summary>
	static void Update(bool isGameRunning, float deltaTime);

	// ===== ゲーム・Timelineから呼び出す =====
	
	/// <summary>
	/// 怯ませる（攻撃の途中まら止める）
	/// <para>その敵の攻撃部品の中から呼んだときは、部品の呼び出しが終わってから怯む</para>
	/// </summary>
	/// <returns>怯むか（撃破・フェーズの移行の間とまだ動いていないときはfalse）</returns>
	static bool Stagger(Handle<Entity> entity, float seconds);

	// 止める・動かす
	static void SetEnabled(Handle<Entity> entity, bool isEnabled);

	/// <summary>
	/// 次の攻撃を名前で決める。現在の攻撃が完了したらMinIdleSeconds（攻撃間隔）は待たず、すぐに攻撃
	/// </summary>
	/// <returns>その名前の攻撃があったか</returns>
	static bool RequestAttack(Handle<Entity> entity, std::string_view attackName);

	// ===== 外から読む（ゲームの判定・確認の画面） =====
	// --- ゲーム ---
	static AttackTableStatus GetStatus(Handle<Entity> entity); // 状態を取得
	static float CalcTargetDistance(Handle<Entity> entity);    // 相手との距離
	// --- 確認用 ---
	static void GetAttackChecks(Handle<Entity> entity, std::vector<AttackCheck>& out); // 攻撃表の行ごとの結果をoutに入れる
	static bool IsAnyRunning(); // 動いている敵がいるか
	
private:
	// Entityが消える直前
	static void OnEntityDestroyed(Handle<Entity> entity);
};