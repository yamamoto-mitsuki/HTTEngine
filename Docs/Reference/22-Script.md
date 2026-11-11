> HTTEngine リファレンスの1章。ほかの章は [索引](../Reference.md) から探す。

# 22. Script（1体ずつ違う動きを書く）

**Script は Unity の MonoBehaviour と同じ形。** クラスを継承して、使う関数だけ override する。`#include "HTTEngine/ECS/Script.h"` の1つで、`GameComponent.h`（当たり・音・カメラ・`LoadScene`）も使える（入力は `InputManager.h` を足す）。

| 使い分け | 書き方 |
|---|---|
| 1体ずつ違う動きをする物（プレイヤー・ボス・ゲームの進行役） | **Script** |
| 同じ処理を、たくさんの Entity にまとめてかける物（弾・敵の群れ） | `COMPONENT` / `SYSTEM`（[5章](05-GameComponent.md)） |

## 書き方

```cpp
// Source/Scripts/PlayerController.h
#pragma once
#include "HTTEngine/ECS/Script.h"

class PlayerController : public Script {
public:
	void Start() override;
	void Update() override;
	void Fields(ComponentUI& ui) override; // Inspector に出す・保存する値
private:
	float moveSpeed_ = 3.0f;
	GameObject body_;            // 子を覚えるなら GameObject（Handle だけなので持っていてよい）
	std::vector<Vector3> route_; // Script は std::vector・std::string も持てる
};
```

```cpp
// Source/Scripts/PlayerController.cpp
#include "Source/Scripts/PlayerController.h"
#include "HTTEngine/Input/InputManager.h"

SCRIPT(PlayerController, "Player"); // Add Component → Gameplay → Player に出る（"Player" を省くと Gameplay の直下）

void PlayerController::Fields(ComponentUI& ui) {
	ui.Field("Move Speed", moveSpeed_, AtLeast(0.0f)); // 書き方は COMPONENT の中と同じ（5章の表）
	ui.ListField("Route", route_, [](ComponentUI& ui, Vector3& point) { ui.Field("Point", point); });
}

void PlayerController::Start() {
	body_ = gameObject.FindChild("Body"); // 自分の子を名前で探す
}

void PlayerController::Update() {
	const Vector2 move = InputManager::GetMoveAxis();
	transform.translation.x += move.x * moveSpeed_ * Time::GetDeltaTime();
}
```

- **`SCRIPT(クラス名)` を .cpp に1行書く**と、Add Component に出る（Inspector の見出しに `(Script)` の印）。クラス名がそのまま保存の名前になるので、**クラス名を変えると前に保存した物は読めない**。
- 3つ目に説明を書くと、Inspector の見出し・Add Component の項目にマウスを乗せたときに出る：`SCRIPT(PlayerController, "Player", "プレイヤーの移動と回避");`（サブカテゴリ無しなら `SCRIPT(クラス名, "", "説明")`）。
- 引数の無いコンストラクタで作れること（エンジンが名前から作る）。
- `Fields` に書いた値だけが保存される（Inspector の見た目とシーンファイルの項目になる）。C++ はクラスの変数を自動で調べられないので、自分で並べる。ラベルに `"Enabled"` は使わない（エンジンが使う）。

## 呼ばれる関数

**エンジンの関数が呼ばれるのは再生中だけ**（停止中は Inspector で値を変えるだけで、Awake も Update も呼ばれない）。

| 関数 | いつ |
|---|---|
| `Awake` | 付いて最初のフレーム（Entity が有効なら）に1回。全部の Awake が、どの Start よりも先 |
| `OnEnable` / `OnDisable` | 有効になった・無効になったとき（Awake の直後、`enabled`・`SetActive` で変えたとき、消える前） |
| `Start` | 最初の Update の前に1回（有効なときだけ） |
| `Update` | 毎フレーム。シーンの `Update` の後、`SYSTEM(...)` の前（[1フレームの順番](01-Overview.md#1フレームの順番) の 3） |
| `LateUpdate` | 毎フレーム。`SYSTEM(...)` の後（全部が動いた後にしたいこと） |
| `OnDestroy` | 消えるとき（Awake を呼んだ物だけ）。Component はまだ読める |
| `OnCollision〜` / `OnTrigger〜` | Collider の当たり（[14章](14-MathCollision.md)） |
| `OnHitDealt` / `OnHitTaken` / `OnBeforeHitTaken` | 攻撃の当たり（[25章](25-HitDamage.md)） |
| `OnTimelineEvent` | Timeline のイベントトラックの時間になった（[21章](21-CurveTimeline.md)） |

- エディタの一時停止中は呼ばれない。ゲームのポーズ中（`Time::SetGamePaused(true)`）は、**開いている UI Screen の中の Script だけ**呼ばれる（[13章](13-Time.md)）。
- `enabled = false` の間は Update などが呼ばれない（OnEnable・OnDisable は次の Update の前）。

## 使える物

| 書き方 | 中身 |
|---|---|
| `gameObject` | 付いている GameObject（`name`・`activeSelf`・`layer`・`SetActive`・`FindChild`） |
| `transform` | 付いている Entity の Transform（`transform.translation` のように、そのまま書ける） |
| `GetComponent<T>()` | 同じ Entity の Component か Script（無ければ nullptr。Script は親クラスの型でも見つかる） |
| `gameObject.AddComponent<T>()` | Script なら付けて返す。データの Component なら予約（次のフレームの頭に付く。何も返さない） |
| `GameObject::Create("名前")` / `Find("名前")` / `Instantiate(prefab, 位置)` | 作る・シーン全体から名前で探す・Prefab から作る（[3章](03-Entity.md#prefab同じ-entity-を何個でも作る)） |
| `Destroy(gameObject)` / `Destroy(this)` | Entity ごと消す（子も）・この Script だけ外す。どちらもフレームの途中（1章の 8）で消える |
| `FindObjectOfType<T>()` / `FindObjectsOfType<T>()` | その型の Script を探す（有効な物だけ）。ポインタは使い捨て |
| `if (other)` | GameObject がまだ居るか（消えた相手は false。`GetComponent` も nullptr） |

## だめな例

```cpp
class Enemy : public Script {
	TransformComponent* target_;     // ✗ Component へのポインタをフレームをまたいで持つ（配列が詰め直されると別の物を指す）
	PlayerController* player_;       // ✗ ほかの Script へのポインタも同じ（消えたら壊れる）
	GameObject targetObject_;        // ○ GameObject で持って、使うときに GetComponent する
};

void Enemy::Start() {
	GetComponent<RigidbodyComponent>()->velocity = {}; // ✗ 同じフレームに AddComponent した Component はまだ無い（nullptr）
}
```

- Script を付けた Entity を Prefab にすると、Script と `Fields` の値も一緒に入る。
