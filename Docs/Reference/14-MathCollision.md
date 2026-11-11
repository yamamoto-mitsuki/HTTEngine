> HTTEngine リファレンスの1章。ほかの章は [索引](../Reference.md) から探す。

# 14. 数学と当たり判定

```cpp
#include <HTTEngine/Math/MathIncludes.h>

const float r = RandomEngine::GetFloat(0.0f, 1.0f);
const int n = RandomEngine::GetInt(0, 10);
const Vector3 v = RandomEngine::GetInsideUnitSphere();

const Vector3 a = Normalize(b - c);
const float len = Length(v);
const float t = MathUtility::Clamp(x, 0.0f, 1.0f);
const Vector3 p = MathUtility::Lerp(from, to, 0.5f);
velocity.x = MathUtility::MoveTowards(velocity.x, target, accel * deltaTime); // target へ最大 accel*deltaTime だけ近づける（行き過ぎない）
```

当たり判定は形どうしの関数。**`Collision::` を付けて呼ぶ**（`#include <HTTEngine/Collision/CollisionIncludes.h>`）。

```cpp
const Sphere sphere{GetWorldPosition(*transform), 1.0f};   // 中心, 半径
const AABB box{{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}}; // 最小点, 最大点
if (Collision::SphereAABB(sphere, box)) { … }

const Ray ray{origin, direction}; // 始点, 向き
float t = 0.0f;
if (Collision::RaySphere(ray, sphere, &t)) { … } // 当たった距離が t に入る
```

`SphereSphere` / `AABBAABB` / `SphereAABB` / `CapsuleSphere` / `CapsuleAABB` / `RaySphere` / `RayAABB` / `RayPlane` / `RayCapsule` / `RayPolygon` / `IsPointInSphere` / `IsPointInAABB` / `ClosestPointOnAABB` / `ClosestPointOnSegment` / `ClosestPointsBetweenSegments` / `DeepestPointOnSegmentInAABB` / `RayPoint`。

押し出す量が要るときは `PushOut〜`（`PushOutSphereSphere`・`PushOutSphereAABB`・`PushOutAABBAABB`・`PushOutSpherePolygon`・`PushOutCapsulePolygon` など）。`a` に `*push` を足すと、ちょうど触れた所になる。最後の `AxisLock{x, y, z}` で、その軸へは押さない（横から見るゲームで Z へ押し出されないように）。

`Capsule{start, end, radius}` は「線分 start-end を半径で太らせた形」（球が start から end まで動いた跡・カプセルの Collider）。`ClosestPointOnSegment(point, a, b)` は線分 a-b の上で point に一番近い点（線に触れているか＝その点との距離が半径以内か）。`ClosestPointsBetweenSegments` は2本の線分のお互いに一番近い点、`DeepestPointOnSegmentInAABB` は線分の上で箱に一番深く入っている点（外なら一番近い点）。

> **Rigidbody を付けた物だけ**、PhysicsSystem が重力で動かし、Collider から押し戻す（Add Component → Physics → Rigidbody。`velocity` を書いて動かす。`isGrounded` で乗っているかが分かる）。歩くキャラは **Character Controller**（Add Component → Physics → Character Controller。CharacterControllerSystem が坂・段・壁に沿って動かす。`velocity` の X・Z を書き、`IsGrounded()` で地面にいるかが分かる）でも押し戻される。どちらも無く Collider だけなら押し戻さない。
> Rigidbody 同士は **Mass の逆の割合**で押し合う（軽い方がたくさん動き、ぶつかった後は同じ速さになる。跳ね返らない）。**Collide With Rigidbodies** を外した方は、ほかの Rigidbody とすり抜ける（壁・床にはぶつかる。当たりの知らせ `OnCollision〜` は来る。当たると吹き飛ぶプレイヤーなど。相手の図形の動きは変わらない）。

## Collider（当たり判定の Component）

