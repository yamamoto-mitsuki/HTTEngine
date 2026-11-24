> HTTEngine リファレンスの1章。ほかの章は [索引](../Reference.md) から探す。

# 25. 攻撃の当たりとダメージ・攻撃表の AI

**Collider（[14章](14-MathCollision.md)）とは別の仕組み。** Collider は「ぶつかる・重なる」、こちらは「攻撃が当たって HP が減る」。どちらも Add Component → **Combat** の下。

| Component | 付ける物 | 中身 |
|---|---|---|
| **Hitbox** | 攻撃（剣・弾・床の範囲攻撃） | 当たる形・当てる相手のレイヤー・窓（いつ当たるか）・何回当たるか・ジャスト判定 |
| **Hurtbox** | 当たられる所（体・弱点） | 球かカプセル。Damage Multiplier（弱点は 1 より大きく） |
| **Health** | 受ける本体 | Max Health・Health・Invincible・Invincible After Hit・Immune Types |
| **Damage Source** | 攻撃（Hitbox と同じ Entity） | Damage・Damage Type・距離で変わる倍率（Falloff Curve・Near・Far）・Hit Stop・Shake |
| **Hitbox Telegraph** | 攻撃（Hitbox と同じ Entity） | 予告の間、床に攻撃の形を塗る（下の節） |

