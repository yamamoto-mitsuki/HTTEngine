> HTTEngine リファレンスの1章。ほかの章は [索引](../Reference.md) から探す。

# 4. Component

Componentは「Entityに付けるデータ」。型ごとに `ComponentStorage<T>` という連続した配列に入る。

```cpp
// 付ける（予約。実際に付くのは次のフレームの頭）
EntityManager::RequestAdd<ModelRendererComponent>(ball, renderer);
EntityManager::RequestAdd<Spin>(ball); // 初期値でよければ値は省略できる

// 取る（持っていなければ nullptr）
if (ModelRendererComponent* renderer = EntityManager::Get<ModelRendererComponent>(ball)) {
	renderer->color = {1.0f, 0.0f, 0.0f, 1.0f}; // RGBA（0〜1）
}

// 外す（予約）
EntityManager::RequestRemove<Spin>(ball);

// 全部回す（その型を持っているEntityだけ）
EntityManager::ForEach<Spin>([](Handle<Entity> entity, Spin& spin) { … });
```

| 関数 | いつ効くか |
|---|---|
| `RequestAdd<T>` / `RequestRemove<T>` | **次のフレームの頭**（それまで `Get<T>` は `nullptr` のまま） |
| `IsAddPending<T>` | 追加を予約済みか |
| `Get<T>` | すぐ。反映済みの物だけ |
| `Destroy` | 同じフレームの後半（System の更新の途中、当たり判定と描画の前。1章の 8） |

## Componentに入れてよい物

```cpp
// ○ 良い例
struct Bullet {
	Vector3 velocity = {0.0f, 0.0f, 10.0f};
	float lifeTime = 3.0f;
	int damage = 1;
	bool piercing = false;
	EntityRef owner;        // 別のEntityへの参照は EntityRef（EntityIdで覚えるので、保存・Stop・Undoをまたいでも同じ相手）
};
```

```cpp
// ✗ だめな例
struct Bullet {
	std::string name;             // ✗ 可変長（ヒープを使う）
	std::vector<Vector3> path;    // ✗ 可変長
	Entity* owner;                // ✗ 生ポインタ
	ID3D12Resource* buffer;       // ✗ GPUリソース
	virtual void Update();        // ✗ 仮想関数（vtableポインタが入る）
};
```

理由：**Undoとコピーは中身を丸ごとバイト列で写している**ので、ヒープを指す物が入っていると壊れる。`static_assert(std::is_trivially_copyable_v<T>)` でコンパイル時に止まる。
文字は `FixedString<N>`（`String/FixedString.h`）、一覧は `FixedList<T, N>`（`Core/FixedList.h`。Inspector・保存は `ui.ListField`。5章）を使う。どちらも上限の数を決めて、中身を自分の中に持つので写せる。

## `ForEach` の最中にその型を増減させてよいか

```cpp
EntityManager::ForEach<Enemy>([](Handle<Entity> entity, Enemy& enemy) {
	EntityManager::RequestAdd<Enemy>(other); // ○ 予約なので、回している配列は動かない（付くのは次のフレームの頭）
	EntityManager::Destroy(entity);          // ○ これも予約
});
```

**予約（`RequestAdd` / `RequestRemove` / `Destroy`）は安全**。回している型の配列を、その場で動かす操作（エンジンの中の `FlushComponentChanges`・`SetNow` など）だけが assert で止まる（「ForEachで回している型の配列は、回し終わるまで増減できません」）。ゲームが踏みやすいのは **`ForEach<TransformComponent>` の中（最初の引数が `TransformComponent&` の SYSTEM も同じ）で `EntityManager::Create` する**こと。Transform は作った直後から付く（予約ではない。7章）ので止まる。Transform 以外の型を先頭にして回す。
