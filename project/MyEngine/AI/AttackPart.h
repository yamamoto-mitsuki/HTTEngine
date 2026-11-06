#pragma once
#include <vector>
#include <memory>
#include <string>
#include <string_view>
#include <algorithm>
#include <type_traits>

#include "MyEngine/Components/Gameplay/AttackTableComponent.h"
#include "MyEngine/ECS/GameComponent.h"
#include "MyEngine/ECS/GameObject.h"

// 敵の攻撃の中身（攻撃部品）を書くための基底クラス。ゲーム側はこのヘッダ1つをインクルードする
//
//   // DashAttack.cpp
//   class DashAttack : public AttackPart {
//   public:
//       void OnStage(const AttackPartContext& context) override;
//       void OnUpdate(const AttackPartContext& context) override;
//       void Fields(ComponentUI& ui) override { ui.Field("Speed", speed_); } // 攻撃表に出す・保存する値
//   private:
//       float speed_ = 20.0f;
//   };
//   ATTACK_PART(DashAttack, "相手の向きへまっすぐ走る"); // これで攻撃表の Part の一覧に出る（説明は省略できる）
//
// 実体は敵1体の攻撃1行（待ちの部品はフェーズ1つ）につき1つできるので、メンバ変数に攻撃の途中の値を持ってよい
//
// 同じ部品を、攻撃の行の Part にも、フェーズの Idle Part（攻撃を選ぶまでの待ち）にも選べる
// どちらで呼ばれたかは context.state で分かる（攻撃は Attacking、待ちは Selecting）


/// <summary>
/// 攻撃部品に渡す今の様子
/// </summary>
struct AttackPartContext {
	GameObject self;                                      // 自分（この敵）
	GameObject target;                                    // 狙う相手（居なければ if (context.target) が false）
	AttackTableState state = AttackTableState::Selecting; // 今の状態（攻撃の部品は Attacking、待ちの部品は Selecting）
	AttackStage stage = AttackStage::None;                // 今の段階（待ちの部品は None）
	float stageTime = 0.0f;                               // 今の段階に入ってからの秒（待ちの部品は Selecting に入ってからの秒）
	float stageDuration = 0.0f;                           // 今の段階の長さ（秒。待ちの部品はフェーズの Min Idle Seconds）
	float deltaTime = 0.0f;                               // このフレームの秒（スローが掛かった物）
	int phase = 0;                                        // 今のフェーズ（0 から）

	// 今の段階がどこまで進んだか（0〜1）
	float GetStageProgress() const { return (stageDuration > 0.0f) ? (std::min)(stageTime / stageDuration, 1.0f) : 1.0f; }
};

/// <summary>
/// 攻撃の終わり方
/// </summary>
enum class AttackPartEnd {
	Finished,    // 硬直まで終わった（待ちの部品は、攻撃を選んだ）
	Interrupted, // 途中で止められた（怯み・フェーズの移行・撃破・表の作り直し・敵が消えた）
};


/// <summary>
/// 攻撃部品。継承して、使う関数だけ override する
/// <para>呼ばれるのは時間が進んでいるときだけ（ポーズ・時間停止・ヒットストップ・AI 停止の間は呼ばれない）</para>
/// <para>攻撃の Part のときの順番は ①OnBegin ②OnStage（予備動作）③OnUpdate を毎フレーム ④段階が変わるたびに OnStage ⑤OnEnd</para>
/// <para>Idle Part のときの順番は ①Selecting の最初のフレームに OnBegin ②同じフレームから OnUpdate を毎フレーム
/// ③攻撃を選んだら、その攻撃の OnBegin より先に OnEnd（Finished）。OnStage は呼ばれない</para>
/// </summary>
class AttackPart {
public:
	virtual ~AttackPart() = default;

	virtual void OnBegin(const AttackPartContext& /*context*/) {}                         // 攻撃に選ばれた
	virtual void OnStage(const AttackPartContext& /*context*/) {}                         // 段階に入った（context.stage で分ける）
	virtual void OnUpdate(const AttackPartContext& /*context*/) {}                        // 毎フレーム
	virtual void OnEnd(const AttackPartContext& /*context*/, AttackPartEnd /*reason*/) {} // 終わった（途中で止められたときも必ず呼ばれる）

	// 攻撃表に出す・保存する値（書き方は Script::Fields と同じ）。攻撃1行ごとに別の値を持てる
	// 表はシーンの外のファイルなので、Entity への参照（EntityRef・GameObject）は書けない。Prefab は AssetField で書ける
	virtual void Fields(ComponentUI& /*ui*/) {}

protected:
	/// <summary>
	/// Hitbox の窓を、今の段階に合わせる（OnStage で呼ぶ）。攻撃表と Hitbox はお互いを知らないので、つなぐのは攻撃部品
	/// <para>予備動作 → Warning（予告。段階の残りの秒）、攻撃 → Active（新しい攻撃の回。段階の残りの秒）、硬直 → Off。OnEnd でも呼ぶか HitSystem::Deactivate で閉じる</para>
	/// </summary>
	static void SyncHitbox(GameObject hitbox, const AttackPartContext& context);
};


/// <summary>
/// 攻撃部品の種類の表（名前 → 作り方）。ATTACK_PART(...) が登録する
/// </summary>
class AttackPartRegistry {
public:
	using CreateFunction = std::unique_ptr<AttackPart> (*)();

	static void Register(const char* name, CreateFunction create, const char* description);
	// 名前から作る（知らない名前なら nullptr）
	static std::unique_ptr<AttackPart> Create(std::string_view name);
	// 名前の一覧（名前の順。攻撃表の Part の選択肢）
	static std::vector<std::string> GetNames();
	// 説明（ATTACK_PART(...) の2つ目。知らない名前・書いていなければ空）
	static const std::string& GetDescription(std::string_view name);
};



/// <summary>
/// ATTACK_PART(...) が1つ作る物。作られたときに表へ登録する
/// </summary>
template<class T> 
class AttackPartRegistrar {
	static_assert(std::is_base_of_v<AttackPart, T>, "ATTACK_PART(...) に書けるのは、AttackPart を継承したクラスだけです");
	static_assert(std::is_default_constructible_v<T>, "攻撃部品は引数の無いコンストラクタで作れるようにしてください（エンジンが名前から作るため）");

public:
	AttackPartRegistrar(const char* name, const char* description) { AttackPartRegistry::Register(name, &Create, description); }

private:
	static std::unique_ptr<AttackPart> Create() { return std::make_unique<T>(); }
};


/// <summary>
/// 攻撃部品を登録する。.cpp に ATTACK_PART(クラス名); か ATTACK_PART(クラス名, "説明"); と1行書く
/// <para>クラス名がそのまま、攻撃表の Part の名前になる（変えると、前に保存した表は読めなくなる）。説明は Part の欄に出る</para>
/// </summary>
// "" __VA_ARGS__ は、説明を書かなければ ""、書けば "" "説明" ＝ "説明"（並んだ文字列はつながる。SCRIPT と同じ）
#define ATTACK_PART(Type, ...) inline const AttackPartRegistrar<Type> attackPartRegistrarOf_##Type(#Type, "" __VA_ARGS__)