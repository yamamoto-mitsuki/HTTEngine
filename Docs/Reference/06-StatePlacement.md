> HTTEngine リファレンスの1章。ほかの章は [索引](../Reference.md) から探す。

# 6. 状態をどこに置くか

**これが一番よく迷う所。** 置き場所は4つあって、使い分けは「**誰の状態か**」で決まる。

| 置き場所 | 誰の状態か | 例 |
|---|---|---|
| ① **Componentの中** | **その物1つだけ**の状態（同じ処理を、たくさんの Entity にまとめてかける物） | 敵のHP、弾の速度 |
| ①' **Script のメンバ**（[22章](22-Script.md)） | **その物1つだけ**の状態（1体ずつ違う動きをする物。`std::vector`・`std::string` も持てる） | プレイヤーの残機・入力の受け付け、ゲームの進行役の予定表 |
| ② **ゲームに1つだけの入れ物** | ゲーム全体の状態 | スコア、倒した数、残り時間、今のステージ |
| ③ **タグComponent（付いているかどうか）** | 状態の切り替え | 倒された、無敵中、狙われている |

## 敵が複数いるとき（よくある勘違い）

**「敵ごとに倒したかのフラグ」はComponentに入れる。** Componentは**Entityごとに1個ずつ**用意されるので、`Enemy` を10体に付ければ `Enemy` の実体も10個ある。グローバル変数にするのは**全体で1個しかない物だけ**。

```cpp
// ○ 良い例
// Source/Components/Enemy.cpp
struct Enemy {
	int hp = 3;          // ← 敵ごとに1個。10体いれば10個ある
	bool isDefeated = false;
	float respawnTimer = 0.0f;
};

COMPONENT(Enemy, "Enemy") {
	ui.Field("HP", value.hp, Range(0.0f, 20.0f));
	ui.Field("Defeated", value.isDefeated);
}

// 倒されたかの判定は「その敵の中」で完結する
void EnemyDeath(Handle<Entity> entity, Enemy& enemy) {
	if (enemy.hp > 0 || enemy.isDefeated) {
		return;
	}
	enemy.isDefeated = true;
	++gGameState.killCount;         // ← 「全体で何体倒したか」だけがグローバル
	gGameState.speedBonus += 0.5f;
	EntityManager::Destroy(entity); // 自分を消す
}
SYSTEM(EnemyDeath);
```

```cpp
// Source/GameState.h  ← ゲーム全体の状態（1個しかない物だけ）
#pragma once
struct GameState {
	int killCount = 0;
	float speedBonus = 0.0f;
	bool isCleared = false;
};
inline GameState gGameState; // inline を付けると、どのcppから見ても「同じ1個」
```

```cpp
// ✗ だめな例：敵ごとの状態をグローバルに置く
struct GameState {
	bool enemyDefeated;          // ✗ どの敵の話？ 敵が2体になった瞬間に破綻する
	bool enemy1Defeated;         // ✗ 敵が増えるたびに変数と if が増える
	bool enemy2Defeated;
	std::vector<bool> defeated;  // ✗ 番号と敵の対応を自分で管理することになる（消えたらずれる）
};
```

**Componentは「Entityごとの変数」そのもの。** 配列の管理はエンジンがやるので、自分で `vector<Enemy>` を持つ必要はない。

## 全体から「敵がまだ何体いるか」を知りたい

```cpp
// ○ 良い例：数えるのはSystemか、シーンのUpdateで
int aliveCount = 0;
EntityManager::ForEach<Enemy>([&](Handle<Entity> entity, Enemy& enemy) {
	if (EntityManager::IsActiveInHierarchy(entity) && !enemy.isDefeated) {
		++aliveCount;
	}
});
if (aliveCount == 0) {
	gGameState.isCleared = true; // クリア処理へ
}
```

## 特定の1体を覚えておきたい（ボスなど）

```cpp
// ○ 良い例：Handle か EntityId で覚える
class GameScene : public IScene {
private:
	Handle<Entity> boss_;  // 同じ実行中ならこれでよい
	EntityId bossId_ = 0;  // 消して作り直しても追いたいならこっち（FindById で引く）
};
```

> シーンファイルを使うシーンでは、**Play・Stop・シーン切り替えのたびに Entity が作り直される**（Handle は変わり、EntityId は同じ）。シーンのクラスも作り直されて `Initialize` がまた呼ばれるので、メンバの Handle は `Initialize` の中で探し直す。探す目印には、空のタグ Component（`struct Boss {};`）を付けて `ForEach<Boss>` で見つけるのが楽。

```cpp
// ✗ だめな例
Enemy* boss_;  // ✗ 配列が詰め直されると別の敵を指す
```

## 別のEntityの状態を見たい（追尾など）

```cpp
// ○ 良い例：相手を EntityRef でComponentに持って、その場で取る
struct Homing {
	EntityRef target; // EntityId で覚える（Handle と違い、Stop・削除の Undo・再起動の後も同じ相手）
	float speed = 5.0f;
};

// Inspector で相手を選べるようにする（Hierarchy からドラッグ＆ドロップ）
// コードで入れるときは homing.target = EntityManager::RefOf(handle);
COMPONENT(Homing, "Movement") {
	ui.Field("Target", value.target);
	ui.Field("Speed", value.speed, AtLeast(0.0f));
}

void HomingMove(Homing& homing, TransformComponent& transform, float deltaTime) {
	const TransformComponent* targetTransform = EntityManager::Get<TransformComponent>(homing.target); // EntityRef のまま取れる
	if (targetTransform == nullptr) {
		return; // 相手はもう居ない（入っていない）
	}
	const Vector3 toTarget = Normalize(targetTransform->translation - transform.translation);
	transform.translation += toTarget * (homing.speed * deltaTime);
}
SYSTEM(HomingMove);
```

## System同士のつなぎ方

**直接呼び合わない。「片方が書いて、片方が読む」にする。** そうすると呼ぶ順番が変わっても壊れない。

```cpp
// ○ 良い例：印（タグComponent）を付けて、別のSystemが拾う
struct Dead {}; // 中身は空でよい
COMPONENT(Dead, "Enemy") { ui.Label("倒された印"); }

void EnemyDeath(Handle<Entity> entity, Enemy& enemy) {
	if (enemy.hp <= 0) {
		EntityManager::RequestAdd<Dead>(entity); // 印を付けるだけ
	}
}
SYSTEM(EnemyDeath);

void DeadEffect(Handle<Entity> entity, Dead&, TransformComponent& transform) {
	transform.scale = transform.scale * 0.9f; // 縮んで消える演出
	if (transform.scale.x < 0.05f) {
		EntityManager::Destroy(entity);
	}
}
SYSTEM(DeadEffect);
```
