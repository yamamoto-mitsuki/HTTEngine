#pragma once
#include <memory>
#include <source_location>
#include <type_traits>
#include <typeinfo>
#include <vector>

#include "HTTEngine/Combat/HitInfo.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/ECS/GameObject.h"
#include "HTTEngine/Systems/Gameplay/ScriptSystem.h"
#include "HTTEngine/Time/EngineTime.h"
#include "HTTEngine/Timeline/TimelineEvent.h"


// ゲーム固有の振る舞いを、Unity の MonoBehaviour と同じ形で書くための基底クラス。ゲーム側はこのヘッダ1つだけをインクルードする
// （GameComponent.h も入っているので、Collider の当たり・音・カメラ・LoadScene もこれだけで使える。入力は InputManager.h を足す）
//
//   // PlayerController.h
//   class PlayerController : public Script {
//   public:
//       void Start() override;
//       void Update() override;
//       void Fields(ComponentUI& ui) override { ui.Field("Move Speed", moveSpeed_); } // Inspector に出す・保存する値
//   private:
//       float moveSpeed_ = 3.0f;
//   };
//
//   // PlayerController.cpp
//   SCRIPT(PlayerController); // これで Add Component → Gameplay に出る（SCRIPT(PlayerController, "Player") なら Gameplay → Player）
//   void PlayerController::Update() { transform.translation.x += moveSpeed_ * Time::GetDeltaTime(); }
//
//   COMPONENT / SYSTEM（GameComponent.h）との使い分け：1体ずつ違う動きをする物（Player・ボス・ゲームの進行）は Script、
//   同じ処理をたくさんの Entity にまとめてかける物（弾・敵の群れ）は COMPONENT / SYSTEM。理由は Docs/Tasks/Script.md の1章

/// <summary>
/// ぶつかった相手と、ぶつかった向き（Script の OnCollision〜 に渡す。Unity の Collision の小さい版）
/// </summary>
struct CollisionInfo {
	GameObject gameObject;                      // ぶつかった相手（Exit のときは、もう消えていることがある。if (collision.gameObject) で確かめる）
	Vector3 normal = {0.0f, 0.0f, 0.0f};        // 相手から自分へ押し出す向き（長さ1。ぶつかった面の向き）。相手の上に乗っていれば、だいたい (0, 1, 0)
	Vector3 selfVelocity = {0.0f, 0.0f, 0.0f};  // 自分の、ぶつかる直前の速さ（物理でぶつかったとき。押し合わない当たりは今の速さ。動き方の Component（Rigidbody・Character Controller）が無ければ0）
	Vector3 otherVelocity = {0.0f, 0.0f, 0.0f}; // 相手の、ぶつかる直前の速さ（同じ）

	// 近づいていた速さ（ぶつかった強さ。プラスなら近づいていた）。normal の向きの分だけ数える（かすっただけなら小さい）
	float GetImpactSpeed() const { return Dot(otherVelocity - selfVelocity, normal); }
};


/// <summary>
/// Script（Unity の MonoBehavior）。継承して、使う関数だけ override する
/// <para>エンジンの関数が呼ばれるのは再生中だけ。停止中は Inspector で値を変えるだけで、Awake も Update も呼ばれない</para>
/// <para>持ってよい物：何でも（std::vector・std::string・GameObject）。ただし Component・ほかの Script へのポインタは
/// フレームをまたいで持たない（GameObject で持って、使うときに GetComponent する）</para>
/// </summary>
class Script {
public:
	virtual ~Script() = default;

	// ===== エンジンが呼ぶ関数 =====
	virtual void Awake() {}      // 付いて最初のフレーム（Entity が有効なら）に1回。全部の Awake が、どの Start よりも先
	virtual void OnEnable() {}   // 有効になったとき（Awake の直後・enabled や SetActive で戻したとき）
	virtual void Start() {}      // 最初の Update の前に1回（有効なときだけ）
	virtual void Update() {}     // 毎フレーム（SYSTEM(...) より前）
	virtual void LateUpdate() {} // 毎フレーム（SYSTEM(...) の後。カメラの追いかけなど、全部が動いた後にしたいこと）
	virtual void OnDisable() {}  // 無効になったとき・消える前
	virtual void OnDestroy() {}  // 消えるとき（Awake を呼んだ物だけ）。Component はまだ読める

