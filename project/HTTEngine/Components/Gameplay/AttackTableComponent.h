#pragma once
#include <cstdint>
#include <type_traits>

#include "HTTEngine/Core/EnumTips.h"
#include "HTTEngine/ECS/Entity.h"

// 攻撃表の AI。Entityに付けるComponent（カテゴリ：Gameplay）
// 何をするかは攻撃表のファイル（.attacktable）に書く。ここは「どの表を使うか」と、ゲームから書く値だけを持つ
// タイマー・クールタイム・攻撃部品の実体は AttackTableSystem が持つ（数が表で変わるので、バイト列で写す Component には入れられない）

/// <summary>
/// AIの状態。Attackingの間だけ、攻撃のステップ（Step）が進む
/// </summary>
enum class AttackTableState {
	Selecting,       // 次の攻撃を選んでいる（Min Idle Seconds が過ぎるまでと、使える攻撃が無い間は、待ちの部品を動かして待つ）
	Attacking,       // 攻撃している（攻撃の行のステップを上から順に進める。ステップの数と並びは行ごとに違う）
	Staggered,       // 怯んでいる（AttackTableSystem::Stagger）
	PhaseTransition, // フェーズの移行中
	Defeated,        // 撃破された（ここからは動かない）
};

/// <summary>
/// ステップの種類。攻撃のステップごとに選ぶ目印で、ステップの数と並びは決められない
/// </summary>
enum class AttackStage {
	None,     // 目印なし（攻撃していない）
	Windup,   // 予備動作
	Active,   // 攻撃
	Recovery, // 硬直
};
// 選択肢ごとの説明
constexpr auto EnumTips(AttackStage) {
	using enum AttackStage;
	return MakeEnumTips<AttackStage>({
	    {None,     "目印なし"},
	    {Windup,   "予備動作"},
	    {Active,   "攻撃"   },
	    {Recovery, "硬直"   },
	});
}


/// <summary>
/// 攻撃表の AI を動かす設定と、ゲームから書く値
/// </summary>
struct AttackTableComponent {
	bool enabled = true;      // falseの間はAIを止める（タイマーも止まる。AttackTableSystem::SetEnabled でも変えられる）
	uint32_t table = 0;       // 攻撃表（AssetType::AttackTable の番号）
	EntityRef target;         // 狙う相手。距離の条件に使う
	float healthRatio = 1.0f; // 残りのHPの割合（0〜1）。同じ EntityにHealthがあればAttackTableSystemがそこから書く（無ければゲームが書く）。フェーズの切り替えと撃破はこの値で決まる
};

static_assert(std::is_trivially_copyable_v<AttackTableComponent>);