- **受ける本体**は、当たった Hurtbox から見て Health の付いた一番近い親か自分。体の部位ごとに子の Hurtbox を置き、親に Health を1つ付ける。
- 当てるかどうかはレイヤーで決める（Hitbox の Targets に、相手の **Hurtbox の Entity のレイヤー**が入っていれば当たる）。レイヤーの名前は Settings の Layers。撃った本人（Instigator。空なら Hitbox の Entity）とその親子には当たらない。
- 当たりを調べるのは全部が動いた後（[1フレームの順番](01-Overview.md#1フレームの順番) の 9）。消すと決めた Entity にはもう当たらない。

## 攻撃の形（Hitbox の Shape）

| Type | 形 |
|---|---|
| `Circle` | 円（Inner Radius でドーナツ） |
| `Sector` | 扇（前 +Z を真ん中に Angle 度。Inner Radius で扇のドーナツ） |
| `Line` | 線（幅 Width・前 +Z へ Length の長方形） |
| `Sphere` | 球（空中の攻撃・弾） |
| `Capsule` | カプセル（前 +Z へ Length の線分を Radius で太らせた形。剣を振る・太いレーザー） |

- 円・扇・線は「床に置く形」（ローカルの XZ に描いた形を、底から上へ Height だけ伸ばした柱）。床の予告を描けるのはこの3つだけ。
- Entity の大きさが掛かる（円・扇の半径は X と Z の大きい方、線の幅は X、長さは Z、高さは Y、球は一番大きい軸）。Entity を回すと形も回る。
- View → **Hit Shapes** で Scene ビューに出る（段階で色が変わる。ジャスト判定の範囲・Hurtbox も）。

## 窓（いつ当たるか）

Hitbox の Phase は `Off`（当たらない）→ `Warning`（予告。当たらない）→ `Active`（当たる）。**Active になるたびに新しい攻撃の回**になる（当てた相手の記録が空になる）。保存した Phase が、Play したとき・Prefab から作ったときの段階。

```cpp
HitSystem::Warn(hitbox, 0.8f, 0.2f); // 0.8 秒予告して、0.2 秒当たって、閉じる
HitSystem::Activate(hitbox, 0.1f);   // すぐ 0.1 秒当たる（0 ならずっと）
HitSystem::Deactivate(hitbox);       // 閉じる
```

- どれも呼んだフレームの当たりから効く。Script・SYSTEM・攻撃部品・Timeline から呼んでよい。
- Rehit Interval が 0 なら1回の攻撃で同じ相手に1回だけ、正なら多段ヒット。Max Targets（0 なら何体でも）まで当てたら Off になり、`IsSpent()` が true（弾を消す合図）。

## 当たったことを受け取る

**Script なら関数が呼ばれる**（当たった次のフレームの Update の前。Collider の当たりと同じ）。

```cpp
void Enemy::OnHitTaken(const HitInfo& hit) {   // 受けた本体（Health の Entity）に来る
	SpawnDamageNumber(hit.point, hit.damage);  // point：当たった所、damage：減った量
	if (hit.isDefeated) { Destroy(gameObject); }
}
void Player::OnHitDealt(const HitInfo& hit) { … }      // 自分の攻撃が当たった（Hitbox の Entity と、撃った本人に来る）
void Player::OnBeforeHitTaken(HitInfo& hit) {          // HP を減らす前に、その場で呼ばれる
	if (isGuarding_) { hit.result = HitResult::Guarded; hit.damage *= 0.2f; } // 防ぐ（Ignored なら断る）・量を書き換える
}
```

- `HitInfo` の中身：`hitbox`・`instigator`・`receiver`・`hurtbox`・`point`・`direction`（攻撃の向き）・`distance`・`damageType`・`damage`・`result`・`isDefeated`・`healthAfter`。
- `HitResult`：`Hit`（HP が減る・ヒットストップと揺れが出る）／`Guarded`（記録はする＝同じ攻撃の回ではもう当たらない。HP は damage だけ減る。ヒットストップと揺れは出ない）／`Ignored`（断る。記録しないので、同じ回のうちに後でもう一度当たることがある）。
- SYSTEM からは `HitSystem::GetAllHits()`・`GetHitsDealt(entity, out)`・`GetHitsTaken(receiver, out)`（最後の当たりの結果。次の当たりの計算まで変わらない）。
- **ダメージ** ＝ Damage × Hurtbox の Damage Multiplier × 距離の倍率。Health が 0 以下になると倒れている（`IsDefeated()`。もう当たらない）。HP の割合は `GetRatio()`。
- 無敵のように「いつも断る」物は、Health の **Invincible** に書く（Script の無い物にも効き、Inspector で見える）。被弾の後の無敵は Invincible After Hit（秒）。効かない攻撃の種類は Immune Types（種類の名前は Settings の Damage Types）。

## ジャスト判定（よける・はじく）

Hitbox の **Just Enabled** が入っていれば、形を Just Margin（m）だけ太らせた範囲が「ジャストの範囲」になる。有効なのは Active になる Just Lead Time 秒前から Active の間。

```cpp
// プレイヤーの Script の Update（よけるボタンを押した瞬間）
JustCandidate just;
if (HitSystem::TryConsumeJust(gameObject.handle, just)) {
	// ジャスト成功。その攻撃の回では、もう当たらない・もうジャストにならない
}
```

- 見るだけ（記録しない）なら `HitSystem::FindJust`。位置は前のフレームの終わりの物。

## 床の予告（Hitbox Telegraph）

Hitbox と同じ Entity に付けると、**Warning の間、床・壁に攻撃の形を塗る**（Show While Active で Active の間も）。円・扇・線の形だけ。Material を選ばなければエンジンの既定（赤い塗り。`HTTEngine/Resources/Materials/HitboxTelegraph.material`）。中身はデカール（[8章](08-Rendering.md#箱の中の床壁に塗るdecal-renderer)）と同じ仕組みで、Order で重なりの順を決める。

## 攻撃表の AI（Attack Table）

ボスのような「攻撃を選んで、ステップ（溜め → 走る → 掴む など）を順に進める」敵を、表のファイルで作る。**Add Component → Gameplay → Attack Table**（`AttackTableComponent`）に、表のファイル（`.attacktable`。Project の Create → Attack Table）と Target（狙う相手）を選ぶ。

- 表の中身は、Project でファイルを選ぶと Inspector で直せる。同じ表を選んだ敵は全部同じ表を見る。
  - **フェーズ**：HP の割合（Health Threshold）で切り替わる段階。移る長さ（Transition Seconds）・攻撃を選ぶまでの最短の秒（Min Idle Seconds）・待ちの部品（Idle Parts。攻撃を選ぶまでの間動く）。
  - **攻撃1行**：名前・抽選の重み・使う距離（Min / Max Distance）・クールタイム・使えるフェーズ・**Steps**・**Parts**。
  - **Steps**：上から順に進める。1つずつ名前・Stage（Windup / Active / Recovery / None の目印）・Seconds（長さの上限）・Next（終わったときの行き先）・On Fail（部品が失敗したときの行き先）。行き先は「1つ下」「攻撃を終える」「名前で選んだステップ」。
  - **Parts**：攻撃部品の並び。1つずつ **Runs In** で動くステップを選ぶ（Whole Attack なら攻撃の始めから終わりまで）。同じステップの部品は上から順に呼ぶ。
  - Max Repeat（同じ攻撃を続けてよい回数）。部品の Clip の欄の候補は、この表を使う敵（Animator を持つ物）を開いているシーンに置くと、その Animator のファイルから出る（居なければ名前を打つ）。
- エンジンの部品：`HitboxWindow`（そのステップの間だけ子の Hitbox を予告・当たりにする）・`PlayAnimation`（ステップに入ったらアニメを流す。ステップの長さに合わせられる。Override Blend Time をオンにすると、前のステップのアニメから混ぜる秒をステップごとに決められる。オフなら Animator の Blend Time）。
- 状態は `Selecting`（選んでいる）→ `Attacking`（ステップを進める）→ `Selecting`。ほかに `Staggered`（怯み）・`PhaseTransition`・`Defeated`。同じ Entity に Health があれば、HP の割合はそこから読む。
- **Attack Table ウィンドウ**に、選んだ敵の今の状態・ステップ・攻撃ごとの使えるか・選ばれる確率が出る。
- 時間は `Time::GetDeltaTime()` で進むので、ポーズ・ヒットストップの間は止まる。

```cpp
AttackTableSystem::Stagger(boss, 1.5f);           // 怯ませる（今の攻撃を止める）
AttackTableSystem::RequestAttack(boss, "Sweep");  // 次にこの攻撃を出させる
AttackTableSystem::SetEnabled(boss, false);       // AI を止める
const AttackTableStatus status = AttackTableSystem::GetStatus(boss); // 今の状態・攻撃とステップの名前・残りの秒
```

### 攻撃の中身を書く（攻撃部品 `AttackPart`）

部品は「1つのステップの1つの事」を書く。ステップの間だけ動かすなら、表で Runs In にそのステップを選ぶ。

```cpp
#include "HTTEngine/AI/AttackPart.h"

class DashToTarget : public AttackPart {
public:
	void OnBegin(const AttackPartContext& context) override { … } // Runs In のステップに入った
	void OnUpdate(const AttackPartContext& context) override {     // 毎フレーム
		if (!context.target) {
			FailStep();   // On Fail の行き先へ
			return;
		}
		if (/* 着いた */) {
			FinishStep(); // Seconds を待たずに Next の行き先へ
		}
	}
	void OnEnd(const AttackPartContext&, AttackPartEnd) override { … } // ステップが終わった（途中で止められても必ず呼ばれる）
	void Fields(ComponentUI& ui) override { ui.Field("Speed", speed_); } // 置いた所ごとの値（表に保存される）
private:
	float speed_ = 10.0f;
};
ATTACK_PART(DashToTarget, "相手へ走る。着いたらステップを終える"); // 攻撃表の Part の一覧に出る（説明は省略できる）
```

- `context` の時間：`stepTime`（今のステップに入ってからの秒。ステップが変わると 0）・`stepDuration`（今のステップの Seconds。上限なので、FinishStep で早く終わることがある）・`attackTime`（攻撃が始まってからの秒）。`GetStepProgress()` で 0〜1。
- `FinishStep()`・`FailStep()` が効くのは、そのフレームの部品を全部呼んだ後。同じフレームに両方あれば失敗が勝つ。OnBegin で呼べば、次の OnUpdate の後に効く。
- Runs In が Whole Attack の部品には、ステップに入るたびに `OnStep` が来る（`context.step`・`context.stage` で分ける）。ステップをまたぐ物（向き・片付け）に使う。
- `SyncHitbox(hitbox, context)` を OnStep で呼ぶと、Hitbox の窓を Stage に合わせる（Windup → Warning、Active → Active、それ以外 → Off）。
- 実体は置いた所1つにつき1つ（敵1体ごと）できるので、メンバに途中の値を持ってよい。前に使ったときの値が残るので、OnBegin で初めに戻す。
- 部品の中から `AttackTableSystem::Stagger` を呼んでもよい。怯むのは、部品の呼び出しが全部終わってから。
- 表はシーンの外のファイルなので、`Fields` に Entity への参照（`EntityRef`・`GameObject`）は書けない（Prefab は `AssetField` で書ける）。Entity は `context.self`・`context.target` から探す。
- 呼ばれるのは時間が進んでいるときだけ（ポーズ・ヒットストップ・AI 停止の間は呼ばれない）。

```cpp
// ✗ だめな例
void OnBegin(const AttackPartContext& context) override {
	HitSystem::Activate(hitbox_.handle); // ✗ 閉じるのを忘れると、次のステップ・怯みの間もずっと当たる → OnEnd で閉じる（HitboxWindow を置けばコードが要らない）
}
```

```
// ✗ だめな置き方：「ステップに入ったときに調べる」部品（掴めるか など）を Whole Attack に置く
//   → 攻撃の頭で調べて FailStep し、最初のステップで攻撃が終わる。調べたいステップを Runs In に選ぶ
```