	// ===== 当たり（Collider 同士）=====
	// どちらかの Collider が Is Trigger なら OnTrigger〜（すり抜ける）、どちらも違えば OnCollision〜（Rigid body が付いていれば押し戻される）
	// Unity と違い、Rigid body が無くても呼ばれる。other が消えた後の Exit もある（そのときは if (other) が false）
	// OnCollision〜 には、相手とぶつかった向きと、ぶつかる直前の速さ（CollisionInfo）が来る。踏んだか（normal.y が大きい）・どちらがぶつかってきたか・ぶつかった強さに使う
	virtual void OnTriggerEnter(GameObject /*other*/) {}                 // 重なり始めた
	virtual void OnTriggerStay(GameObject /*other*/) {}                  // 重なっている
	virtual void OnTriggerExit(GameObject /*other*/) {}                  // 離れた
	virtual void OnCollisionEnter(const CollisionInfo& /*collision*/) {} // ぶつかり始めた
	virtual void OnCollisionStay(const CollisionInfo& /*collision*/) {}  // ぶつかっている（床に乗っている間もずっと）
	virtual void OnCollisionExit(const CollisionInfo& /*collision*/) {}  // 離れた（normal は離れる前のフレームの向き）

	// ===== 攻撃の当たり（Hitbox・Hurtbox・Health）=====
	// OnHitDealt・OnHitTaken は、当たった次のフレームの Update の前に来る（Collider の当たりと同じ）。hit.point がダメージの数字を出す所、hit.damage が量
	virtual void OnHitDealt(const HitInfo& /*hit*/) {} // 自分の攻撃が当たった（Hitbox の Entity と、撃った本人（Instigator）に来る）
	virtual void OnHitTaken(const HitInfo& /*hit*/) {} // 攻撃を受けた（受けた本体＝Health の付いた Entity に来る）。hit.isDefeated なら倒れた
	// 受ける前（HP を減らす前に、その場で呼ばれる。受けた本体に来る）。hit.result を Guarded（防いだ）・Ignored（断った）に変える・hit.damage を書き換えられる
	// 無敵のように「いつも断る」物は Health の Invincible に書く方がよい（Inspector で見えて、Script の無い物にも効く）
	virtual void OnBeforeHitTaken(HitInfo& /*hit*/) {}

	// ===== Timeline =====
	// Timeline のイベントトラックの時間になった（トラックの Target の Entity に付いている Script に来る）
	virtual void OnTimelineEvent(const TimelineEvent& /*event*/) {}

	// ===== Inspector に出す・保存する値 =====
	// 書き方は COMPONENT(...) の中と同じ（ui.Field の並びが、そのまま Inspector の見た目とシーンファイルの項目になる）
	// C++ にはクラスの変数を自動で調べる仕組みが無いので、ここだけは自分で並べる。"Enabled" はエンジンが使うので使わない
	virtual void Fields(ComponentUI& /*ui*/) {}

	// ===== 変数のように書ける値 =====
	__declspec(property(get = GetGameObject)) GameObject gameObject;        // 付いている GameObject
	__declspec(property(get = GetTransform)) TransformComponent& transform; // 付いている GameObject の位置・回転・大きさ
	__declspec(property(get = IsEnabled, put = SetEnabled)) bool enabled;   // false の間は Update などが呼ばれない

	GameObject GetGameObject() const { return gameObject_; }
	TransformComponent& GetTransform() const { return gameObject_.GetTransform(); }
	bool IsEnabled() const { return enabled_; }
	void SetEnabled(bool value) { enabled_ = value; } // OnEnable・OnDisable は、次の Update の前に呼ばれる

