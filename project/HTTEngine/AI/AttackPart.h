#pragma once
#include <vector>
#include <memory>
#include <string>
#include <string_view>
#include <algorithm>
#include <type_traits>

#include "HTTEngine/Components/Gameplay/AttackTableComponent.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/ECS/GameObject.h"

// 敵の攻撃の中身を書くための基底クラス
//   // DashToTarget.cpp
//   class DashToTarget : public AttackPart {
//   public:
//       void OnUpdate(const AttackPartContext& context) override; // 相手へ走る。近づいたら FinishStep()
//       void Fields(ComponentUI& ui) override { ui.Field("Speed", speed_); } // 攻撃表に出す・保存する値
//   private:
//       float speed_ = 20.0f;
//   };
//   ATTACK_PART(DashToTarget, "相手へ走る。近づいたらステップを終える"); // これで攻撃表の部品の一覧に出る（説明は省略できる）
//
// 置き方は2つ
//   攻撃の部品：攻撃の Parts に置き、Runs In で動くステップを選ぶ。そのステップの間だけ OnBegin → OnUpdate → OnEnd が来る
//               Runs In を Whole Attack にすると、攻撃の始めから終わりまで動く（ステップに入るたびに OnStep も来る。このコメントでは「通しの部品」と呼ぶ）
//   待ちの部品：フェーズの Idle Parts に置く。攻撃を選ぶまで（Selecting の間）動く


// ===== 攻撃部品に渡す今の様子 =====
struct AttackPartContext {
	GameObject self;   // 自分
	GameObject target; // 狙う相手
	AttackTableState state = AttackTableState::Selecting; // 今の状態（ステップの部品・通しの部品は Attacking、待ちの部品は Selecting）
	AttackStage stage = AttackStage::None; // 今のステップの種類
	int step = -1;                         // 今のステップの番号（0から）
	std::string_view stepName;             // 今のステップの名前
	float stepTime = 0.0f;                 // 今のステップに入ってからの秒。ステップが変わると 0 に戻る（待ちの部品は Selecting に入ってからの秒）
	float stepDuration = 0.0f;             // 今のステップの長さの上限（表の Seconds）。時間では増えない。FinishStep・FailStep で、これより早く終わることがある
	float attackTime = 0.0f;               // 攻撃が始まってからの秒。ステップが変わっても 0 に戻らない（待ちの部品は 0）
	float deltaTime = 0.0f;                // このフレームの秒
	int phase = 0;                         // 今のフェーズ（0から）

	// 今のステップがどこまで進んだか（0〜1）
	float GetStepProgress() const { return (stepDuration > 0.0f) ? (std::min)(stepTime / stepDuration, 1.0f) : 1.0f; }
};

// ===== 部品の終わり方 =====
enum class AttackPartEnd {
	Finished,    // 通しの攻撃が終わった
	Interrupted, // 途中で止められた（怯み・撃破等）
};

// ===== 部品からステップへの頼み =====
enum class AttackStepRequest {
	None,   // 頼みなし
	Finish, // 今のステップを終えてNextの行き先へ
	Fail,   // 今のステップを終えてOnFailの行き先へ
};


/// <summary>
/// 攻撃部品
/// </summary>
class AttackPart {
public:
	virtual ~AttackPart() = default;

	virtual void OnBegin(const AttackPartContext& /*context*/) {}  // 攻撃部品の始まったときに更新
	virtual void OnStep(const AttackPartContext& /*context*/) {}   // ステップに入った始まりに更新
	virtual void OnUpdate(const AttackPartContext& /*context*/) {} // 毎フレーム
	virtual void OnEnd(const AttackPartContext& /*context*/, AttackPartEnd /*reason*/) {} // 終わったとき

	// 攻撃表に出す・保存する値。表はシーンの外のファイルなので、Entityへの参照は書けない
	virtual void Fields(ComponentUI& /*ui*/) {}

	// 最後に出された頼みを返して、頼みなしに戻す（AttackSystemが呼ぶのでゲームからは呼ばない）
	AttackStepRequest TakeStepRequest();

protected:
	void FinishStep(); // 今のステップを終えてNextの行き先へ進む
	void FailStep();   // 今のステップを失敗で終えてOnFailの行き先へ進む

	/// <summary>
	/// Hitboxの窓を今のステップの種類に合わせる
	/// <para>Windup → Warning、 Active → Active、 Recovery・None → Off</para>
	/// </summary>
	static void SyncHitbox(GameObject hitbox, const AttackPartContext& context);

private:
	AttackStepRequest stepRequest_ = AttackStepRequest::None; // 出された頼み
};


/// <summary>
/// 攻撃部品の種類の表。ATTACK_PART(...)とエンジンの部品が登録する
/// </summary>
class AttackPartRegistry {
public:
	using CreateFunction = std::unique_ptr<AttackPart>(*)();

	// 登録する
	static void Register(const char* name, CreateFunction create, const char* description);
	// 名前から作る
	static std::unique_ptr<AttackPart> Create(std::string_view name);
	// 名前の一覧
	static std::vector<std::string> GetNames();
	// 説明（ATTACK_PART(...)の2つ目）
	static const std::string& GetDescription(std::string_view name);
};


/// <summary>
/// ATTACK_PART(...) が1つ作る物。作られたときに表へ登録する
/// </summary>
template<class T> class AttackPartRegistrar {
	static_assert(std::is_base_of_v<AttackPart, T>, "ATTACK_PART(...) に書けるのは、AttackPartを継承したクラスだけです");
	static_assert(std::is_default_constructible_v<T>, "攻撃部品は引数の無いコンストラクタで作れるようにしてください（エンジンが名前から作るため）");

public:
	AttackPartRegistrar(const char* name, const char* description) { AttackPartRegistry::Register(name, &Create, description); }

	// 作り方（エンジンの部品を Initialize で登録するときにも使う）
	static std::unique_ptr<AttackPart> Create() { return std::make_unique<T>(); }
};

/// <summary>
/// 攻撃部品を登録する。ゲームの .cpp に ATTACK_PART(クラス名); か ATTACK_PART(クラス名, "説明"); と1行書く
/// <para>クラス名がそのまま、攻撃表の部品の名前になる（変えると、前に保存した表は読めなくなる）。説明は部品の欄に出る</para>
/// <para>エンジン（スタティックライブラリ）の中では使えない（どこからも呼ばれない .obj はリンカに捨てられ、登録が走らない）。エンジンの部品は BuiltInAttackParts.cpp で登録する</para>
/// </summary>
// "" __VA_ARGS__ は、説明を書かなければ ""、書けば "" "説明" ＝ "説明"（並んだ文字列はつながる。SCRIPT と同じ）
#define ATTACK_PART(Type, ...) inline const AttackPartRegistrar<Type> attackPartRegistrarOf_##Type(#Type, "" __VA_ARGS__)