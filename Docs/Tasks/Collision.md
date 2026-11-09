# Collision：当たり判定（S2）と、Play / Stop を繰り返すと落ちる件の直し

## このファイルについて

- Serialize.md の順番の **S2（Collider ＋ CollisionSystem）** と、S1 の確認（Serialize.md「4. 確認すること」の4）で見つかった **Play / Stop を繰り返すと落ちる件** の直しを書く。
- 話題ごとに1ファイルで、**終わっても消さずに更新し続ける**。終わったStepで消してよいのは写経用のコードだけ（コミットしてから）。「なぜ」は「決めたこと」の表へ移す。
- 各Stepの確認は **エンジンのビルド → ゲームのビルド → 実行して「確認すること」** の3段階。
- ブランチは `feature/collision`（`feature/scene-serialize` の 27418f5 から、2026-09-27 に作成）。S1 がまだ `develop` に入っていないので、S1 の先から切った。
- **2026-09-27：この後の `Audio.md` のパートAでフォルダを整理した。** このファイルに書いたパスは整理の前の物（`Collision/ColliderComponent.h` → `Components/`、`Collision/CollisionSystem.h/.cpp` → `Systems/`、`Component/GameComponent.h` → `ECS/`）。

| 読む順番 | 何が書いてあるか |
|---|---|
| [1. 落ちた原因](#1-play--stop-を繰り返すと落ちた原因) | ログの読み方と、SRVスロットが減り続けた理由 |
| [2. Collider で何が変わるか](#2-collider-で何が変わるか) | できること・ゲームでの書き方・仕組み |
| [3. 手順](#3-手順パート-ac) | 写経するコード（パートA〜C） |
| [4. 確認すること](#4-確認すること) | 写したあとに実行して見ること |
| [6. 決めたこと](#6-決めたこと) | 理由の一覧 |

---

## 進捗

| Step | 作業 | 状態 |
|---|---|---|
| **A** | **SRVスロットを返す（Play / Stop を繰り返すと落ちる件）** | **写経待ち（2026-09-27）** |
| **B** | **ColliderComponent ＋ CollisionSystem（S2）** | **写経待ち（2026-09-27）** |
| C | プロジェクトに追加・確認用のComponent | 写経待ち |

---

## 1. Play / Stop を繰り返すと落ちた原因

### 起きたこと

Play → Stop を繰り返すと、次のログで落ちる。

```
[DirectXCommon.cpp::AllocateSRVSlot 342] AssertError: instance_->nextSrvSlot_ < kSRVDescriptorHeap - SRVDescriptorHeapのスロットが不足しています
```

SRVDescriptorHeap は、シェーダーからテクスチャを読むための「番号付きの棚」で、**256個しかない**（`DirectXCommon::kSRVDescriptorHeap`）。`AllocateSRVSlot()` は「次の番号を渡して、1つ進める」だけで、**返す関数が無かった**。一度渡した番号は、持ち主が消えても二度と使われない。

### 何が番号を使っていたか（ログから読める）

`RenderTextureCube created ... SRVSlot=` の行を並べると、こうなっている。

| 時刻 | 操作 | 使った番号 | 何に使ったか |
|---|---|---|---|
| 22:51 | 起動 | 1〜27 | テクスチャ・画面の描き込み先・IBL（19〜22） |
| 22:56:37 | 1回目の Play | 28〜31 | IBL の焼き先（キューブ3枚 ＋ BRDF LUT 1枚） |
| （Play 中） | | 32〜49 | IBL の確認ウィンドウ（キューブ3枚 × 6面 ＝ 18個。ログには出ない） |
| 22:58:56 | Stop | 50〜53 | IBL の焼き先 |
| … | 同じことの繰り返し（1回の Play → Stop で 4 ＋ 18 ＋ 4 ＝ 26個） | … | |
| 23:09:53 | 9回目の Play | 236〜239 | IBL の焼き先 |
| 23:09:54 | （Play 中） | 240〜255 の16個しか残っていない | 確認ウィンドウの18個が取れずに落ちた |

- Play と Stop のたびにシーンが作り直され（Serialize.md の S1）、`GameScene::Initialize` の `environment_.MakeFromHDR(...)` が **IBL の焼き先を4つ新しく作る**。古い4つは消えるが、番号は返らない。
- Play 中は `GameScene::Update` が IBL の確認ウィンドウ（EnvCube / Irradiance / Prefilter）を出していて、`RenderTextureCube::GetFaceSRVGPUHandle` が**面ごとのSRVを18個作る**。これも返らない。
- 起動で27個、1往復で26個。(256 − 27) ÷ 26 ≒ 8.8 なので、**9回目の Play で必ず落ちる**。scratch で本物の DirectXCommon を本物のGPUで動かして、直す前のコードで同じ操作をすると、9回目の Play で同じ assert・同じ行で落ちるのを確かめた（「5. 検証したこと」）。

`RenderTexture.h` には `~RenderTexture(); // SRVスロットの解放` と書いてあるのに、`.cpp` の中身は空だった。作り直さない使い方（画面の描き込み先など）では困らなかったので、S1 で「作り直す」ようになって初めて表に出た。

### 「読み込んだテクスチャをもう一度数えているのでは」について

半分当たり。増えていたのは**テクスチャ（`TextureManager`）ではなく、描き込み先（`RenderTexture` / `RenderTextureCube`）と、確認用の面のSRV**。`TextureManager::Load` はパスで覚えていて、同じパスなら前の番号を返す（ログの `Loaded: resources/monsterBall/sky.hdr` は起動のときの1回しか出ていない）。描き込み先は毎回 `new` で作り直すので、そのたびに番号を取っていた。

### 「SRVスロットの合計も scene.json に保存されている」について

**保存されていない。** scene.json に書かれるのは Entity と Component だけ（`resources/scenes/GameScene.scene.json` を開くと分かる）。番号の数え（`nextSrvSlot_`）はメモリの中だけにあり、アプリを起動し直すと 1 から数え直す。

保存の後に落ちたように見えたのは、**その前の Play / Stop で残りがほとんど無くなっていて、次の Play で確認ウィンドウの18個が取れなかったから**。Save はSRVを1つも使わない。

### 直し方

- `DirectXCommon` に **返す関数（`FreeSRVSlot`）** を足し、`RenderTexture` / `RenderTextureCube` の**デストラクタで返す**（持ち主が消えたら必ず返る）。`AllocateSRVSlot` は、返してもらった番号を先に配る。
- **返した番号は、そのフレームの間は配らない。** フレームの終わりにGPUを待った後（`RecycleFreedSRVSlots`）から配る。
- Profiler の **GPU → Stats に「SRV Slots」のバー**を足した（今いくつ使っているか）。Play / Stop を繰り返しても、ここが増え続けなければ直っている。

> **解説：なぜ、すぐに配り直さないのか**
>
> コマンドリストは「あとでGPUがまとめて実行する予定表」で、`SetGraphicsRootDescriptorTable` などで積んだのは「○番の棚を読め」という**番号だけ**。棚の中身（SRV）をGPUが読むのは、フレームの終わりに `ExecuteCommandLists` で実行したとき。
>
> もし「IBLを消す → 同じフレームで別のテクスチャを作る」と、すぐに同じ番号が配られて棚の中身が書き換わり、**その前に積んだ描画が、実行されるときには別の絵を読んでしまう**。そこで、返した番号は「返したばかりの列」にいったん置き、`WaitForGPU` で予定表が全部終わったのを確かめてから、配れる列へ移す。このエンジンは毎フレームの終わりにGPUを待っているので、1フレーム待つだけで安全になる。

---

## 2. Collider で何が変わるか

### できること

| 操作 | どうなるか |
|---|---|
| Inspector の **Add Component → Physics → Collider** | 当たり判定の形が付く。Scene ビューに**緑の線**で出る（ライトのギズモと同じく、エディタでは Game ビューにも出る。Release では出ない） |
| **Shape** | `Sphere`（球）か `Box`（箱）。Sphere なら **Radius**、Box なら **Size**（幅・高さ・奥行き）の欄が出る |
| **Center** | Entity の位置からのずれ（Entity のローカル座標。Entity の回転と大きさも掛かる） |
| **Enabled** | 外さずに、一時的に当たらなくする |
| 重なる | **両方の線が赤になる**（停止中でも。置き場所を合わせるときに使える） |
| Entity の **Scale** | 球は一番大きい軸の大きさ、箱は軸ごとの大きさが掛かる（親の Scale も） |
| Entity の **Rotation** | 箱は回らない（軸にそろった箱＝AABB）。Center のずれは回る |
| **View → Gizmos → Colliders** | 線を出す / 出さない |
| Save | シーンファイルに保存される。Undo も効く |
| ゲームのコード | `SYSTEM` の中で **`CollisionSystem::GetHits(entity)`** を読む。当たり始め・当たっている・離れた の3つが分かる |

**押し戻しはしない。** 重なったことを知らせるだけで、止める・跳ね返すはゲームが書く（Unity の「全部がトリガー」と同じ）。

### ゲームでの書き方

弾が当たったら、弾を消して敵の HP を減らす例（このまま `Source/Components/` に置いてビルドできることを確かめてある）。

```cpp
// 使い方の例（Reference.md・Collision.md に載せるコードのコンパイル確認用）
#include <HTTEngine/Component/GameComponent.h>

// ===== 弾：まっすぐ進む =====
struct Bullet {
	Vector3 velocity = {0.0f, 0.0f, 10.0f}; // 1秒に進む量
};
COMPONENT(Bullet, "Shooter") { ui.Field("Velocity", value.velocity); }

void BulletMove(Bullet& bullet, TransformComponent& transform, float deltaTime) { transform.translation += bullet.velocity * deltaTime; }
SYSTEM(BulletMove);

// ===== 敵：弾が当たるとHPが減り、0で消える =====
struct Enemy {
	int hp = 3;
};
COMPONENT(Enemy, "Shooter") { ui.Field("HP", value.hp); }

void EnemyHit(Handle<Entity> entity, Enemy& enemy) {
	for (const CollisionHit& hit : CollisionSystem::GetHits(entity)) {
		// 当たり始めだけ数える（Stay で数えると、重なっている間ずっと減る）
		if (hit.phase != HitPhase::Enter) {
			continue;
		}
		// 相手が弾のときだけ（相手が何者かは、持っているComponentで見分ける）
		if (EntityManager::Get<Bullet>(hit.other) == nullptr) {
			continue;
		}
		EntityManager::Destroy(hit.other); // 弾は消す
		--enemy.hp;
	}
	if (enemy.hp <= 0) {
		EntityManager::Destroy(entity);
	}
}
SYSTEM(EnemyHit);
```

- **相手が何者かは、相手が持っている Component で見分ける**（`EntityManager::Get<Bullet>(hit.other)` が `nullptr` でなければ弾）。タグやレイヤーは無い。
- **数えるのは `HitPhase::Enter`（当たり始め）で。** `Stay`（当たっている）は重なっている間ずっと毎フレーム来るので、HP がみるみる減る。床のダメージのように「乗っている間ずっと」なら `Stay`、範囲から出たのを知りたいなら `Exit`。
- `GetHits` が返すのは「このEntity から見た当たり」だけ（`hit.self` は必ず自分、`hit.other` が相手）。1組の当たりは、両方の Entity に1件ずつ届く。
- Collider は `Add Component` で付けても、コードで `EntityManager::RequestAdd<ColliderComponent>(entity, collider)` で付けてもよい（弾を撃つときなど）。

### 仕組み（読まなくても写せるが、知っておくと直せる）

#### 1フレームの中の順番

| 順 | 何が起きるか |
|---|---|
| 1 | ゲームの `SYSTEM`（**前のフレームの当たり**を `GetHits` で読む。ここで `Destroy` してよい） |
| 2 | `EntityManager::UpdateTransforms`（ワールド行列） |
| 3 | `EntityManager::FlushDestroy`（`Destroy` の予約を反映） |
| 4 | **`CollisionSystem::Update`**（全部の Collider の重なりを調べて、当たりの一覧を作り直す） |
| 5 | 描画（`CollisionSystem::DrawGizmos` も。4 で調べた形そのものを描くので、線と判定がずれない） |

- 判定は**消した後**に行う。弾が当たって `Destroy` したフレームに、もう一度その弾と当たることは無い。
- 当たりは「次のフレームの SYSTEM」が読む。動かしてから判定しているので、読んだ当たりは「今見えている位置」の結果。

#### 当たり始め・当たっている・離れた

毎フレーム、重なっている組（A と B）を全部集めて、前のフレームの組と見比べる。

| 前のフレーム | このフレーム | 届くもの |
|---|---|---|
| 無い | ある | `Enter`（当たり始め） |
| ある | ある | `Stay`（当たっている） |
| ある | 無い | `Exit`（離れた。Collider を外した・無効にした・相手が消えた、も含む） |

- 相手が消えたときは、残った方にだけ `Exit` が届く（`hit.other` は消えているので `EntityManager::Get` が `nullptr`）。消えた方には何も届かない（読む人がいない）。
- Play / Stop で作り直すと Handle が全部変わるので、**重なっている物は `Enter` からやり直し**になる（前の組の `Exit` は来ない）。

#### 大きさ

- 中心 ＝ Center を Entity のワールド行列で変換した点（Entity の回転・大きさ・位置が掛かる）。
- 球の半径 ＝ Radius × Entity の大きさの**一番大きい軸**（球のまま大きくするため。楕円にはならない）。
- 箱の大きさ ＝ Size × Entity の大きさ（軸ごと）。**回転は使わない**。

---

## 3. 手順（パート A〜C）

> **2026-09-27 に書いた。** 下のコードは全部、scratchpad に作ったエンジンとゲームの写しに入れて、コンパイル（エンジン84ファイル＋ゲーム、Debug / Release、`/W4`）でエラー0・新しい警告0、当たり判定の動作テスト 6176項目全通過、本物のGPUを使ったSRVのテストを確かめてある（「5. 検証したこと」）。画面の見た目と実際の操作はまだ確かめていないので、写したら「4. 確認すること」を見てほしい。

パート A と B は互いに独立していて、どちらから写してもよい（同じファイルを触る所もあるが、別の行）。**C（ゲーム側）はエンジンをビルドした後に**写す。**新しいファイルは3つ**（エンジン）＋確認用1つ（ゲーム）。

| パート | 中身 | ファイル |
|---|---|---|
| A | SRVスロットを返す | `DirectXCommon.h/.cpp`、`RenderTexture.cpp`（1行）、`RenderTextureCube.h/.cpp`（数行）、`WindowManager.cpp`（1行）、`Engine.cpp`（Profiler に4行） |
| B | Collider と CollisionSystem | 新規 `ColliderComponent.h`・`CollisionSystem.h/.cpp`、`Engine.cpp`（3か所）、`WindowManager.cpp`（3か所）、`ImGuiManager.cpp`（2か所）、`GameComponent.h`（2か所） |
| C | プロジェクトに追加・ビルド、確認用の Component | vcxproj、ゲーム側 `Source/Components/HitLog.cpp` |

---

### A. SRVスロットを返す

#### A-1. `HTTEngine/Graphics/GPU/DirectXCommon.h`

(1) `AllocateSRVSlot` の宣言（説明のコメントから `static uint32_t AllocateSRVSlot();` まで）を、次で差し替える。説明を1行直して、下に3つ足している。

```cpp
	/// <summary>
	/// SRVスロット番号を1つ発行する。
	/// <para>TextureManager・RenderTextureなどSRVを必要とする全クラスがこれを使う。</para>
	/// <para>返してもらったスロットがあれば、それを先に使う。無ければ新しい番号（スロット0はImGui予約済みのため1から）。</para>
	/// </summary>
	/// <returns>確保したスロット番号</returns>
	static uint32_t AllocateSRVSlot();

	/// <summary>
	/// SRVスロット番号を返す。RenderTextureなど、スロットをもらった物を消すときに呼ぶ
	/// <para>このフレームのコマンドがまだそのスロットを読むかもしれないので、すぐには配り直さない。
	/// フレームの終わりにGPUを待った後（RecycleFreedSRVSlots）から、また配られる。</para>
	/// <para>0（まだもらっていない）を渡したときと、DirectXCommonが無いときは何もしない。</para>
	/// </summary>
	static void FreeSRVSlot(uint32_t slot);

	/// <summary>
	/// このフレームに返してもらったスロットを、次から配れるようにする
	/// <para>フレームの終わりに WaitForGPU した後に1回呼ぶ（GPUがもう読まないと確かなときだけ）</para>
	/// </summary>
	static void RecycleFreedSRVSlots();

	// 今使っているSRVスロットの数（Profilerの表示用。スロット0のImGui用は数えない）
	static uint32_t GetUsedSRVSlotCount();
```

(2) 下の方のメンバ変数 `nextSrvSlot_` の下に、2つ足す。

```cpp
	// SRVスロットの発行カウンタ
	// スロット0はImGui予約済みのため1から開始する
	uint32_t nextSrvSlot_ = 1;
	// 返してもらって、また配れるスロット（AllocateSRVSlot はここから先に配る）
	std::vector<uint32_t> freeSrvSlots_;
	// このフレームに返してもらったスロット（GPUを待った後で freeSrvSlots_ へ移す）
	std::vector<uint32_t> pendingFreeSrvSlots_;
```

#### A-2. `HTTEngine/Graphics/GPU/DirectXCommon.cpp`

(1) 先頭のインクルードに `<algorithm>` を足す（`std::find` を使う）。

```cpp
#include "HTTEngine/Graphics/GPU/DirectXCommon.h"

#include <algorithm>
#include <cassert>
#include <format>
```

(2) `AllocateSRVSlot` の実装（`// SRVスロット番号の発行` の見出しから、関数の閉じ `}` まで）を、次で差し替える。`AllocateSRVSlot` の中身を直して、下に3つ足している。

```cpp
//=============================================================================
// SRVスロット番号の発行
// 返してもらったスロットがあれば、それを先に使う（無ければ新しい番号）
// スロット0はImGui予約済みのため1から開始する
//=============================================================================
uint32_t DirectXCommon::AllocateSRVSlot() {
	HTT_ASSERT_MSG(instance_, "DirectXCommon::Initialize()を先に呼んでください");
	// --- 返してもらったスロットを使い回す ---
	if (!instance_->freeSrvSlots_.empty()) {
		const uint32_t slot = instance_->freeSrvSlots_.back();
		instance_->freeSrvSlots_.pop_back();
		return slot;
	}
	// --- 無ければ新しい番号 ---
	HTT_ASSERT_MSG(instance_->nextSrvSlot_ < kSRVDescriptorHeap, "SRVDescriptorHeapのスロットが不足しています");
	return instance_->nextSrvSlot_++;
}

//=============================================================================
// SRVスロット番号の返却
// すぐには配り直さず、フレームの終わり（GPUを待った後）まで取っておく
//=============================================================================
void DirectXCommon::FreeSRVSlot(uint32_t slot) {
	// 終了のとき、DirectXCommonより後にRenderTextureが消えることがある（ヒープごと無いので、返す先も無い）
	if (instance_ == nullptr) {
		return;
	}
	// 0番はImGui用で、ここからは配っていない（RenderTextureなどでは「まだもらっていない」の意味）
	if (slot == 0) {
		return;
	}
	HTT_ASSERT_MSG(slot < instance_->nextSrvSlot_, "配っていないSRVスロットが返されました");
	// 同じスロットを2回返すと、別々の2つに同じスロットが配られてしまう（片方の絵がもう片方に化ける）
	const auto contains = [slot](const std::vector<uint32_t>& slots) { return std::find(slots.begin(), slots.end(), slot) != slots.end(); };
	HTT_ASSERT_MSG(!contains(instance_->freeSrvSlots_) && !contains(instance_->pendingFreeSrvSlots_), "同じSRVスロットが2回返されました");
	// このフレームのコマンドリストが、まだこのスロットを読むかもしれない。
	// すぐに配り直して別のテクスチャのSRVで上書きすると、GPUが実行するときには別の絵を読んでしまう
	instance_->pendingFreeSrvSlots_.push_back(slot);
}

//=============================================================================
// 返してもらったスロットを、次から配れるようにする
// フレームの終わりに WaitForGPU した後に呼ぶ（このフレームのコマンドは、もう全部終わっている）
//=============================================================================
void DirectXCommon::RecycleFreedSRVSlots() {
	HTT_ASSERT_MSG(instance_, "DirectXCommon::Initialize()を先に呼んでください");
	std::vector<uint32_t>& pending = instance_->pendingFreeSrvSlots_;
	instance_->freeSrvSlots_.insert(instance_->freeSrvSlots_.end(), pending.begin(), pending.end());
	pending.clear();
}

//=============================================================================
// 今使っているSRVスロットの数
//=============================================================================
uint32_t DirectXCommon::GetUsedSRVSlotCount() {
	HTT_ASSERT_MSG(instance_, "DirectXCommon::Initialize()を先に呼んでください");
	// 配った数（1番〜nextSrvSlot_-1番）から、返してもらった数を引く
	const uint32_t issued = instance_->nextSrvSlot_ - 1;
	const size_t returned = instance_->freeSrvSlots_.size() + instance_->pendingFreeSrvSlots_.size();
	return issued - static_cast<uint32_t>(returned);
}
```

- `FreeSRVSlot` の assert 2つは、使い方を間違えたときにすぐ気づくため。**同じ番号を2回返すと、別々の2つの描き込み先に同じ番号が配られ、片方の絵がもう片方に化ける**（落ちずに絵だけおかしくなるので、原因を探しにくい）。
- 番号の数（最大256）が少ないので、`std::find` で列を全部見ても問題にならない。

#### A-3. `HTTEngine/Graphics/RenderTarget/RenderTexture.cpp`

空だったデストラクタを差し替える。

```cpp
// ===== デストラクタ =====
// SRVスロットを返す（返さないと、作り直すたびに空きが減り続け、256個を使い切ったところで止まる）
RenderTexture::~RenderTexture() { DirectXCommon::FreeSRVSlot(srvSlot_); }
```

#### A-4. `HTTEngine/Graphics/RenderTarget/RenderTextureCube.h` / `.cpp`

(1) `.h` の一番下、`faceSrvGPU_` の下に1行足す（面ごとのSRVは、今は GPU ハンドルしか覚えていないので、返すための番号も覚える）。

```cpp
	// ImGui確認用
	void EnsureFaceSRVs();
	std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> faceSrvGPU_;
	std::vector<uint32_t> faceSrvSlots_; // 面ごとのSRVのスロット番号（消すときに返す）
```

(2) `.cpp` の空だったデストラクタを差し替える。

```cpp
// ===== デストラクタ =====
// SRVスロットを返す。ImGuiで面を見せるために作ったスロット（6つ）も返す
RenderTextureCube::~RenderTextureCube() {
	DirectXCommon::FreeSRVSlot(srvSlot_);
	for (uint32_t slot : faceSrvSlots_) {
		DirectXCommon::FreeSRVSlot(slot);
	}
}
```

(3) `.cpp` の `EnsureFaceSRVs` の頭に2行足す（`faceSrvSlots_.resize(6);` と `faceSrvSlots_[face] = slot;`）。

```cpp
void RenderTextureCube::EnsureFaceSRVs() {
	faceSrvGPU_.resize(6);
	faceSrvSlots_.resize(6);
	for (uint32_t face = 0; face < 6; ++face) {
		uint32_t slot = DirectXCommon::AllocateSRVSlot();
		faceSrvSlots_[face] = slot; // 消すときに返すので覚えておく
```

#### A-5. `HTTEngine/Window/WindowManager.cpp`（`PostRenderAll` の中）

フレームの終わりの `DirectXCommon::WaitForGPU();` の下に2行足す。

```cpp
	// 全ウィンドウのコマンドリストがコマンドキューに投げられた後、ここで待つ
	DirectXCommon::WaitForGPU();
	// GPUがもう読まないと確かになったので、このフレームに返してもらったSRVスロットを配り直せるようにする
	DirectXCommon::RecycleFreedSRVSlots();
```

#### A-6. `HTTEngine/Engine.cpp`（`EndFrame` の中）

`Frame Upload` のバーの下（`#endif` の前）に足す。

```cpp
	// 1フレームにGPU用に書いたデータの量（RenderContextのFrameUploadBuffer）
	constexpr float kBytesToMB = 1.0f / (1024.0f * 1024.0f);
	float uploadMB = static_cast<float>(RenderContext::GetLastFrameUploadBytes()) * kBytesToMB;
	float uploadCapacityMB = static_cast<float>(RenderContext::GetFrameUploadCapacity()) * kBytesToMB;
	Profiler::Category(ProfCategory::GPU).Group("Stats").Bar("Frame Upload", uploadMB, uploadCapacityMB, "MB");

	// SRVスロットの使っている数（Play / Stop を何回繰り返しても、ここが増え続けなければよい）
	float usedSrvSlots = static_cast<float>(DirectXCommon::GetUsedSRVSlotCount());
	float srvSlotCapacity = static_cast<float>(DirectXCommon::kSRVDescriptorHeap - 1); // 0番はImGui用
	Profiler::Category(ProfCategory::GPU).Group("Stats").Bar("SRV Slots", usedSrvSlots, srvSlotCapacity);
```

- テクスチャ（`TextureManager`）の番号は返さない。読み込んだテクスチャはアプリの終わりまで持つ作りなので、何度 `Load` しても増えない。
- ImGui のフォントの画像のスロットは、`ImGuiManager.cpp` が自分で使い回しているので、そのまま。

---

### B. Collider と CollisionSystem

#### B-1. 新規：`HTTEngine/Collision/ColliderComponent.h`

```cpp
#pragma once
#include "HTTEngine/Math/Vector3.h"

// 当たり判定の形と大きさ。Entityに付けるComponent（カテゴリ：Physics）
// 位置は持たない。付けたEntityのTransformから取る（ライトと同じ）
// 押し戻しはしない。重なったかどうかを CollisionSystem が調べて、ゲームは CollisionSystem::GetHits で読む


/// <summary>
/// 当たり判定の形
/// </summary>
enum class ColliderShape {
	Sphere, // 球。中心と半径
	Box,    // 箱。中心と大きさ。Entityを回しても箱は回らない（軸にそろった箱＝AABB）
};

/// <summary>
/// 当たり判定
/// </summary>
struct ColliderComponent {
	bool enabled = true;                         // falseの間は当たらない（外さずに、一時的に止めたいとき）
	ColliderShape shape = ColliderShape::Sphere; // 形
	Vector3 center = {0.0f, 0.0f, 0.0f};         // Entityの位置からのずれ（Entityのローカル座標。Entityの回転と大きさも掛かる）
	float radius = 0.5f;                         // 球の半径（Entityの大きさの、一番大きい軸を掛ける）
	Vector3 size = {1.0f, 1.0f, 1.0f};           // 箱の幅・高さ・奥行き（Entityの大きさを軸ごとに掛ける）
};
```

#### B-2. 新規：`HTTEngine/Collision/CollisionSystem.h`

```cpp
#pragma once
#include <span>

#include "HTTEngine/Collision/ColliderComponent.h"
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/Entity/Entity.h"

// 前方宣言
class Camera;


/// <summary>
/// 当たり方の段階
/// </summary>
enum class HitPhase {
	Enter, // 当たり始めた（このフレームから重なった）
	Stay,  // 当たっている（前のフレームから重なったまま）
	Exit,  // 離れた（前のフレームまで重なっていた）
};

/// <summary>
/// 当たり1件。self から見て、other と当たった
/// </summary>
struct CollisionHit {
	Handle<Entity> self;  // 自分
	Handle<Entity> other; // 相手（Exit のときは、相手がもう消えていることがある。そのときは EntityManager::Get が nullptr を返す）
	HitPhase phase;       // 当たり始め・当たっている・離れた
};


/// <summary>
/// ColliderComponent を持つEntity同士の重なりを調べて、「当たりの一覧」を作る
/// <para>押し戻しはしない。ゲームは SYSTEM の中で GetHits を読んで、消す・音を鳴らすなどを決める</para>
/// <para>1フレームの順番：SYSTEM（前のフレームの当たりを読む）→ ワールド行列 → 破棄の反映 → ここで判定 → 描画</para>
/// <para>一覧などの中身は CollisionSystem.cpp の中だけに置く（このヘッダはゲームのComponentのファイルがみんな読むので、軽くしておく）</para>
/// </summary>
class CollisionSystem {
public:
	// ColliderComponent を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// 全部の Collider の重なりを調べて、当たりの一覧を作り直す
	/// <para>EntityManager::FlushDestroy の後に呼ぶ（消すと決めたEntityは、もう当たらない）</para>
	/// </summary>
	static void Update();

	/// <summary>
	/// このEntityの当たり（self がこのEntityの物だけ）。無ければ空
	/// <para>中身は次の Update まで変わらない（SYSTEM の中で読む分には気にしなくてよい）</para>
	/// </summary>
	static std::span<const CollisionHit> GetHits(Handle<Entity> entity);

	// 全部の当たり（1組につき、両方から見た2件が入る。self の順に並んでいる）
	static std::span<const CollisionHit> GetAllHits();

	// Collider の線を描く（エディタ用。当たっている物は色を変える）
	static void DrawGizmos(Camera* camera);

	// 線を出すか（メニューバーの View から切り替える）
	static bool& GetShowGizmos();
};
```

- `std::span<const CollisionHit>` は「配列の一部を、写さずに指して渡す」ための型（先頭のポインタと数だけを持つ）。`for (const CollisionHit& hit : CollisionSystem::GetHits(entity))` のように、そのまま `for` で回せる。
- 一覧などの中身をヘッダに書かないのは、`GameComponent.h` がこのヘッダを読む（＝ゲームの Component のファイルが全部読む）ので、`<vector>` や描画のヘッダを持ち込んでビルドを遅くしないため。

#### B-3. 新規：`HTTEngine/Collision/CollisionSystem.cpp`

```cpp
#include "CollisionSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <tuple>
#include <vector>

#include "HTTEngine/Collision/Collision.h"
#include "HTTEngine/Component/ComponentSerializer.h"
#include "HTTEngine/Component/ComponentUI.h"
#include "HTTEngine/Component/GameComponent.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Entity/EntityManager.h"
#include "HTTEngine/Entity/TransformComponent.h"
#include "HTTEngine/Graphics/Renderer/Renderer.h"

namespace {
constexpr float kPI = std::numbers::pi_v<float>;
constexpr uint32_t kCircleDivision = 24;    // 球の円を何本の線で描くか
constexpr uint32_t kIdleColor = 0x66FF66FF; // 当たっていない Collider の線の色（緑）
constexpr uint32_t kHitColor = 0xFF4444FF;  // 当たっている Collider の線の色（赤）
constexpr float kNoFadeStart = 10000.0f;    // 距離でフェードさせないための値（LightGizmoと同じ）
constexpr float kNoFadeEnd = 20000.0f;

// ===== 判定に使う形 =====

/// <summary>
/// ワールド座標に直した Collider
/// </summary>
struct WorldCollider {
	Handle<Entity> entity;                       // 持ち主
	ColliderShape shape = ColliderShape::Sphere; // 形
	Sphere sphere{};                             // shape が Sphere のとき使う
	AABB box{};                                  // shape が Box のとき使う
};

/// <summary>
/// 重なっている2つ。first が小さい方（同じ組を「AとB」「BとA」の2回数えないように、並びをそろえる）
/// </summary>
struct HitPair {
	Handle<Entity> first;
	Handle<Entity> second;
};

/// <summary>
/// CollisionSystem が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct CollisionState {
	std::vector<WorldCollider> colliders; // このフレームに判定した形（ギズモもこれを描く＝見えている線と判定が同じ）
	std::vector<HitPair> currentPairs;    // このフレームに重なっていた組（並べてある）
	std::vector<HitPair> previousPairs;   // 前のフレームに重なっていた組（並べてある）
	std::vector<CollisionHit> hits;       // 当たりの一覧（self の順）
	Renderer::LineListConfig gizmoLines;  // ギズモの線の置き場（配列のメモリを使い回す）
	bool showGizmos = true;               // 線を出すか
};
std::unique_ptr<CollisionState> state;

// Handle の並び順（番号 → 世代）。組の並びをそろえるのと、二分探索に使う
bool HandleLess(const Handle<Entity>& a, const Handle<Entity>& b) { return std::tie(a.index, a.generation) < std::tie(b.index, b.generation); }

// 組の並び順（first → second）
bool PairLess(const HitPair& a, const HitPair& b) {
	if (a.first != b.first) {
		return HandleLess(a.first, b.first);
	}
	return HandleLess(a.second, b.second);
}

// 2つのEntityから、並びをそろえた組を作る
HitPair MakePair(Handle<Entity> a, Handle<Entity> b) { return HandleLess(a, b) ? HitPair{a, b} : HitPair{b, a}; }

// ローカル座標の点を、ワールド座標に直す（ワールド行列の1〜3行目＝ローカルのX・Y・Z軸（回転と大きさ入り）、4行目＝位置）
// ※ Matrix4x4.h の TransformNormal は、名前と違って位置も足す（点の変換）ので、向きの変換と間違えないようにここで書く
Vector3 TransformPoint(const Vector3& point, const Matrix4x4& m) {
	return {
	    point.x * m.m[0][0] + point.y * m.m[1][0] + point.z * m.m[2][0] + m.m[3][0],
	    point.x * m.m[0][1] + point.y * m.m[1][1] + point.z * m.m[2][1] + m.m[3][1],
	    point.x * m.m[0][2] + point.y * m.m[1][2] + point.z * m.m[2][2] + m.m[3][2],
	};
}

// Entityの大きさ（ワールド行列の1〜3行目の長さ＝ローカルのX・Y・Z軸がどれだけ伸びているか。親の大きさも入っている）
Vector3 GetWorldScale(const Matrix4x4& m) {
	return {
	    Length(Vector3{m.m[0][0], m.m[0][1], m.m[0][2]}),
	    Length(Vector3{m.m[1][0], m.m[1][1], m.m[1][2]}),
	    Length(Vector3{m.m[2][0], m.m[2][1], m.m[2][2]}),
	};
}

// Collider を、ワールド座標の球・箱に直す
WorldCollider MakeWorldCollider(Handle<Entity> entity, const ColliderComponent& collider, const TransformComponent& transform) {
	WorldCollider result;
	result.entity = entity;
	result.shape = collider.shape;
	// 中心：ずれはEntityのローカル座標なので、ワールド行列で変換する（回転と大きさが掛かってから、Entityの位置が足される）
	const Vector3 center = TransformPoint(collider.center, transform.worldMatrix);
	const Vector3 scale = GetWorldScale(transform.worldMatrix);
	if (collider.shape == ColliderShape::Sphere) {
		// 球のまま大きくするために、一番大きい軸の大きさを掛ける（std::max は windows.h の max マクロとぶつかるので使わない）
		float maxScale = scale.x;
		if (scale.y > maxScale) {
			maxScale = scale.y;
		}
		if (scale.z > maxScale) {
			maxScale = scale.z;
		}
		result.sphere = {center, std::abs(collider.radius) * maxScale};
	} else {
		// 箱は「中心から面までの距離（大きさの半分）」を軸ごとに出す。回転は使わない（軸にそろった箱）
		const Vector3 half = {
		    std::abs(collider.size.x) * scale.x * 0.5f,
		    std::abs(collider.size.y) * scale.y * 0.5f,
		    std::abs(collider.size.z) * scale.z * 0.5f,
		};
		result.box = {center - half, center + half};
	}
	return result;
}

// 2つが重なっているか（形の組み合わせで、使う関数を選ぶ）
bool IsOverlapping(const WorldCollider& a, const WorldCollider& b) {
	const bool aIsSphere = (a.shape == ColliderShape::Sphere);
	const bool bIsSphere = (b.shape == ColliderShape::Sphere);
	if (aIsSphere && bIsSphere) {
		return Collision::SphereSphere(a.sphere, b.sphere);
	}
	if (!aIsSphere && !bIsSphere) {
		return Collision::AABBAABB(a.box, b.box);
	}
	// 球と箱（どちらが球でも、球を先にして渡す）
	return aIsSphere ? Collision::SphereAABB(a.sphere, b.box) : Collision::SphereAABB(b.sphere, a.box);
}

// 1組を「両方から見た2件」にして足す。消えたEntityから見た分は足さない（読む人がいない）
void AddHits(std::vector<CollisionHit>& hits, const HitPair& pair, HitPhase phase) {
	if (EntityManager::IsAlive(pair.first)) {
		hits.push_back({pair.first, pair.second, phase});
	}
	if (EntityManager::IsAlive(pair.second)) {
		hits.push_back({pair.second, pair.first, phase});
	}
}

// このEntityが、今重なっているか（当たり始め・当たっている。離れた＝Exit は入れない）
bool IsTouching(Handle<Entity> entity) {
	for (const CollisionHit& hit : CollisionSystem::GetHits(entity)) {
		if (hit.phase != HitPhase::Exit) {
			return true;
		}
	}
	return false;
}

// ===== 線（ギズモ）=====

// axisA と axisB が作る平面に、中心 center・半径 radius の円を線で並べる（LightGizmo と同じ作り方）
void PushCircle(std::vector<Renderer::LineSegment>& lines, const Vector3& center, float radius, const Vector3& axisA, const Vector3& axisB, uint32_t color) {
	Vector3 prev = center + axisA * radius; // 角度0の点
	for (uint32_t i = 1; i <= kCircleDivision; ++i) {
		float angle = 2.0f * kPI * static_cast<float>(i) / static_cast<float>(kCircleDivision);
		Vector3 current = center + axisA * (std::cos(angle) * radius) + axisB * (std::sin(angle) * radius);
		lines.push_back({prev, current, color});
		prev = current; // 次の線の始点にする
	}
}

// 球：3方向の円を重ねる
void PushSphere(std::vector<Renderer::LineSegment>& lines, const Sphere& sphere, uint32_t color) {
	const Vector3 axisX = {1.0f, 0.0f, 0.0f};
	const Vector3 axisY = {0.0f, 1.0f, 0.0f};
	const Vector3 axisZ = {0.0f, 0.0f, 1.0f};
	PushCircle(lines, sphere.center, sphere.radius, axisX, axisY, color); // XY平面
	PushCircle(lines, sphere.center, sphere.radius, axisY, axisZ, color); // YZ平面
	PushCircle(lines, sphere.center, sphere.radius, axisZ, axisX, color); // ZX平面
}

// 箱：12本の辺
void PushBox(std::vector<Renderer::LineSegment>& lines, const AABB& box, uint32_t color) {
	// 8つの角。番号 i の 1・2・4 のビットで、x・y・z に min と max のどちらを使うかを決める
	Vector3 corners[8];
	for (uint32_t i = 0; i < 8; ++i) {
		corners[i] = {
		    (i & 1) ? box.max.x : box.min.x,
		    (i & 2) ? box.max.y : box.min.y,
		    (i & 4) ? box.max.z : box.min.z,
		};
	}
	// ビットが1つだけ違う角どうしを結ぶと、辺になる（1つの軸に4本 × 3軸 ＝ 12本）
	for (uint32_t i = 0; i < 8; ++i) {
		for (uint32_t bit = 1; bit < 8; bit <<= 1) {
			if ((i & bit) == 0) {
				lines.push_back({corners[i], corners[i | bit], color});
			}
		}
	}
}

// ===== Inspector と 保存 =====

// Collider の見せ方。Inspector とシーンファイルの両方がこの並びを使う（COMPONENT(...) の中身と同じ書き方）
// 保存の名前はエンジンの他の型にそろえてメンバ名にし、Inspector には「表示###保存の名前」で読みやすい名前を出す
void DescribeCollider(ComponentUI& ui, ColliderComponent& value) {
	ui.Field("Enabled###enabled", value.enabled);
	ui.Field("Shape###shape", value.shape);
	ui.Field("Center###center", value.center, Tip("Entityの位置からのずれ（Entityの回転と大きさも掛かる）"));
	// 形に合う大きさだけを見せる（保存も、使っている方だけが書かれる）
	if (value.shape == ColliderShape::Sphere) {
		ui.Field("Radius###radius", value.radius, AtLeast(0.0f).Tip("Entityの大きさの、一番大きい軸を掛けて判定する"));
	} else {
		ui.Field("Size###size", value.size, AtLeast(0.0f).Tip("幅・高さ・奥行き。Entityを回しても、箱は回らない"));
	}
}
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
void CollisionSystem::Initialize() {
	HTT_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<CollisionState>();
	// 線は距離でフェードさせない
	state->gizmoLines.fadeStartDistance = kNoFadeStart;
	state->gizmoLines.fadeEndDistance = kNoFadeEnd;

	// Collider の置き場所・保存・Inspector を登録する。見せ方は DescribeCollider 1つで、保存と Inspector の両方に使う
	EntityManager::RegisterComponent<ColliderComponent>();
	ComponentSerializer::Register<ColliderComponent>("Collider", &DescribeCollider);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<ColliderComponent>>("Collider", "Physics", &DescribeCollider));
}

void CollisionSystem::Release() { state.reset(); }


//=============================================================================
// 判定
//=============================================================================
void CollisionSystem::Update() {
	HTT_ASSERT_MSG(state != nullptr, "CollisionSystem::Initialize()を先に呼んでください");

	// ===== 1. 判定に使う形を集める（有効なEntityの、有効な Collider だけ）=====
	state->colliders.clear();
	EntityManager::ForEach<ColliderComponent>([](Handle<Entity> entity, const ColliderComponent& collider) {
		if (!collider.enabled || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		if (transform == nullptr) {
			return;
		}
		state->colliders.push_back(MakeWorldCollider(entity, collider, *transform));
	});

	// ===== 2. 全部の組を調べる（総当たり。n個なら n×(n-1)/2 回。数百個まではこれで足りる）=====
	std::vector<HitPair>& current = state->currentPairs;
	current.clear();
	const std::vector<WorldCollider>& colliders = state->colliders;
	for (size_t i = 0; i < colliders.size(); ++i) {
		for (size_t j = i + 1; j < colliders.size(); ++j) {
			if (IsOverlapping(colliders[i], colliders[j])) {
				current.push_back(MakePair(colliders[i].entity, colliders[j].entity));
			}
		}
	}
	std::ranges::sort(current, PairLess);

	// ===== 3. 前のフレームの組と見比べて、当たり始め・当たっている・離れた に分ける =====
	// どちらの列も同じ順に並べてあるので、先頭から1つずつ見比べれば「両方にある」「片方にしか無い」が分かる
	const std::vector<HitPair>& previous = state->previousPairs;
	std::vector<CollisionHit>& hits = state->hits;
	hits.clear();
	size_t c = 0; // current の何番目を見ているか
	size_t p = 0; // previous の何番目を見ているか
	while (c < current.size() || p < previous.size()) {
		if (p == previous.size() || (c < current.size() && PairLess(current[c], previous[p]))) {
			// 今だけにある ＝ 当たり始めた
			AddHits(hits, current[c], HitPhase::Enter);
			++c;
		} else if (c == current.size() || PairLess(previous[p], current[c])) {
			// 前だけにある ＝ 離れた（Colliderを外した・無効にした・Entityを消した、も含む）
			AddHits(hits, previous[p], HitPhase::Exit);
			++p;
		} else {
			// 両方にある ＝ 当たったまま
			AddHits(hits, current[c], HitPhase::Stay);
			++c;
			++p;
		}
	}
	// 自分（self）の順に並べ直す。GetHits が、自分の分だけを二分探索で切り出せるように
	std::ranges::stable_sort(hits, HandleLess, &CollisionHit::self);

	// 今回の組を、次のフレームの「前」にする（入れ替えるだけ。配列のメモリはどちらも使い回す）
	std::swap(state->previousPairs, state->currentPairs);
}


//=============================================================================
// 当たりの一覧
//=============================================================================
std::span<const CollisionHit> CollisionSystem::GetHits(Handle<Entity> entity) {
	HTT_ASSERT_MSG(state != nullptr, "CollisionSystem::Initialize()を先に呼んでください");
	// self の順に並んでいるので、自分の分は1か所にまとまっている。その範囲（最初と最後）を二分探索で探す
	const auto range = std::ranges::equal_range(state->hits, entity, HandleLess, &CollisionHit::self);
	return {range.begin(), range.end()};
}

std::span<const CollisionHit> CollisionSystem::GetAllHits() {
	HTT_ASSERT_MSG(state != nullptr, "CollisionSystem::Initialize()を先に呼んでください");
	return state->hits;
}


//=============================================================================
// ギズモ
//=============================================================================
void CollisionSystem::DrawGizmos(Camera* camera) {
	HTT_ASSERT_MSG(state != nullptr, "CollisionSystem::Initialize()を先に呼んでください");
	if (!state->showGizmos) {
		return;
	}
	Renderer::LineListConfig& gizmo = state->gizmoLines;
	gizmo.lines.clear(); // 中身だけ消す。確保済みのメモリは残るので、次から再確保が起きにくい
	gizmo.camera = camera;
	// 判定に使った形をそのまま描く（Update が集めた物。無効な Collider・無効なEntityは入っていない）
	for (const WorldCollider& collider : state->colliders) {
		const uint32_t color = IsTouching(collider.entity) ? kHitColor : kIdleColor;
		if (collider.shape == ColliderShape::Sphere) {
			PushSphere(gizmo.lines, collider.sphere, color);
		} else {
			PushBox(gizmo.lines, collider.box, color);
		}
	}
	Renderer::DrawLines(gizmo); // 線が0本なら中で何もしない
	gizmo.camera = nullptr;     // フレームをまたいで持たない
}

bool& CollisionSystem::GetShowGizmos() {
	HTT_ASSERT_MSG(state != nullptr, "CollisionSystem::Initialize()を先に呼んでください");
	return state->showGizmos;
}
```

> **解説：`Update` の 3.（前のフレームと見比べる所）**
>
> 組の列は、どちらも同じ決まり（`PairLess`）で並べてある。並んだ2つの列は、先頭から1つずつ見比べれば、全部を探し回らなくても「両方にある」「片方にしか無い」が分かる（トランプの札を2つの山から小さい順に出していくのと同じ）。
>
> ```
> 前      : (A,B) (A,C)       (B,D)
> このフレーム: (A,B)       (A,E) (B,D)
>            ↓     ↓     ↓     ↓
>          Stay  Exit  Enter  Stay     ← (A,C) は前だけ、(A,E) は今だけ
> ```
>
> 小さい方を1つ進め、同じなら両方を1つ進める。列の長さを n・m とすると、見比べるのは n＋m 回で済む。

> **解説：`std::ranges::equal_range(state->hits, entity, HandleLess, &CollisionHit::self)`**
>
> - `hits` は `self`（自分）の順に並べてある（`stable_sort` の所）。だから、同じ `self` の当たりは1か所にまとまっている。`equal_range` は、並んだ列の中から「`self` が `entity` の範囲（最初と最後）」を二分探索で探す。
> - 最後の `&CollisionHit::self` は「比べるときは、各要素の `self` だけを見る」という指定（C++20 の ranges の「射影」）。これが無いと、`CollisionHit` と `Handle` を比べる関数を別に書くことになる。
> - `stable_sort` の同じ指定も同じ意味。`stable_` は「同じ `self` の中では、元の並び（組の順）を崩さない」並べ替え。

> **解説：箱の12本の辺（`PushBox`）**
>
> 箱の8つの角に 0〜7 の番号を付け、番号の 1・2・4 のビットを「x・y・z に max を使うか」にする（`5` ＝ `101` なら x と z が max、y が min）。**ビットが1つだけ違う2つの角は、1つの軸の上だけ離れている**ので、それを結ぶと辺になる。各角から「まだ立っていないビット」を立てた角へ結ぶと、同じ辺を2回引かずに12本になる。

- **エンジンの型なのに `COMPONENT(...)` と同じ書き方（`DescribeCollider` 1つ）で、Inspector と保存の両方をまかなっている**のは、Collider が初めて。`GameComponent.h` の `DescribedComponentEditor` をそのまま使っている。ライトや Model Renderer は、今は Inspector（手書き）と保存（`EngineComponents.cpp`）を別に書いているが、いずれこの形に寄せる（Serialize.md「7. 未解決」）。
- 保存の名前は、エンジンの他の型にそろえてメンバ名（`enabled`・`shape`・`center`・`radius`・`size`）にした。Inspector には `"Radius###radius"` の書き方で読みやすい名前を出している（`###` の後ろが保存の名前）。
- 中心は `TransformPoint` を自分で書いている。**エンジンの `TransformNormal`（`Matrix4x4.h`）は、名前は「向きの変換」なのに、中で位置も足している（点の変換）。** 最初はこれを使って中心を「位置＋ずれ」で出していたら、位置を2回足していた（テストで見つけた）。`TransformNormal` はどこからも使われていないので、今回は触らない（「7. 未解決」）。
- `std::max` は `windows.h` の `max` マクロとぶつかるので、一番大きい軸は `if` で選んでいる（CLAUDE.md の方針）。

#### B-4. `HTTEngine/Engine.cpp`（3か所）

(1) インクルード。`// Collision` の下に1行足す。

```cpp
// Collision
#include "HTTEngine/Collision/CollisionSystem.h"
#include "HTTEngine/Collision/Profiling/CollisionProfiler.h"
```

(2) `Initialize` の中、`LightSystem::Initialize();` の下に1行足す。

```cpp
	EntityManager::Initialize();
	LightSystem::Initialize(); // ライトのComponentをEntityManagerに登録するので、EntityManagerの後
	CollisionSystem::Initialize(); // ColliderをEntityManagerに登録する（保存・Inspectorも）ので、EntityManagerの後
	ComponentSerializer::RegisterEngineComponents(); // Transform・ModelRenderer・ライトの保存項目（Releaseでもシーンを読むのに要る）
```

(3) `Finalize` の中、`LightSystem::Release();` の下に1行足す。

```cpp
	instance_->windowManager_.Finalize();
	LightSystem::Release(); // シーン（ウィンドウ）の破棄より後にする
	CollisionSystem::Release(); // シーン（ウィンドウ）の破棄より後にする
	EntityManager::Release(); // シーン（ウィンドウ）の破棄より後にする
```

#### B-5. `HTTEngine/Window/WindowManager.cpp`（3か所）

(1) インクルード。`LightSystem.h` の下に1行足す。

```cpp
#include "HTTEngine/Particle/ParticleManager.h"
#include "HTTEngine/Light/LightSystem.h"
#include "HTTEngine/Collision/CollisionSystem.h"
#include "HTTEngine/Entity/EntityManager.h"
```

(2) `UpdateAll` の中、`EntityManager::FlushDestroy();` の下に1行足す。

```cpp
	EntityManager::UpdateTransforms();      // 更新順序2: ワールド行列（親→子）
	ParticleManager::Update();              // パーティクル更新
	LightSystem::Update();                 // ライト更新
	EntityManager::FlushDestroy();          // 更新順序6: 破棄予約の反映
	CollisionSystem::Update();              // 当たり判定（消すと決めたEntityを消した後。結果は次のフレームのSYSTEMが読む）
```

(3) `DrawAll` の一番下、`LightSystem::DrawGizmos(gizmoCamera);` の下に1行足す。

```cpp
	LightSystem::DrawGizmos(gizmoCamera);
	CollisionSystem::DrawGizmos(gizmoCamera); // Colliderの線（当たっている物は赤）
#endif
```

#### B-6. `HTTEngine/Editor/ImGuiManager.cpp`（2か所）

(1) インクルード。`LightGizmo.h` の下に1行足す。

```cpp
#include "HTTEngine/Light/LightGizmo.h"
#include "HTTEngine/Collision/CollisionSystem.h"
#include "HTTEngine/Diagnostics/LogManager.h"
```

(2) メニューバーの `View` の中、`Light Ranges` の下に1行足す。

```cpp
			ImGui::SeparatorText("Gizmos");
			ImGui::MenuItem("Light Icons", nullptr, &lightGizmo.showIcon);
			ImGui::MenuItem("Light Ranges", nullptr, &lightGizmo.showRange);
			ImGui::MenuItem("Colliders", nullptr, &CollisionSystem::GetShowGizmos()); // Colliderの線
```

#### B-7. `HTTEngine/Component/GameComponent.h`（2か所）

インクルードの先頭に1行、説明のコメントの一番下に2行足す（ゲームは今までどおりこのヘッダ1つで、`CollisionSystem::GetHits` と `ColliderComponent` が使える）。

```cpp
#include "HTTEngine/Collision/CollisionSystem.h"
#include "HTTEngine/Component/ComponentSerializer.h"
#include "HTTEngine/Component/ComponentUI.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Entity/EntityManager.h"
#include "HTTEngine/Entity/TransformComponent.h"

// ゲーム固有Componentを書くための道具。ゲーム側はこのヘッダ1つだけをインクルードする
//
//   1ファイル＝1Component。書くのは「① データ ② 見せ方 ③ 処理」の3つだけ
//
//   struct Spin { Vector3 speed = {0.0f, 90.0f, 0.0f}; };            // ① データ
//   COMPONENT(Spin, "Movement") { ui.Field("Speed", value.speed); }  // ② 見せ方
//   void SpinUpdate(Spin& spin, TransformComponent& transform, float deltaTime) { ... }  // ③ 処理
//   SYSTEM(SpinUpdate);
//
//   当たったかどうかは、③の中で CollisionSystem::GetHits(entity) を読む（Collider は Add Component → Physics で付ける）
```

---

### C. プロジェクトに追加・ビルド、確認用の Component

1. Visual Studio の `HTTEngine` プロジェクトに新しい3ファイルを追加する（「既存の項目を追加」。フィルタは `Collision`）。
   - `HTTEngine/Collision/ColliderComponent.h`・`CollisionSystem.h`・`CollisionSystem.cpp`
   - **足し忘れても、エンジンのビルドは通ってしまう**（静的ライブラリなので）。エラーが出るのはゲームのリンクで、`HTTEngine.lib(Engine.obj) : error LNK2019: ... CollisionSystem::Initialize` のように出る。そう出たら、ここを見直す。
2. エンジンをビルド（Debug と Release）。ゲーム側に配るヘッダは、ビルド後の xcopy がフォルダごと拾う。
3. ゲーム側に確認用の Component を1つ足す（`CG3_Project` の `Source\Components` に、Spin.cpp と同じように追加）。確かめたら消してよい。

```cpp
// CG3_Project/Source/Components/HitLog.cpp
// 確認用：当たり始めと離れたときに Log に出す（確かめたら消してよい）
#include <format>
#include <string>

#include <HTTEngine/Component/GameComponent.h>
#include <HTTEngine/Diagnostics/LogManager.h>

// 付けたEntityの当たりを Log に出す（中身の無い、印だけのComponent）
struct HitLog {};
COMPONENT(HitLog, "Test") {}

void HitLogUpdate(Handle<Entity> entity, HitLog&) {
	for (const CollisionHit& hit : CollisionSystem::GetHits(entity)) {
		if (hit.phase == HitPhase::Stay) {
			continue; // 当たっている間は毎フレーム来るので出さない
		}
		const Entity* self = EntityManager::Get(hit.self);
		const Entity* other = EntityManager::Get(hit.other); // 離れたときは、相手がもう消えていることがある
		const std::string otherName = other ? other->name : "(消えた)";
		const char* phase = (hit.phase == HitPhase::Enter) ? "当たり始め" : "離れた";
		LogManager::Log(std::format("{}: {} ← {}", phase, self->name, otherName));
	}
}
SYSTEM(HitLogUpdate);
```

4. ゲームをビルド（Debug と Release）。

---

## 4. 確認すること

**SRVスロット（パートA）**

1. 起動して、Profiler の **GPU → Stats** に **SRV Slots** のバーが出ている（起動直後は 27 前後 / 255）。
2. **Play → Stop を20回くらい繰り返す。** 落ちない。SRV Slots は、停止中はだいたい同じ数、再生中は **18 増える**（IBL の確認ウィンドウの分）を行き来するだけで、増え続けない（日本語の文字が初めて出たときなどに、ImGui の分が1つ増えることはある）。
3. Log の `RenderTextureCube created ... SRVSlot=` の番号が**増え続けない**（前は 28 → 50 → 54 → 76 → … → 236 と増え続けていた）。直した後は、1回目の Stop で起動時の IBL の番号（19〜22）が出て、その後は今までに使った番号の中で回る。
4. IBL の確認ウィンドウ（EnvCube / Irradiance / Prefilter / BRDF LUT）の絵が、Play / Stop を繰り返しても崩れない。MonsterBall（PBR）の映り込みも変わらない。
5. Serialize.md「4. 確認すること」の4（Play → Stop を10回繰り返しても Entity の数が増えない）を、もう一度通す。

**Collider（パートB）**

6. MonsterBall を選んで **Add Component → Physics → Collider**。Scene ビューに**緑の線の球**が出る。Radius を変えると線が変わる。**Shape を Box** にすると箱になり、欄が Size に変わる。
7. Hierarchy の＋で Entity を作り、Collider を付けて、Position を動かして MonsterBall の線に重ねる → **両方の線が赤**になる（停止中でも）。離すと緑に戻る。
8. MonsterBall の Scale を (1, 2, 1) にすると、球が2倍になる（一番大きい軸）。箱なら縦だけ伸びる。Rotation を変えても、箱は回らない。Center を (1, 0, 0) にしてから Rotation の Y を 90 にすると、中心が Entity の周りを回る。
9. **View → Gizmos → Colliders** を外すと線が消える。
10. 確認用の **HitLog**（Add Component → Gameplay → Test）を MonsterBall に付けて **Play** → 作った Entity の Position を Inspector で動かして重ねる → Log に「当たり始め: MonsterBall ← Entity」、離すと「離れた: MonsterBall ← Entity」。**重ねたまま Play を押す**と、Play の直後に「当たり始め」が出る（作り直すと当たり始めからやり直し）。
11. Play 中に、重なっている相手を Hierarchy で消す → 「離れた: MonsterBall ← (消えた)」。
12. Collider の Enabled を外す → 線が消え、当たらない（Play 中なら HitLog に「離れた」）。
13. Collider を付けて値を変え、Stop してから **Save** → 再起動しても残っている。`GameScene.scene.json` に `"Collider": { "enabled": true, "shape": "Sphere", "center": [...], "radius": 0.5 }` のように書かれている（Box なら `radius` の代わりに `size`）。
14. Collider の追加・Shape の切り替え・値の変更が **Ctrl+Z** で戻る。

**Release**

15. Release でビルドして起動 → 落ちない（線は出ない。判定は動いていて、SYSTEM から `GetHits` が読める）。

---

## 5. 検証したこと（2026-09-27、Claude）

- **コンパイル**：scratchpad にエンジン全体とゲームの写しを作り、このファイルのコードを入れて VS 18（v145）の cl で `/std:c++latest /W4 /utf-8`。エンジン84ファイル・ゲーム（`GameScene`・`Stage`・`Particles`・`main`・`Spin`＋`HitLog`＋「2. ゲームでの書き方」の例）を Debug（`USE_IMGUI`）/ Release で、**エラー0・新しい警告0**（元からある警告は変わらず）。
- **SRVスロットのテスト（本物のGPU）**：本物の `DirectXCommon` / `RenderTexture` / `RenderTextureCube` を RTX 4060 で動かし、ログと同じ流れ（起動で26個使う → Play のたびに IBL の組を作り直して面の確認ウィンドウの18個を作る → Stop でまた作り直す）を繰り返した。
  - **直す前のコード**：**9回目の Play で `[DirectXCommon.cpp::AllocateSRVSlot 342] AssertError: ... SRVDescriptorHeapのスロットが不足しています`**（ログと同じ行）で落ちた。
  - **直した後**：300往復しても落ちない。使っている数は起動時＋IBL の分から増えず、全部消すと起動時の数に戻る。返した番号は同じフレームでは配られず、`RecycleFreedSRVSlots` の後から配られる。キューブの面の6個も返る。`0` を返しても何も起きない。D3D12 のデバッグレイヤー（GPU側の検証つき、エラー・警告で止まる設定）を有効にした状態で、エラー・警告無し。
- **当たり判定の動作テスト 6176項目 全通過**：本物の `EntityManager` / `CollisionSystem` / `Collision::` / `ComponentSerializer` / `SceneSerializer` / `ComponentEditorRegistry` を使い、描画だけ差し替えた。
  - 球と球・箱と箱・球と箱（面・角・中に入っている）、境目ちょうど、Enter → Stay → Exit → また Enter。
  - 大きさ（一番大きい軸・負の大きさ・親の大きさ）、Center のずれに回転と大きさが掛かる、箱は回らない。
  - Enabled を外す・Entity を無効にする・親を無効にする・Collider を外す → 両方に Exit。消した相手 → 残った方にだけ Exit（相手は `Get` が `nullptr`）。
  - 全部作り直す（Play / Stop と同じ）→ 新しい Handle で Enter から、古い組の Exit は来ない。
  - 保存 → 読み込みで全項目が元どおり、形に合う大きさだけが書かれる（球なら `radius`、箱なら `size`）、警告無し、もう一度書くと同じ文字列。
  - 120個（球と箱が半分ずつ、途中で消して番号を使い回し）をばらまいて5フレーム：当たりの数が総当たりで数え直した数と一致、どの当たりにも相手側の1件がある、`GetHits` は自分の分だけを全部返す。
  - ギズモ：球は円3つ（72本）、箱は12本（どれも軸に平行で長さが Size どおり）、当たっている物は赤、無効な Collider は描かない、View で切ると描かない。
- **確かめていないこと**：画面の見た目（線の色・太さ、Inspector の欄の並び、View メニュー）、本物のエディタでのマウス操作、Profiler のバーの見え方。

---

## 6. 決めたこと

| 決めたこと | 理由 |
|---|---|
| SRVスロットは持ち主のデストラクタで返す | 返し忘れが起きない。S1 で「作り直す」ようになったので、返さないと Play / Stop のたびに減る |
| 返した番号は、フレームの終わり（`WaitForGPU` の後）から配り直す | 同じフレームのコマンドがまだその番号を読むかもしれない（「1. 直し方」の解説） |
| テクスチャ（`TextureManager`）の番号は返さない | 読み込んだ物をアプリの終わりまで持つ作りで、同じパスなら同じ番号を返すので増えない。テクスチャを捨てる機能を作るときに一緒に |
| Profiler に SRV Slots を出す | 256個しかない物なので、減り続けていないかを目で見られるようにする |
| Collider は1つの型で、形は enum（`Sphere` / `Box`） | Inspector で形を切り替えられる。`SphereCollider` / `BoxCollider` に分けると、形を変えるたびに付け直しになる |
| 1つの Entity に Collider は1つ | 同じ型の Component は1つまで（ComponentStorage の決まり）。形を組み合わせたいときは、子の Entity に付ける |
| 箱は回らない（AABB） | エンジンにある判定（`Collision::AABBAABB` / `SphereAABB`）で足りる。回る箱（OBB）は判定を足す必要があるので後で |
| 押し戻しはしない。`isTrigger` も無い | 重なりを知らせるだけなら、全部がトリガーと同じ。押し戻しを作るときに、`isTrigger` と一緒に足す |
| 判定は `FlushDestroy` の後（Serialize.md では「`UpdateTransforms` の直後」と書いていたのを変えた） | 消すと決めた Entity はもう当たらない。弾が当たって消えるフレームに、もう一度当たることが無い |
| 結果は「当たりの一覧」で渡し、ゲームは SYSTEM で読む（呼び出し＝コールバックにしない） | SYSTEM は「データを読んで書くだけ」にそろえる（ARCHITECTURE.md のデータと処理の分離）。コールバックだと、呼ばれる順番や、中で `Destroy` したときの扱いが難しくなる |
| 当たり始め・当たっている・離れた（Enter / Stay / Exit）を分ける | 弾は1回だけ数える（Enter）、床のダメージは乗っている間（Stay）、範囲から出た（Exit）、が書き分けられる |
| 消えた Entity には何も届けず、残った相手に Exit を届ける | 消えた方の `GetHits` を読む人はいない。残った方は「範囲の中の数」を数えているかもしれないので、離れたことを知る必要がある |
| 停止中も判定する | 線の色で「重なっているか」が分かり、置き場所を合わせられる。ゲームの SYSTEM は停止中は動かないので、結果を読む人はいない |
| 全部の組を調べる（総当たり） | 作りが一番簡単で、数百個までならこれで足りる見込み（n個で n×(n−1)/2 回。300個なら約4.5万回）。Debug では `Collision::` の関数が1回ずつ時間を測っているので Release より重い。重くなったら空間分割（格子）を足す |
| 見せ方（`DescribeCollider`）1つで Inspector と保存をまかなう | 項目の一覧が1か所なので、足し忘れが無い。エンジンの型でもこの書き方でよいことの最初の例 |
| 形に合う大きさだけを見せる・保存する | 球なのに Size が出ていると、どれが効いているか分からない。見せ方の関数はふつうの C++ なので `if` で分けられる |
| 一覧などの中身は `.cpp` に置く | このヘッダはゲームの Component のファイルが全部読むので、軽くしておく |
| 中心の変換は `TransformPoint` を自分で書く | エンジンの `TransformNormal` は名前と違って位置も足す。名前どおりに使うと間違える |

---

## 7. 未解決・後回し

| 何を | なぜ後か / どうするか |
|---|---|
| 押し戻し | 当たったら重ならない所まで戻す。重さ（動く物・動かない物）の区別と `isTrigger` が要る。小さなゲーム（S5）で必要になったら |
| 回る箱（OBB）・カプセル | `Collision::` に OBB どうし・球と OBB の判定を足せば、`ColliderShape` に足すだけ |
| レイヤー（何と何が当たるか） | 今は全部が全部と当たり、ゲームが相手の Component で見分ける。弾どうしが大量に当たって重くなったら、ビットの組み合わせで足す |
| 空間分割 | 数百個を超えて重くなったら。Profiler の Collision の呼び出し回数で分かる |
| シーンに向けたレイ（`CollisionSystem::Raycast`） | クリックで Entity を選ぶ・視線の先を調べる、が要るときに。一覧の形（`colliders`）はもうあるので、`Collision::RaySphere` / `RayAABB` で回すだけ |
| 選んだ Entity の Collider だけ線を出す | 今は全部出す。数が増えて見づらくなったら |
| `TransformNormal`（`Matrix4x4.h`）の名前 | 中で位置も足している（点の変換）。直すなら `TransformPoint` に名前を変えて、位置を足さない本当の `TransformNormal` を足す。今はどこからも使われていない |
| `ui.Field` の `int` に `AtLeast` を付けると上限が壊れる | `ComponentUI.cpp` の `Field(int)` が `static_cast<int>(FLT_MAX)` をしている（範囲外の変換）。`int` の上限に丸めれば直る。今は `int` に `AtLeast` を付けない |
| IBL を Play / Stop のたびに焼き直している | 作り直しのたびに少し待つ。同じ HDR なら焼いた物を使い回せる（IBL をシーンの設定として保存するとき＝Serialize.md「7. 未解決」と一緒に） |
| `GameScene` の IBL の確認ウィンドウ | Play 中にずっと4つ出ている。要らなければ `GameScene::Update` から消してよい（消しても番号は返るので、もう落ちない） |