	// ===== Unity の MonoBehaviour と同じ関数 =====
	// 付いている GameObject の Component・Script（gameObject.GetComponent<T>() と同じ）
	template<class T> T* GetComponent() const { return gameObject_.GetComponent<T>(); }
	// 消す（どちらもフレームの終わりに消える。子も一緒に消える）
	static void Destroy(GameObject target) { EntityManager::Destroy(target.handle); }
	static void Destroy(Script* target) { ScriptSystem::RequestDestroy(target); }
	// その型の Script を全部探す（有効な GameObject に付いている物だけ）。ポインタは使い捨て
	template<class T> static std::vector<T*> FindObjectsOfType() {
		std::vector<Script*> found;
		ScriptSystem::FindAll([](const Script& script) { return dynamic_cast<const T*>(&script) != nullptr; }, found);
		std::vector<T*> result;
		result.reserve(found.size());
		for (Script* script : found) {
			result.push_back(dynamic_cast<T*>(script));
		}
		return result;
	}
	// その型の Script を1つ探す（無ければ nullptr）
	template<class T> static T* FindObjectOfType() {
		const std::vector<T*> all = FindObjectsOfType<T>();
		return all.empty() ? nullptr : all.front();
	}

private:
	// 付ける・呼ぶ・消すのは ScriptSystem だけ（ゲームが書き換えると、呼ぶ順番が壊れる）
	friend class ScriptSystem;

	GameObject gameObject_;           // 付いている GameObject（付けたときに ScriptSystem が入れる）
	bool enabled_ = true;             // Inspector の Enabled
	bool isAwake_ = false;            // Awake を呼んだ
	bool isEnableCalled_ = false;     // OnEnable を呼んで、まだ OnDisable を呼んでいない
	bool isStarted_ = false;          // Start を呼んだ
	bool isDestroyRequested_ = false; // Destroy(this) を頼まれた（フレームの終わりに消える）
};


/// <summary>
/// SCRIPT(...) が1つ作る物。作られた時点では「登録待ちの表」に並ぶだけ（COMPONENT(...) と同じ）
/// </summary>
template<class T> class ScriptRegistrar {
	static_assert(std::is_base_of_v<Script, T>, "SCRIPT(...) に書けるのは、Script を継承したクラスだけです");
	static_assert(std::is_default_constructible_v<T>, "Script は引数の無いコンストラクタで作れるようにしてください（エンジンが名前から作るため）");

public:
	// subCategory は Add Component の Gameplay の下の分け方（空なら Gameplay の直下）
	// where は書かなくてよい。既定の引数は「呼んだ側」で作られるので、SCRIPT(...) を書いたファイルと行が入る
	ScriptRegistrar(const char* name, const char* subCategory, std::source_location where = std::source_location::current()) {
		const char* sourceFile = where.file_name();
		ComponentRegistryDetail::AddPending([name, subCategory, sourceFile] { ScriptSystem::RegisterType(name, typeid(T), &Create, sourceFile, MakeGameplayCategory(subCategory)); });
	}

private:
	// 名前から作るときの作り方（シーンファイル・Add Component・Undo）
	static std::unique_ptr<Script> Create() { return std::make_unique<T>(); }
};


/// <summary>
/// Script を登録する。.cpp に SCRIPT(クラス名); か SCRIPT(クラス名, "サブカテゴリ"); と1行書く
/// <para>Add Component → Gameplay（→ サブカテゴリ）に出る。COMPONENT(...) と同じ所に並ぶ（見分けは「Script」の印と、Inspector の見出しの (Script)）</para>
/// <para>クラス名がそのまま、Inspector の見出しとシーンファイルの名前になる（変えると、前に保存した物は読めなくなる）</para>
/// </summary>
// "" __VA_ARGS__ は、サブカテゴリを書かなければ ""、書けば "" "Player" ＝ "Player"（並んだ文字列はつながる。COMPONENT と同じ）
#define SCRIPT(Type, ...) inline const ScriptRegistrar<Type> scriptRegistrarOf_##Type(#Type, "" __VA_ARGS__)