Inspector の **Add Component → Physics → Collider** で付ける（コードなら `EntityManager::RequestAdd<ColliderComponent>(entity, collider)`）。形は球（Radius）・箱（Size）・カプセル（Radius・Height）・多角形（Polygon：同じ Entity の Polygon Shape の点。[8. 描画](08-Rendering.md#横から見た形の足場polygon-shapepolygon-renderer)）で、Entity の Scale（親の分も）が掛かる。エディタの Scene / Game ビューに緑の線（Is Trigger は水色）で出て（Release では出ない。View → Colliders で消せる）、重なると赤になる。**Collider は押し戻しはしない**（重なったことを知らせるだけ。押し戻すのは上の囲みの Rigidbody・Character Controller）。

当たったかどうかは、SYSTEM の中で **`CollisionSystem::GetHits(entity)`** を読む（`GameComponent.h` だけで使える）。

```cpp
#include <HTTEngine/ECS/GameComponent.h>

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

| `hit.phase` | いつ来るか | 使いどころ |
|---|---|---|
| `HitPhase::Enter` | 重なり始めたフレームに1回 | 弾が当たった・アイテムを取った |
| `HitPhase::Stay` | 重なっている間、毎フレーム | 床のダメージ・範囲の中にいる間 |
| `HitPhase::Exit` | 離れたフレームに1回。相手が消えたときも来る（そのとき `EntityManager::Get(hit.other)` は `nullptr`） | 範囲から出た |

- `hit.self` は自分、`hit.other` が相手。1組の当たりは、両方の Entity に1件ずつ届く。
- `hit.normal` は**相手から自分へ押し出す向き**（長さ1。ぶつかった面の向き）。相手の上に乗っていれば、だいたい (0, 1, 0)。吹き飛ばす向き・踏んだか（`normal.y > 0.7` など）に使う。Exit は離れる前のフレームの向き。
- 相手が何者かは、**相手が持っている Component で見分ける**（タグは無い）。Entity のレイヤー（`EntityManager::Get(hit.other)->layer`。名前は Settings の Layers）でも見分けられるが、Collider の当たり自体はレイヤーで絞らない（全部の組が届く）。
- 読めるのは**前のフレームの判定結果**（1章の順番の 9）。停止中も判定しているので、線の色で重なりが分かる。
- 1つの Entity に Collider は1つ。形を組み合わせたいときは、子の Entity に付ける。
- 箱は Entity を回しても回らない（軸にそろった箱＝AABB）。Center のずれは回る。
- カプセルもいつも立ったまま（Y の向き）。Height は下の端から上の端まで（半球も入れる）で、半径の2倍より小さいと球になる。プレイヤー向け（箱より段差の角で引っかからず、球より縦長にできる）。
- 多角形（Polygon）は、横から見た形を Z へ押し出した形。横の面・前の面・後ろの面の一番近い所から押し出す（Freeze Position Z の物は X・Y だけ）。球・カプセルとだけ当たり、箱・多角形どうしは当たらない。坂の上で止まりたいプレイヤーは、`hit.normal` から坂の向きを出して、坂に沿った速さにする。

## だめな例：`Enter` を見ずに数える

```cpp
// だめ：重なっている間（Stay）も毎フレーム来るので、HP がみるみる減る
for (const CollisionHit& hit : CollisionSystem::GetHits(entity)) {
	if (EntityManager::Get<Bullet>(hit.other)) {
		--enemy.hp;
	}
}
```

→ `if (hit.phase != HitPhase::Enter) { continue; }` を先に書く。

## Script で当たりを受け取る（OnCollision〜・OnTrigger〜）

Script なら、`GetHits` を読まずに、エンジンが関数を呼んでくれる（Unity と同じ名前）。どちらかが Is Trigger なら `OnTrigger〜(GameObject other)`、どちらも違えば `OnCollision〜(const CollisionInfo& collision)`。

```cpp
// 図形（動く Rigidbody）にぶつかったら、ぶつかった面の向きへ飛ばされる
void PlayerController::OnCollisionEnter(const CollisionInfo& collision) {
	if (collision.gameObject.GetComponent<RigidbodyComponent>() == nullptr) {
		return; // 床・壁（Rigidbody が無い）
	}
	if (RigidbodyComponent* rigidbody = GetComponent<RigidbodyComponent>()) {
		rigidbody->velocity = collision.normal * 8.0f; // normal は相手から自分へ（長さ1）
	}
}
```

- `collision.gameObject` が相手、`collision.normal` が `hit.normal` と同じ向き。
- Enter・Stay・Exit の分け方は `hit.phase` と同じ（Stay は触れている間ずっと。床に乗っている間も来る）。
- `collision.selfVelocity`・`collision.otherVelocity`：ぶつかる直前の自分・相手の速さ（物理が押し戻す前。跳ね返って一瞬で離れた組も Enter が来る）。「どちらがぶつかってきたか」は `相手の重さ * Dot(collision.otherVelocity, collision.normal)`（相手の勢い）、ぶつかった強さは `collision.GetImpactSpeed()`。
- 跳ね返り・摩擦は Collider の **Physic Material**（`.physmat`。Project の Create → Physic Material で作り、Project で選ぶと Inspector で直せる）。2つの値は混ぜ方（Average・Minimum・Multiply・Maximum）で1つにする。1m/s より遅いと跳ねない。

## その場で調べる（Raycast・SweepSphere）

`GetHits` は「フレームの最後に1回作った重なりの一覧」。こちらは **SYSTEM の中で何回でも、その場で**聞ける（`GameComponent.h` だけで使える）。

```cpp
// 相手は true を返した物だけにする（ここでは「持っている Component」で絞る。レイヤーで絞る引数は無いので、使うならこの中で見る）。空なら全部
bool IsWall(Handle<Entity> entity) { return EntityManager::Get<Wall>(entity) != nullptr; }

// レイを飛ばして、一番近い Collider（壁で線を止める）
RaycastHit hit;
if (CollisionSystem::Raycast({from, direction}, maxDistance, hit, IsWall)) {
	// hit.entity・hit.point（当たった位置）・hit.distance（始点からの距離）
}

// 球を start から end まで動かした跡に触れた Collider を全部（速く動く攻撃。1フレームで飛び越えてもすり抜けない）
std::vector<Handle<Entity>> touched;
CollisionSystem::SweepSphere({start, end, 0.5f}, touched, IsEnemy); // {始点, 終点, 半径}
for (const Handle<Entity> enemy : touched) {
	EntityManager::Destroy(enemy);
}
```

- 調べる相手の形は、**前のフレームに集めた物**（1章の順番の 9。ギズモの線と同じ）。動かない壁なら気にしなくてよい。
- **Play・シーン切り替えの直後の1フレームは、何にも当たらない**（作り直す前の形しか無い）。
- レイの向きは長さ1でなくてよい。始点が Collider の中なら距離0で当たる。多角形（Polygon）にも当たる（前の面・後ろの面・横の面）。
- 動いた跡（カプセル）は、Collider と同じく「線分の上で相手に一番近い点の球」にして調べる（箱の角でも正確）。
- 速く動く物の攻撃を Collider（`GetHits`）でやると、**1フレームで相手を飛び越えてすり抜ける**。動いた跡（`SweepSphere`）で調べる。

## 弾む面（Elastic Surface）

**Add Component → Physics → Elastic Surface**（`ElasticSurfaceComponent`）。トランポリンのように、乗った物を沈めて跳ね返す板。面の真ん中は付けた Entity の位置で、横（X）に Width・奥行き（Z）に Depth だけ広がる（回転・大きさは使わない）。厚み（Thickness）と両端の支柱（Post Height）は見た目だけで、当たるのは上の面。

- **乗れるのは Rigidbody と Collider（Is Trigger でない物）を付けた Entity**。球の Collider は面に一番近い点、箱の Collider は底に並べた点、カプセルの Collider は下の端の球で押し返される。坂の上の物は、低い方（くぼみ）へ押される。
- Rigidbody の **Mass**（kg）が重いほど深く沈め、周りの物を大きく跳ね上げる。Mass は Rigidbody 同士の押し合いにも使う（14章の上の囲み）。
- **Edge**（Edge Stiffness・Edge Damping・Edge Falloff）で、端ほど沈みにくく・跳ね返りを弱くできる（初期値は Edge Stiffness 0.3・Edge Damping 0・Edge Falloff 2。Stiffness と Damping を両方 0 にすると無し）。
- 面の計算は、PhysicsSystem の1歩をさらに細かく分けた小さな1歩（Elastic Surface の **Steps Per Second**。初期値 1200＝1/1200 秒）で、Rigidbody と一緒に進む。Tension を上げて暴れたら増やす。再生中だけ動き、停止中はいつも平ら。
- 見た目は **Surface Material**（板）と **Posts**（支柱）に、Primitive Renderer と同じ Material の項目がある（テクスチャ・Shading・光沢・金属感）。テクスチャは **m 単位**で貼る（UV Scale 2 なら 2m 四方に1枚。上の面・壁・支柱のどこでも同じ大きさ）。

```cpp
// ゲームの Script・SYSTEM から（GameComponent.h だけで使える）
const Handle<Entity> floor = ElasticSurfaceSystem::FindSurface(x);       // x の上下にある面（無ければ無効な Handle）
const float floorY = ElasticSurfaceSystem::GetHeight(floor, x);          // その位置の面の高さ（ワールドの Y）
const float floorVelocity = ElasticSurfaceSystem::GetVelocity(floor, x); // 面の上下の速さ（戻る勢いに合わせて跳ぶ）
ElasticSurfaceSystem::AddImpulse(floor, x, -20.0f, 2.0f);                // 一瞬で叩く（N・s。下向きは負）
```

- `AddImpulse` で面の真ん中が変わる速さは、だいたい **力積 ÷（Mass Per Meter × 半径）**（Mass Per Meter 1・半径 2m で −20 なら約 −10m/s）。大きすぎると面が乗っている物より先に沈み、戻るときに追い越して、物が**面を突き抜ける**。

## 風（Wind Zone）

**Hierarchy の ＋ → 3D Object → Wind Zone**、または **Add Component → Physics → Wind Zone**（`WindZoneComponent`）。範囲の中の Rigidbody を、風の向きへ押す。

| 項目 | 中身 |
|---|---|
| 向き | Entity の右（ローカルの +X）へ吹く。Rotation の Z で回す（90 で上へ） |
| Size | 範囲の大きさ（m。Entity から見た箱。Scale も掛かる。Entity と一緒に回る） |
| Speed | 風の速さ（m/s）。物はこの速さまで流される。それより速い物は、それ以上押さない（止めもしない） |
| Strength | 風の速さまで速くする強さ（m/s²）。上向きの風で浮かせるなら、重力（Settings の Physics → World → Gravity × Rigidbody の Gravity Scale）より大きくする |
| Gust・Gust Frequency | ゆれ（0.3 なら速さが ±30%）と、ゆれる速さ（1秒に何回くらい） |
| Line Amount | 画面の風の線の濃さ（0〜1）。**カメラが追っている物（Camera Follow・Camera Orbit の Target）が範囲の中にいる間だけ**、入ってから 0.3 秒で出て、範囲から出たら 0.5 秒で消える。Game ビューだけ（Lens のパス） |

- 範囲と向きの線は、Collider のギズモと一緒に出る（Scene ビュー）。
- 押されるのは **Rigidbody の Wind Scale**（初期値 1）の倍率。0 なら押さない。Script で速さを毎フレーム目標へ近づけている物（プレイヤーなど）は、押した分がすぐ消されるので、Wind Scale を 0 にして、自分で風を読む：

```cpp
#include <HTTEngine/Systems/Physics/WindZoneSystem.h>

// その位置の風（重なった範囲は足す。範囲の外なら全部 0）
const WindZoneSystem::WindSample wind = WindZoneSystem::Sample(GetWorldPosition(transform));
if (wind.IsBlowing()) {
	// wind.velocity：風の速さ（向き × 速さ。ゆれ込み）、wind.strength：近づける強さ（m/s²）
	targetSpeed += wind.velocity.x; // 例：走る目標の速さに足す（追い風で速く・向かい風で遅く）
}
// エンジンと同じ押し方（風の向きの速さが、風の速さより遅い間だけ速くする）
rigidbody->velocity = WindZoneSystem::Push(wind, rigidbody->velocity, Time::GetDeltaTime());
```
- Sample は前のフレームの終わりの範囲を使う（Script の Update は WindZoneSystem::Update より前）。

## 範囲に入ったら出す（Trigger Activator・Alpha Group）

**Hierarchy の ＋ → 3D Object → Trigger Activator**（Is Trigger の箱の Collider ＋ `TriggerActivatorComponent`）、または **Add Component → Gameplay → Trigger Activator**。同じ Entity の Collider の範囲に入ったら・出たら、**Target**（別の Entity）を出す（有効にする）・消す（無効にする）。チュートリアルの説明・看板に。Script は要らない。

| 項目 | 中身 |
|---|---|
| Target | 出す・消す Entity（文字と背景をまとめた親を入れると、子も一緒）。自分と自分の親は選ばない |
| Source | 誰が入ったら反応するか。`FollowTarget`（メインカメラの Camera Follow・Camera Orbit の Target＝プレイヤー）・`Entity`（Source Entity。子の Collider も数える）・`AnyRigidbody`（Rigidbody を付けた物）・`AnyMover`（Rigidbody か Character Controller を付けた物） |
| On Enter・On Exit | 入ったとき・出たとき。`Show`（出す）・`Hide`（消す）・`None` |
| Once | 入ったとき・出たときの動きを、それぞれ1回だけ |
| Hide On Start | 再生を始めたら、すぐ Target を消す（初期値オン。エディタでは見えたまま置ける） |
| Fade In Time・Fade Out Time | 見えきる・消えきるまでの秒数（0 ならすぐ）。消えきったら無効にする |

- 動くのは再生中だけ。Stop で、Play を押した瞬間の状態に戻る。入った・出たは、毎フレーム「Enter・Stay の当たりに相手がいるか」を前のフレームと比べて決める（相手が消えても「出た」になる）。
- フェードは Target の **Alpha Group**（`AlphaGroupComponent`。Add Component → UI → Alpha Group）の Alpha を動かす。無ければ再生中だけ付く。
- **Alpha Group** は、自分と子の Image・Text・Vector Shape・Polyline・Sprite Renderer の色の a に Alpha を掛ける（親の Alpha Group とも掛け合わせる。Unity の Canvas Group と同じ考え）。Script から子をまとめてフェードするのにも使える：

```cpp
if (AlphaGroupComponent* group = GetComponent<AlphaGroupComponent>()) {
	group->alpha = MathUtility::MoveTowards(group->alpha, 0.0f, Time::GetDeltaTime() / 0.5f); // 0.5 秒で消す
}
```
- 描くときの値は `SpriteRenderSystem::CalcGroupAlpha(entity)`（自分と親を全部たどって掛けた値）。モデル・Primitive Renderer には効かない。Sprite Renderer の Alpha Clip は、半分より透明になると急に消える。

