参考：
- 格闘ゲームの Hitbox・Hurtbox（攻撃判定・やられ判定・押し合う形を分ける）：https://en.wikipedia.org/wiki/Hitbox
- Unreal の ApplyDamage（当たりとダメージを分ける。攻撃した物・撃った本人・ダメージの種類を渡す）：https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/Kismet/UGameplayStatics/ApplyDamage
- Unreal の AActor::TakeDamage（受ける側が量を決める・断る）：https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/GameFramework/AActor/TakeDamage
- Unreal の UDamageType（攻撃の種類）：https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/GameFramework/UDamageType
- Unity のレイヤー（Entity ごとに1つ・名前は設定で付ける）：https://docs.unity3d.com/Manual/Layers.html
- Unity の LayerMask（LayerToName・NameToLayer・GetMask）：https://docs.unity3d.com/ScriptReference/LayerMask.html
- 2D の符号付き距離（円・扇・ドーナツ・箱。Inigo Quilez）：https://iquilezles.org/articles/distfunctions2d/
- 3D の符号付き距離と押し出し（Extrusion）：https://iquilezles.org/articles/distfunctions/
- 黄金分割探索（谷を細かく探す）：https://en.wikipedia.org/wiki/Golden-section_search
- marching squares（格子で縁を探す）：https://en.wikipedia.org/wiki/Marching_squares
- `std::bit_cast`：https://en.cppreference.com/w/cpp/numeric/bit_cast
- `std::ranges::sort` の射影（projection）：https://en.cppreference.com/w/cpp/algorithm/ranges/sort
- `std::erase_if`（`std::unordered_map`）：https://en.cppreference.com/w/cpp/container/unordered_map/erase_if
- `std::unordered_map::try_emplace`：https://en.cppreference.com/w/cpp/container/unordered_map/try_emplace

# 攻撃の当たりとダメージ（Hitbox・Hurtbox・Health）

攻撃が当たる・ダメージを受ける仕組みをエンジンに足す。Character Controller と同じく、層に分ける。

- **形**（`HitShape`）：攻撃の形（円・扇・線・球・カプセル）と、そこまでの距離。ECS も時間も知らない
- **当たり**（Hitbox・Hurtbox・`HitSystem`）：誰が誰に・いつ当たったか（窓・敵と味方・当てた相手の記録・すり抜けない・ジャスト判定の問い合わせ）。HP は知らない
- **ダメージ**（Health・Damage Source・`DamageSystem`）：当たりを受けて、断る・量を決める・HP を減らす・手応え。形は知らない
- **ゲーム**（Script・攻撃部品・弾の SYSTEM）：いつ窓を開けるか、無敵の旗、ダメージの数字、ジャストの連続数

あわせて、敵と味方を分けるために **Entity のレイヤー**（Unity と同じ）を足す。

パートは **A → B → C → D → E の順に写す**。どのパートの後でもビルドできる（C までなら、当たりの知らせだけ来て HP は減らない）。E は Sandbox のテスト。

ブランチは `feature/HitDamage`（develop から）。

## 0章 全体の形

### 0.1 層と流れ

```
                  ┌───────── 同じ値・同じ式（符号付き距離）─────────┐
形の層            HitShape ─→ 当たる範囲   ジャスト判定の範囲   床の予告（後で。シェーダーに同じ式）
（Math/Geometry）             d ≦ r        d ≦ r ＋ Margin       d ≦ 0 を塗る

当たりの層        Hitbox（形・当てるレイヤー・窓・多段・ジャスト）   Hurtbox（やられ判定・倍率）
（Combat）              └──── HitSystem ────┘
                          窓を進める → 重なり（動いた跡も）→ 受ける本体ごとに1つ → 記録を見る
                          → ダメージの層に聞く（登録された答え方）→ 記録して知らせる
                                        │受けるか？・受ける本体は？
ダメージの層      Health（HP・無敵・効かない種類）   Damage Source（量・種類・距離の倍率・手応え）
（Combat）              └──── DamageSystem ────┘
                          断る（倒れている・無敵・効かない種類）→ 量 → Script の OnBeforeHitTaken
                          → HP を減らす → 被弾の後の無敵 → ヒットストップ・揺れ

ゲームの層        攻撃部品（SyncHitbox で窓を段階に合わせる）・Script（Invincible を書く・TryConsumeJust・OnHitTaken で数字）
                  弾の COMPONENT / SYSTEM（Is Spent で消す）
```

```
1フレーム（再生中）
  Script の Update
    3.  当たり（OnTrigger〜・OnCollision〜）
    3.5 攻撃の当たり（OnHitDealt・OnHitTaken）       ← 新：前のフレームの終わりの知らせ
    4.  Update                                       ← 無敵の旗を書く・ジャストを聞く・窓を開ける
  SYSTEM（弾）
  …攻撃表（攻撃部品が窓を開ける）・アニメ・Timeline・キャラ・物理・カメラ…
  破棄の反映
  CollisionSystem::Update
  DamageSystem::Update                               ← 新：被弾の後の無敵の残りを減らす
  HitSystem::Update                                  ← 新：全部が動いた後の位置で当たりを調べる
  …
```

Collider（押し合う形）・Hurtbox（やられ判定）・Hitbox（攻撃判定）は**別の物**。Collider の当たり（OnCollision〜）と攻撃の当たり（OnHitDealt・OnHitTaken）も別。

### 0.2 作る物・直す物

| ファイル | パート | 役目 |
|---|---|---|
| `UI/GlobalVariables.h`・`.cpp` | A | Settings の項目に文字（`std::string`）を足す |
| `Core/NameList.h`・`.cpp`（新しい） | A | Settings の「番号 → 名前」の一覧（32 個） |
| `ECS/Layers.h`・`.cpp`（新しい） | A | Entity のレイヤーの名前・`LayerMask` |
| `ECS/ComponentUI.h`・`.cpp`・`ComponentSerializer.cpp` | A | 名前の一覧から選ぶ欄（`NameField`・`NameMaskField`） |
| `ECS/Entity.h`・`GameObject.h`・`.cpp` | A | `layer` |
| `Scene/SceneSerializer.h`・`.cpp`・`Scene/Prefab/PrefabRecords.h`・`.cpp`・`PrefabOverrides.cpp`・`PrefabManager.cpp` | A | レイヤーの保存・Prefab の上書き |
| `Editor/History/EditorHistory.h`・`.cpp`・`Editor/Windows/InspectorWindow.cpp` | A | レイヤーの Undo・Inspector の見出しの欄 |
| `Math/Geometry/HitShape.h`・`.cpp`（新しい） | B | 攻撃の形と符号付き距離 |
| `Combat/HitInfo.h`（新しい） | C | 当たり1件の知らせ |
| `Components/Combat/HitboxComponent.h`・`HurtboxComponent.h`（新しい） | C | 攻撃判定・やられ判定 |
| `Systems/Combat/HitSystem.h`・`.cpp`（新しい） | C | 窓・当たり・記録・ジャスト判定・ギズモ |
| `ECS/Script.h`・`Systems/Gameplay/ScriptSystem.h`・`.cpp` | C | `OnHitDealt`・`OnHitTaken`・`OnBeforeHitTaken` |
| `Combat/DamageTypes.h`・`.cpp`（新しい） | D | 攻撃の種類の名前 |
| `Components/Combat/HealthComponent.h`・`DamageSourceComponent.h`（新しい） | D | HP・ダメージの量と手応え |
| `Systems/Combat/DamageSystem.h`・`.cpp`（新しい） | D | 断る・量・HP・手応え |
| `Components/Gameplay/AttackTableComponent.h`・`Systems/Gameplay/AttackTableSystem.cpp` | D | HP の割合を Health から読む |
| `AI/AttackPart.h`・`.cpp` | D | `SyncHitbox`（窓を攻撃の段階に合わせる） |
| `ECS/GameComponent.h`・`Systems/EngineSystems.cpp`・`Editor/ImGuiManager.cpp` | A・C・D | 初期化・呼ぶ順・ギズモ・メニュー |
| `HTTEngine.vcxproj`・`.filters` | A〜D | 新しいファイルを足す |
| `Sandbox/Parameters/Layers/Layers.json`・`DamageTypes/DamageTypes.json`（新しい） | E | レイヤーと攻撃の種類の名前 |
| `Sandbox/Source/Scripts/HitTestPlayer`・`HitboxCycler`・`TestBullet`（新しい） | E | テスト用の Script・弾 |
| `Sandbox/Resources/scenes/HitDamageTest.scene.json`（新しい） | E | 円・扇・線・多段・弾のテストシーン |

### 0.3 使い方

**レイヤーの名前**：Settings の **Layers** で名前を付ける（例：1 Player、2 Enemy）。Entity の Inspector の名前の下の **Layer** で選ぶ。

**プレイヤー（受ける側）**

- 同じ Entity に **Health**（Max Health）と **Hurtbox**（Capsule。押し合う Collider より小さめでよい）を付ける。Layer は Player
- 無敵はゲームが書く。回避ジャンプの無敵なら毎フレーム写す

```cpp
// PlayerController::Update
if (HealthComponent* health = GetComponent<HealthComponent>()) {
	health->invincible = IsInvincible(); // true の間は断る（記録しないので、切れた後に同じ攻撃の範囲にいれば当たる）
}
```

**ボスの攻撃（攻撃部品から窓を開ける）**

- ボスの Entity（Layer は Enemy）に **Health** と **Hurtbox**。部分ごとなら子の Entity に Hurtbox を1つずつ（弱点は Damage Multiplier を大きく）。Attack Table の HP の割合は Health から自動で入る
- 攻撃ごとに、子の Entity に **Hitbox**（Shape・Targets は Player）と **Damage Source**。Instigator は空でよい（子なので、ボスとその親子には当たらない）

```cpp
// PawSlam.cpp（攻撃部品）
void PawSlam::OnBegin(const AttackPartContext& context) { hitbox_ = context.self.FindChild("Paw Slam Hitbox"); }
void PawSlam::OnStage(const AttackPartContext& context) { SyncHitbox(hitbox_, context); } // 予備動作→予告、攻撃→当たる、硬直→閉じる
void PawSlam::OnEnd(const AttackPartContext&, AttackPartEnd) { HitSystem::Deactivate(hitbox_.handle); } // 怯みで止められても閉じる
```

**床に出して自分で進める攻撃（Prefab）**：Hitbox の Phase を Warning、Warning Seconds・Activate After Warning・Active Seconds を入れた Prefab を、プレイヤーの足元などに Instantiate する（Instigator はボスを書く）。予告 → 当たる → 閉じる、は Hitbox が時間で進める。

**弾（COMPONENT / SYSTEM）**：Hitbox（Sphere・Phase は Active・Max Targets は 1・Just Enabled はオフ）と Damage Source を付けて作る。当てきったら `hitbox.IsSpent()` が true になるので、弾の SYSTEM で消す（E-2 の `TestBullet`）。速くてもすり抜けない。

**ジャスト回避**

```cpp
// 回避ジャンプを始めた瞬間
JustCandidate just;
if (HitSystem::TryConsumeJust(gameObject.handle, just)) {
	++justCount_; // ジャスト。よけた攻撃は、無敵が切れた後も、その相手にはもう当たらない
}
```

**ダメージの数字**：`OnHitTaken(const HitInfo& hit)`（受けた本体）・`OnHitDealt(const HitInfo& hit)`（Hitbox の Entity と撃った本人）で `hit.point` に `hit.damage` を出す。

**ガード**：受ける本体の Script の `OnBeforeHitTaken(HitInfo& hit)` で `hit.result = HitResult::Guarded; hit.damage *= 0.2f;`（断るなら `HitResult::Ignored`）。

**距離で威力が変わる弾**：Damage Source の Falloff Curve に .curve を選ぶ（横軸＝距離を Near〜Far で 0〜1 に直した値、縦軸＝倍率。近いほど強いなら右下がり）。

**ギズモ**：メニューの View → Gizmos → **Hit Shapes**。Hitbox の形は灰（Off）・黄（Warning）・赤（Active）、水色はジャスト判定の範囲、緑は Hurtbox。

### 0.4 決めたこと

| 項目 | 決めたこと | 理由 |
|---|---|---|
| 攻撃の形 | **攻撃専用の `HitShape`**（形の層）。Collider には足さない | Collider は押し合う形。扇で押し戻す意味は無く、全部の形の組み合わせの CalcPushOut・Cast が要る。攻撃は回る（扇・線の向き）が、Collider は回らない |
| 形の表し方 | **符号付き距離 1 本**で、当たり・ジャスト範囲・床の予告を作る | 3つが同じ値・同じ式を見るので、ずれない。一回り大きくするのは足し算1つ（1.1） |
| 受ける側の形 | **Hurtbox が自分の形を持つ**（球・カプセル、回る）。部分ごとに子の Entity | 押し合う形と分ける（自機の当たりを小さく・ボスの部分ごとに倍率）。格闘ゲームと同じ |
| 敵と味方 | **Entity のレイヤー**（32 個・名前は Settings）。Hitbox は当てるレイヤーの `LayerMask` | チームとレイヤーを別に作ると、後で絞り方が2つになる。CollisionSystem・Raycast も後で同じレイヤーを使える。絞り込みは `targets` の1か所で、Tag は後で足す |
| 撃った本人 | Hitbox の Instigator（空なら自分）。本人・Hitbox の Entity とその親子には当たらない | 自分の弾が自分に当たらない。レイヤーを同じにしなくても安全 |
| 窓 | **Hitbox は窓だけ**（Off・Warning・Active）。開けるのは呼ぶ側。攻撃部品は `SyncHitbox` を1行 | 攻撃表と Hitbox がお互いを知らない。窓が3つの連続攻撃・足元に出す予告・弾・Timeline も同じ窓で書ける |
| 当てた相手の記録 | **受ける本体ごと**。Active になるたびに新しい攻撃の回 | 頭と手の両方に入っても1回。そのときは倍率の大きい方 |
| 多段ヒット | Rehit Interval（0 なら1回）・Max Targets（0 なら何体でも） | 弾は Max Targets 1。当てきったら Off |
| 断る | **Health の Invincible（旗）＋ 被弾の後の無敵 ＋ 受ける前の問い合わせ** | 旗は Inspector で見えて、Script の無い物にも効く。条件付き（ガード）は Script で。断った当たりは記録しない（無敵が切れた後に当たる） |
| ガード | `HitResult::Guarded`：記録する・量は書いた分・手応えは出さない・被弾の後の無敵も出さない | 「防いだ」は「当たった」とは別に知らせたい（ガードの音・削り） |
| HP | **エンジンに Health**。攻撃表は Health があれば HP の割合をそこから読む | チーム制作で同じ物を使う。書き忘れ・書く順番のずれが出ない |
| 当たりとダメージ | **層を分ける**。Hitbox と Damage Source は別の Component | 当たりだけの物（スイッチ・回復の範囲・ジャストだけ）も作れる。Unreal・GAS と同じ分け方 |
| 層のつなぎ方 | ダメージの層が `HitSystem` に答え方を2つ登録（受ける本体・受けるか） | MotionQuery と同じ。当たりの層は Health を知らない（1.8） |
| ジャスト判定 | `TryConsumeJust`。有効なのは Warning の最後の Just Lead Time 秒と Active。**よけた攻撃はもう当たらない** | Windup 全部で有効だと、2秒前に跳んでもジャストになる。無敵が切れたとたんに同じ攻撃の残りで当たる理不尽を防ぐ |
| 距離で変わる威力 | **Damage Source の Falloff Curve**（距離は Active になった所から） | FPS の距離減衰・Unreal の Radial Damage と同じ。弾（Script の無い物）でも使える |
| ヒットストップ・揺れ | **Damage Source の値でエンジンが掛ける**（0 なら無し） | 格闘ゲームの技のデータと同じ。後で「当たった所に出す Prefab・音」もここに足せる |
| 攻撃の種類 | **Settings の名前の一覧**（DamageTypes）。Damage Source で1つ、Health で効かない種類 | Unreal の DamageType の役目。番号で持つので名前を付け直しても変わらない |
| すり抜けない | Hurtbox から見た形の動き（形の動き − Hurtbox の動き）を、Hurtbox の半径の間隔で調べる | 弾が速くても、プレイヤーがダッシュで攻撃を通り抜けても当たる（1.5） |
| ワープ | Hurtbox が1フレームに 5m より動いたら、跡を調べない。窓が開いた最初のフレームも調べない | リスポーンで通っていない所の攻撃に当たらない・開く前の位置で当たらない（写しで見つけた） |
| 知らせの時 | 次のフレームの Script の Update の前（Collider と同じ）。問い合わせだけその場 | 当たりを調べる順番に左右されない。SYSTEM も同じ一覧を読む |
| 時間 | `Time::GetDeltaTime()`。0 の間（一時停止・ヒットストップ）は窓も当たりも進めない | 攻撃表と同じ。ヒットストップ中に次の当たりが出ない |
| 保存 | 形・窓の初めの値は保存。攻撃の回・記録は保存しない（Stop で消える） | Prefab に「出たら予告して当たる」を書いておける |

### 0.5 場面ごとの動作

| 場面 | どうなるか |
|---|---|
| 円の中に立つ（Active） | 1回だけ当たる（Active の間ずっと中にいても1回） |
| 円の外 | 当たらない |
| 扇の開きの外・ドーナツの穴の中 | 当たらない |
| 線の幅の外 | 当たらない |
| 床に置く形の上を跳んで越える | Height より高ければ当たらない |
| Warning の間に中にいる | 当たらない（予告だけ） |
| 無敵の間に中にいる → 無敵が切れる | 切れた後、まだ Active の範囲にいれば当たる |
| 予告の終わり（Just Lead Time 以内）にジャスト範囲で TryConsumeJust | ジャスト。同じ攻撃ではもうジャストにならず、当たらない |
| 予告の始め（Just Lead Time より前） | ジャストにならない |
| ジャストの後に予告をやめる → 次の攻撃 | 次の攻撃は当たる（別の攻撃） |
| Rehit Interval 0.25・1秒中にいる | 4 回くらい当たる |
| 被弾の後の無敵 0.45・Rehit 0.1 | 間引かれる（1秒で 2〜3 回） |
| Max Targets 1 の弾が2体に重なる | 先に触れた1体だけ。当てきって Off（Is Spent） |
| 自分の弾・撃った本人の子の Hurtbox | 当たらない |
| Targets に入っていないレイヤー | 当たらない |
| 1フレームに 3m 進む弾 | 通り抜けた Hurtbox に当たる（向きは進む向き） |
| 頭と体の両方に入る | 1回。倍率の大きい方 |
| OnBeforeHitTaken で Guarded | 記録する（もう当たらない）。HP は書いた量だけ。ヒットストップは出ない |
| OnBeforeHitTaken で Ignored | 記録しない（やめたら当たる） |
| 効かない種類 | 当たらない |
| HP が 0 になった | `isDefeated`。倒れた物にはもう当たらない。Attack Table は撃破へ |
| ヒットストップ中 | 窓も当たりも止まる |
| Stop | 窓・HP は保存した値に戻る。記録は消える |

### 0.6 今回やらないこと

3章にまとめた（床の予告の描画・Tag・CollisionSystem のレイヤー・当たった所に出す Prefab と音・攻撃側の倍率 など）。

## 1章 解説

### 1.1 符号付き距離で、3つを1つの式にする

**符号付き距離** d(p) は、点 p が形の外なら表面までの距離、中ならマイナスの値（表面ちょうどで 0）。

```
      d = +0.7      形の外
   ┌─────────┐
   │ d = −0.4 │    形の中
   └─────────┘
```

Hurtbox はどれも「芯（線分か点）を半径 r で太らせた形」なので、

- **当たる**：芯の上のどこかで d ≦ r
- **ジャスト範囲に入る**：芯の上のどこかで d ≦ r ＋ Just Margin（形を Margin だけ太らせたのと同じ）
- （2026-11-07 追記）Just Margin が 0 なら、ジャスト範囲は形そのもの（d ≦ r）。ジャストにしない当たりは Just Enabled をオフにする（BugFixRenameJustHitStop.md の 2章）
- **床の予告**（後で）：床の画素ごとに d ≦ 0 を塗る・d ≦ Margin を薄く塗る

3つとも同じ `HitShape` の値を、同じ式で読む。値を3か所に持たないので、予告の円と当たる範囲がずれることがない。

**新しい形の足し方**：`HitShapeType` に足して、`CalcSignedDistance2D`（床に置く形）か `CalcSignedDistance`（空中の形）に式を1つ書く。当たり・ジャスト・すり抜けない判定・ギズモは、全部その式を使うので、ほかは直さなくてよい。

### 1.2 扇・ドーナツの距離の式（`CalcSectorDistance`）

円・ドーナツ・扇・扇のドーナツは、どれも「半径 ri〜R、前 +Z から左右に a の範囲」。円は a ＝ 180 度（360 度の扇）、穴なしは ri ＝ 0 として、1つの式で書く。

左右は同じ形なので、x を正の側にそろえた q ＝ (|x|, z) で考える。φ は q の +Z からの角度。

```
            +Z
     ＼  e   ↑   ／        e：開きの端の向き（sin a, cos a）
       ＼    │  ／         横の辺：ri・e 〜 R・e の線分
   外    ＼  │／   外
           ＼│／
  ────────── ● ──────── +X
```

- φ ≦ a（開きの中）
  - 外側の弧の外（|q| ＞ R）・内側の弧の内側（|q| ＜ ri）：弧までの距離（|q| − R か ri − |q|）
  - 形の中：外側の弧・内側の弧・横の辺のうち、一番近い物までの距離にマイナスを付ける
- φ ＞ a（開きの外）：横の辺（線分）までの距離。弧の端は辺の端と同じ点なので、辺だけ見ればよい

内側の弧は ri ＞ 0 のときだけ見る（ri ＝ 0 なら真ん中の1点で、縁ではない。写しで、円の中の点が「真ん中までの距離」になる誤りを見つけた）。

線（長方形）は、中心 (0, Length / 2)・半分の大きさ (Width / 2, Length / 2) の箱の式（参考の iq の sdBox）。

写しで、縁を2万個の点に分けて一番近い点を総当たりで探した値と比べ、円・ドーナツ・扇（10・30・90・180・200・270 度）・扇のドーナツ・線の 44000 点で、ずれは 0.000001 以下だった。

### 1.3 床の形を柱にする（`Extrude`）

円・扇・線は、床の面（ローカルの XZ）の形を、高さ 0〜Height の柱にした物。横の距離 d2 と上下の距離 dy（柱の真ん中からの距離 − 高さの半分）を組み合わせる。

```
dy ＝ |y − Height / 2| − Height / 2
d  ＝ min(max(d2, dy), 0) ＋ length(max(d2, 0), max(dy, 0))
```

- 横も上下も中：近い方の縁（max(d2, dy) はマイナス）
- 横だけ外（上下は中）：d2
- 角の外（横も上下も外）：2つの距離の三平方

Height より高く跳べば当たらない（衝撃波を跳んでよける、が書ける）。

### 1.4 カプセルとの重なり（並べて、谷を細かく探す）

Hurtbox のカプセルに当たるかは「芯の線分の上で、d が一番小さい所が r 以下か」。線分の上で d がどう変わるかは形しだいなので、どの形でも同じ書き方にする（Character Controller の Cast と同じ考え）。

```
d  ●             ●
     ●         ●            1. 線分に、半径の間隔で点を並べて d を測る
       ●     ●              2. 両隣より小さい所（谷）ごとに、両隣の間を
         ● ●                   黄金分割探索で細かく探す（24 回）
          ▲ 谷
```

**黄金分割探索**：区間の中に2点を置いて d を比べ、大きい方の外側を捨てる。残った区間に、前の点をそのまま使えるように 0.618 : 0.382 で点を置くので、1回に1つしか測らない。24 回で区間は 1 万分の 1 より細かくなる。

形が凸（ふつうの円・90 度の扇・線）なら谷は1つで、ちょうどの答えになる。へこんだ形（ドーナツ・180 度より広い扇）は谷がいくつかあるので、谷ごとに探して一番小さい物を使う。点の間隔を半径までにするのは、d は1m 動くと 1 までしか変わらない（傾きが 1 以下）ので、間隔が半径以下なら、当たっている所を挟む点のどれかが必ず谷の近くに来るから。

当たった位置（`HitInfo::point`）は、見つけた芯の点から、形の縁へ寄せた所（d の傾き＝外へ向かう向きの逆へ、d だけ。半径まで）。芯が形の中なら芯の点。

### 1.5 すり抜けない（Hurtbox から見た形の動き）

弾が1フレームに Hurtbox より長く進むと、前のフレームも今のフレームも重ならずに、飛び越える。Hitbox ごとに前のフレームの形の位置を覚えておき、**Hurtbox から見た形の動き**（形の動き − Hurtbox の動き）の跡を調べる。

```
前のフレーム ○ ─ ─ ─ ─ ─ ─ ─ ─▶ ○ 今のフレーム
                    ●
              Hurtbox（今の位置に置いたまま）
形を前の位置から今の位置まで、Hurtbox の半径の間隔で動かして重なりを見る（32 か所まで）
```

- Hurtbox の動きも引くので、プレイヤーがダッシュで細い攻撃を通り抜けても当たる
- 重なった割合（fraction）が小さい相手から当てる（弾が2体に重なっても、手前の1体に当たる）
- **窓が開いた最初のフレームは跡を調べない**（形も Hurtbox も）。開く前の位置で当たらないように
- **Hurtbox が1フレームに 5m より動いたら、ワープとみなす**（リスポーン・置き直しで、通っていない所の攻撃に当たらないように）

写しでは、1フレームに 3m 進む半径 0.1 の球（どのフレームでも Hurtbox に重ならない）が、ちょうど1回当たった。

### 1.6 攻撃の回と記録

```
Hitbox（Component）                 HitSystem が持つ記録（Hitbox ごと）
  phase      Off/Warning/Active       serial（記録が今どの回か）
  attackSerial（Active になるたび +1） records：受ける本体・回の番号・最後に当てた時刻・当てた・よけた
  hitCount（この回に当てた相手の数）
```

- Active に入るたびに `attackSerial` が 1 増える＝新しい攻撃の回。HitSystem は番号が変わったのを見て記録を空にする
  - `HitSystem::Activate`・Warning からの自動の Active・**phase を直接 Active に書いた**（Timeline・Inspector）の、どれでも同じ
- 記録は数が変わるので、バイト列で写す Component には入れず、HitSystem が持つ（Stop で消える）
- **Warning の間にジャストでよけた記録**は、これから始まる回の番号（attackSerial ＋ 1）で書く。Active になって記録を空にしても、その回の分は残る。予告をやめた（Warning → Off）ら捨てる

### 1.7 断る・防ぐ・当たる

| 結果 | 記録 | HP | 被弾の後の無敵 | ヒットストップ・揺れ |
|---|---|---|---|---|
| 断る（Invincible・被弾の後の無敵・効かない種類・倒れている・`Ignored`） | しない | 減らない | 出ない | 出ない |
| 防ぐ（`Guarded`） | する | `damage` の分 | 出ない | 出ない |
| 当たる（`Hit`） | する | `damage` の分 | 出る | 出る |

「断る」は記録しないので、同じ攻撃の回のうちに、後のフレームでもう一度調べられる。格闘ゲームの無敵（当たり自体が無かったことになる）と同じで、回避の無敵が切れたときに、まだ攻撃の範囲にいれば当たる。

### 1.8 答える係を登録する（当たりの層がダメージの層を知らない）

```cpp
using ReceiverFinder = Handle<Entity> (*)(Handle<Entity> hurtbox); // 受ける本体を決める
using HitResolver = bool (*)(HitInfo& hit);                       // 受けるか決めて、ダメージの欄を書く
```

`DamageSystem::Initialize` が、自分の関数の場所（関数ポインタ）を `HitSystem::SetReceiverFinder`・`SetHitResolver` に渡す。HitSystem は登録された関数を呼ぶだけで、Health・Damage Source の型を知らない（MotionQuery と同じ形）。

- パート C まで（ダメージの層が無い）：受ける本体は Hurtbox 自身、いつも受ける
- 受け方を変えたいゲーム（盾の Component など）は、ダメージの層の中に足す。当たりの層は直さなくてよい

### 1.9 ギズモ：格子で縁を探す（marching squares）

床に置く形の縁は、形ごとに線を書かず、符号付き距離から描く。

```
＋ ＋ ＋ －        1. 形を囲む 40 × 40 の格子の点ごとに、d − level を測る
＋ ＋ － －        2. ます目の4つの角で符号が変わる辺に、距離の比で点を置く
＋ － － －        3. ます目の中の2点を線でつなぐ（4辺とも変わる鞍の形は、
                     ます目の真ん中の符号で、つなぎ方を決める）
```

level ＝ 0 で形の縁、level ＝ Just Margin でジャスト範囲の縁。当たりと同じ式なので、ギズモの線＝本当に当たる所。床の予告も、後でシェーダーに同じ式を書く。

### 1.10 あまり使わない書き方

**`std::bit_cast<To>(from)`**（`<bit>`）：値のビットをそのまま、別の型として読む。`NameMaskField` の既定の中身で、`uint32_t` のビットの並びを `int` の項目として保存・読み込みに渡す（全部 1 なら -1）。`reinterpret_cast` と違って、大きさの同じ型どうしでしか書けず、未定義の動作にならない。

```cpp
int value = std::bit_cast<int>(bits);
Field(label, value, style);
bits = std::bit_cast<uint32_t>(value);
```

**`std::ranges::sort(range, {}, 射影)`**：3つ目に「並べる物から、比べる値を取り出す関数」を渡せる。比べる関数（2つ目）は `{}` で既定（小さい順）。

```cpp
std::ranges::sort(candidates, {}, [](const HitCandidate& candidate) { return candidate.sweep.fraction; }); // fraction の小さい順
```

**`std::erase_if(map, 条件)`**：`std::unordered_map` でも使える（C++20）。条件に合う要素を全部消す。見つからなくなった Hitbox・Hurtbox の記録を片付けるのに使う。

**`try_emplace` と構造化束縛**：`try_emplace(key)` は、キーが無ければ既定の値で作り、あれば何もしない。返す `std::pair<iterator, bool>` を `const auto [it, isNew]` で2つの名前に分けて受ける。

```cpp
const auto [it, isNew] = state->hitboxes.try_emplace(KeyOf(found));
```

## パートA レイヤー（Entity のレイヤーと、Settings の名前の一覧）

### A-1 `UI/GlobalVariables.h`

`Item` に `std::string` を足す。

```cpp
	// std::string は名前の一覧（レイヤー名・攻撃の種類の名前）などの文字の項目
	using Item = std::variant<bool, int32_t, float, Vector2, Vector3, Vector4, ComboItem, ColorItem, std::string>;
```

### A-2 `UI/GlobalVariables.cpp`

include の `EditorHistory.h` の下に足す。

```cpp
#ifdef USE_IMGUI
#include "HTTEngine/Editor/History/EditorHistory.h"
#include "HTTEngine/Editor/Widgets/EditorWidgets.h"
#endif
```

`DrawItem` の `ColorItem` の分岐の後（`}, item);` の前）に足す。

```cpp
		    } else if constexpr (std::is_same_v<T, std::string>) {
			    // 文字（長さの上限なし。日本語も打てる）。ラベルは左、入力欄は右（bool・int と同じ並び）
			    ImGui::Text("%s", label.c_str());
			    ImGui::SameLine(150.0f);
			    ImGui::SetNextItemWidth(-1);
			    EditorWidgets::InputText(uid.c_str(), v);
		    }
```

`ItemToJson` のコメントだけ直す（`std::string` もそのまま json になる）。

```cpp
			    return v; // bool・int32_t・float・std::string はそのまま
```

`JsonToItem` の `ComboItem` の分岐の後に足す。

```cpp
		    } else if constexpr (std::is_same_v<T, std::string>) {
			    if (!value.is_string()) {
				    return false;
			    }
			    v = value.get<std::string>();
		    }
```

### A-3 `Core/NameList.h`（新しい）

```cpp
#pragma once
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

/// <summary>
/// Settings の Project ページに置く「番号 → 名前」の一覧（32 個まで）。レイヤー・攻撃の種類に使う
/// <para>Component には番号（またはビットの並び）を書く。名前を付け直しても、選んだ物は変わらない（Unity のレイヤーと同じ）</para>
/// <para>名前は Settings で付ける（Parameters/&lt;グループ名&gt;/&lt;グループ名&gt;.json に保存）。先頭の決まった名前（Default など）は Settings に出さず、変えられない</para>
/// </summary>
class NameList {
public:
	static constexpr int kCount = 32; // 名前の数（uint32_t のビットの数。ビットの並びで「どれとどれ」を持てるように）

	/// <param name="groupName">Settings のページの名前（保存先のフォルダの名前にもなる）</param>
	/// <param name="itemPrefix">Settings の行の名前の頭（"Layer" なら "Layer 3"）</param>
	/// <param name="fixedNames">先頭から順に、決まった名前（Settings に出さない・変えられない）</param>
	NameList(std::string groupName, std::string itemPrefix, std::initializer_list<std::string_view> fixedNames);

	// Settings に名前の行を登録する（起動のときに1回。保存した名前があれば、その名前になる）
	void Register() const;

	// 名前（範囲の外・名前の無い番号は空）
	std::string GetName(int index) const;
	// 名前から番号（無ければ -1。空の名前も -1）
	int Find(std::string_view name) const;
	// Inspector に出す文字（"3: Enemy"。名前が無ければ "3: (Layer 3)"）
	std::string GetLabel(int index) const;
	// 名前のビットを立てた並び（知らない名前は警告を出して飛ばす）
	uint32_t MakeMask(std::initializer_list<std::string_view> names) const;
	// 名前の付いている番号の一覧（小さい順）。Inspector の選択肢に使う
	std::vector<int> GetNamedIndices() const;

	const std::string& GetGroupName() const { return groupName_; }

private:
	// Settings の行の名前（"Layer 3"）
	std::string ItemName(int index) const;

	std::string groupName_;               // Settings のページの名前
	std::string itemPrefix_;              // 行の名前の頭
	std::vector<std::string> fixedNames_; // 先頭の決まった名前
};
```

### A-4 `Core/NameList.cpp`（新しい）

```cpp
#include "NameList.h"

#include <format>
#include <utility>

#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/UI/GlobalVariables.h"

//=============================================================================
// 作る・登録
//=============================================================================
// ===== 作る =====
NameList::NameList(std::string groupName, std::string itemPrefix, std::initializer_list<std::string_view> fixedNames)
    : groupName_(std::move(groupName)), itemPrefix_(std::move(itemPrefix)) {
	for (const std::string_view name : fixedNames) {
		fixedNames_.emplace_back(name);
	}
}

// ===== Settings に名前の行を登録する =====
void NameList::Register() const {
	// ページ（グループ）とファイル（カテゴリ）は同じ名前。決まった名前の番号は出さない
	GlobalVariables::CategoryBuilder category = GlobalVariables::GetInstance()->Group(groupName_).Category(groupName_);
	for (int i = static_cast<int>(fixedNames_.size()); i < kCount; ++i) {
		category.Add(ItemName(i), std::string());
	}
}


//=============================================================================
// 名前を引く
//=============================================================================
// ===== 名前 =====
std::string NameList::GetName(int index) const {
	if (index < 0 || index >= kCount) {
		return {};
	}
	if (index < static_cast<int>(fixedNames_.size())) {
		return fixedNames_[index];
	}
	return GlobalVariables::GetInstance()->Get<std::string>(groupName_, groupName_, ItemName(index));
}

// ===== 名前から番号 =====
int NameList::Find(std::string_view name) const {
	if (name.empty()) {
		return -1;
	}
	for (int i = 0; i < kCount; ++i) {
		if (GetName(i) == name) {
			return i;
		}
	}
	return -1;
}

// ===== Inspector に出す文字 =====
std::string NameList::GetLabel(int index) const {
	const std::string name = GetName(index);
	return name.empty() ? std::format("{}: ({})", index, ItemName(index)) : std::format("{}: {}", index, name);
}

// ===== 名前のビットを立てた並び =====
uint32_t NameList::MakeMask(std::initializer_list<std::string_view> names) const {
	uint32_t bits = 0;
	for (const std::string_view name : names) {
		const int index = Find(name);
		if (index < 0) {
			LogManager::Warning(std::format("{} に \"{}\" という名前はありません（Settings の {} で付けてください）", groupName_, name, groupName_));
			continue;
		}
		bits |= (1u << index);
	}
	return bits;
}

// ===== 名前の付いている番号の一覧 =====
std::vector<int> NameList::GetNamedIndices() const {
	std::vector<int> indices;
	for (int i = 0; i < kCount; ++i) {
		if (!GetName(i).empty()) {
			indices.push_back(i);
		}
	}
	return indices;
}

// ===== Settings の行の名前 =====
std::string NameList::ItemName(int index) const { return std::format("{} {}", itemPrefix_, index); }
```

### A-5 `ECS/Layers.h`（新しい）

```cpp
#pragma once
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

// 前方宣言
class NameList;

/// <summary>
/// どのレイヤーか、のビットの並び（Unity の LayerMask と同じ）。1つの uint32_t に、32 個のレイヤーのどれを含むかを持つ
/// <para>Component に持たせて「当てる相手のレイヤー」などに使う。保存は数（名前を付け直しても、選んだ物は変わらない）</para>
/// </summary>
struct LayerMask {
	uint32_t bits = 0; // 番号 i のレイヤーを含むなら、i 番目のビットが 1

	// そのレイヤーを含むか
	constexpr bool Contains(int layer) const { return layer >= 0 && layer < 32 && ((bits >> layer) & 1u) != 0; }

	// 何も含まない・全部含む・1つだけ含む
	static constexpr LayerMask Nothing() { return {0u}; }
	static constexpr LayerMask Everything() { return {0xFFFFFFFFu}; }
	static constexpr LayerMask Of(int layer) { return {(layer >= 0 && layer < 32) ? (1u << layer) : 0u}; }

	bool operator==(const LayerMask&) const = default;
};


/// <summary>
/// Entity のレイヤー（Unity の Layer と同じ。Entity に1つ。0〜31 の番号で、名前は Settings の Layers で付ける）
/// <para>0 は Default（名前は変えられない）。攻撃の当たり（Hitbox の Targets）などが、相手の Entity のレイヤーで絞る</para>
/// </summary>
class Layers {
public:
	static constexpr int kDefault = 0; // 何も選ばなかったときのレイヤー

	// Settings に名前の行を登録する（EngineSystems::Initialize の初めに1回）
	static void Initialize();

	// 名前の一覧（Inspector の選択肢）
	static const NameList& GetNameList();

	// 番号 → 名前（名前が無ければ空）。Unity の LayerMask.LayerToName
	static std::string LayerToName(int layer);
	// 名前 → 番号（無ければ -1）。Unity の LayerMask.NameToLayer
	static int NameToLayer(std::string_view name);
	// 名前を並べてビットの並びを作る（Layers::GetMask({"Player", "Enemy"})）。Unity の LayerMask.GetMask
	static LayerMask GetMask(std::initializer_list<std::string_view> names);
};
```

### A-6 `ECS/Layers.cpp`（新しい）

```cpp
#include "Layers.h"

#include "HTTEngine/Core/NameList.h"

//=============================================================================
// 名前の一覧
//=============================================================================
// ===== 名前の一覧（関数の中の static。初めて呼んだときに作る）=====
const NameList& Layers::GetNameList() {
	static const NameList names("Layers", "Layer", {"Default"});
	return names;
}

// ===== Settings に名前の行を登録する =====
void Layers::Initialize() { GetNameList().Register(); }


//=============================================================================
// 番号 ↔ 名前
//=============================================================================
std::string Layers::LayerToName(int layer) { return GetNameList().GetName(layer); }

int Layers::NameToLayer(std::string_view name) { return GetNameList().Find(name); }

LayerMask Layers::GetMask(std::initializer_list<std::string_view> names) { return {GetNameList().MakeMask(names)}; }
```

### A-7 `ECS/ComponentUI.h`

include の先頭に `<bit>` を足す。

```cpp
#pragma once
#include <bit>
#include <cfloat>
```

前方宣言に足す。

```cpp
// 前方宣言
struct GameObject;
class NameList;
```

`ChoiceField` の下（`protected:` の前）に足す。

```cpp
	// ===== Settings の名前の一覧から選ぶ（レイヤー・攻撃の種類。NameList）=====
	// 保存は番号（名前を付け直しても、選んだ物は変わらない）。名前の選択肢を出すのは Inspector だけで、保存・読み込みの係は int の項目と同じに扱う
	// 1つ選ぶ（index は 0〜31）
	virtual void NameField(const char* label, int& index, const NameList& names, FieldStyle style = {}) {
		(void)names;
		Field(label, index, style);
	}
	// いくつでも選ぶ（i 番を選んだら、bits の i 番目のビットが 1）。保存は数（Unity の LayerMask と同じ）
	virtual void NameMaskField(const char* label, uint32_t& bits, const NameList& names, FieldStyle style = {}) {
		(void)names;
		int value = std::bit_cast<int>(bits); // ビットの並びを、そのまま int として読み書きする（全部 1 なら -1 になる）
		Field(label, value, style);
		bits = std::bit_cast<uint32_t>(value);
	}
```

### A-8 `ECS/ComponentSerializer.cpp`

`ValueWriterUI`（値を書く係）の「数で表せない項目は書かない」の並びの、`AssetField` の下に足す。

```cpp
	void AssetField(const char*, uint32_t&, AssetType, FieldStyle) override {}
	// 名前の一覧の番号・ビットの並びは、数でも途中の値に意味が無い（Timeline でなめらかに動かす物ではない）ので書かない
	void NameField(const char*, int&, const NameList&, FieldStyle) override {}
	void NameMaskField(const char*, uint32_t&, const NameList&, FieldStyle) override {}
```

`ValueListUI`（値を読む係）も同じ所に足す。

```cpp
	void AssetField(const char*, uint32_t&, AssetType, FieldStyle) override {}
	void NameField(const char*, int&, const NameList&, FieldStyle) override {}
	void NameMaskField(const char*, uint32_t&, const NameList&, FieldStyle) override {}
```

書く係（`JsonWriterUI`）・読む係（`JsonReaderUI`）・参照を付け替える係（`RefRemapUI`）は直さない（既定の中身＝ int の項目として保存・読み込みする）。

### A-9 `ECS/ComponentUI.cpp`

include に足す。

```cpp
#include "HTTEngine/AI/AttackTableManager.h"
#include "HTTEngine/Core/NameList.h"
#include "HTTEngine/Curve/CurveManager.h"
```

`DrawSelectedFontSample` の後（`/// Inspectorに描く係` の前）に足す。

```cpp
// ===== 名前の一覧のビットの並び =====
constexpr uint32_t kAllBits = 0xFFFFFFFFu; // 全部選んだ（Everything）
constexpr size_t kMaskPreviewNames = 3;    // 欄に並べる名前の数（これより多ければ「…」）

// 欄に出す文字（何も無し・全部・選んだ名前を「,」でつないだ物）
std::string MakeMaskPreview(uint32_t bits, const NameList& names) {
	if (bits == 0u) {
		return "Nothing";
	}
	if (bits == kAllBits) {
		return "Everything";
	}
	std::string text;
	size_t count = 0;
	for (int i = 0; i < NameList::kCount; ++i) {
		if ((bits & (1u << i)) == 0u) {
			continue;
		}
		if (count == kMaskPreviewNames) {
			return text + ", …";
		}
		const std::string name = names.GetName(i);
		text += (count == 0 ? "" : ", ") + (name.empty() ? std::format("({})", i) : name);
		++count;
	}
	return text;
}
```

`InspectorComponentUI` の `AssetField` の後（`ColorField` の前）に足す。

```cpp
	// Settings の名前の一覧から1つ選ぶ（名前の付いている番号だけ並べる。今の番号に名前が無くても、欄にはその番号を出す）
	void NameField(const char* label, int& index, const NameList& names, FieldStyle style) override {
		const std::string preview = names.GetLabel(index);
		if (ImGui::BeginCombo(label, preview.c_str())) {
			for (const int i : names.GetNamedIndices()) {
				const std::string text = names.GetLabel(i);
				if (ImGui::Selectable(text.c_str(), i == index)) {
					index = i;
				}
				if (i == index) {
					ImGui::SetItemDefaultFocus(); // 開いたときに今の選択へスクロールする
				}
			}
			ImGui::Separator();
			ImGui::TextDisabled("名前は Settings の %s で付ける", names.GetGroupName().c_str());
			ImGui::EndCombo();
		}
		DrawTooltip(style);
	}

	// Settings の名前の一覧から、いくつでも選ぶ（開くと、名前の付いている番号のチェックが並ぶ）
	void NameMaskField(const char* label, uint32_t& bits, const NameList& names, FieldStyle style) override {
		const std::string preview = MakeMaskPreview(bits, names);
		if (ImGui::BeginCombo(label, preview.c_str())) {
			if (ImGui::Selectable("Nothing", bits == 0u)) {
				bits = 0u;
			}
			if (ImGui::Selectable("Everything", bits == kAllBits)) {
				bits = kAllBits;
			}
			ImGui::Separator();
			// チェックは押しても一覧が閉じない（Selectable は閉じる）ので、続けていくつも選べる
			for (const int i : names.GetNamedIndices()) {
				const uint32_t bit = 1u << i;
				bool isOn = (bits & bit) != 0u;
				if (ImGui::Checkbox(names.GetLabel(i).c_str(), &isOn)) {
					bits = isOn ? (bits | bit) : (bits & ~bit);
				}
			}
			ImGui::Separator();
			ImGui::TextDisabled("名前は Settings の %s で付ける", names.GetGroupName().c_str());
			ImGui::EndCombo();
		}
		DrawTooltip(style);
	}
```

Release 用の `InspectorComponentUI`（`#else` の方）は直さない（既定の中身のまま＝何も描かない係の int の項目に流れる）。

### A-10 `ECS/Entity.h`

```cpp
	bool isActive = true;
	bool isDontSave = false; // シーンに保存しない（子も一緒に。エディタで編集中の画面の中身）。シーンファイル・Play の退避・保存していない変更（*）に入らない
	uint8_t layer = 0;       // レイヤー（0〜31。名前は Settings の Layers。Layers.h）。攻撃の当たりが「当てる相手」を絞るのに使う
};
```

### A-11 `Scene/Prefab/PrefabRecords.h`・`.cpp`

`.h`：`EntityRecords` のコメントを直し、下に足す。

```cpp
// Entity 1つ分の JSON（{"id", "name", "parent", "active", "layer", "components"}。シーンファイルの1項目と同じ形）の並び。親が子より前
// Prefab の中身を「置いたときの番号」で並べた物。Entity を作る前の形
using EntityRecords = std::vector<SceneJson>;

// ===== Entity 1つ分の JSON のレイヤー（"layer"）=====
// 0（Default）は書かない（前のファイル・ほとんどの Entity は Default なので、ファイルを変えないように）。無ければ 0
inline constexpr const char* kLayerKey = "layer";
// 読む（無い・0〜31 の数でなければ 0）
uint8_t ReadEntityLayer(const SceneJson& item);
// 書く（0 なら項目を消す）
void WriteEntityLayer(SceneJson& item, uint8_t layer);
```

`.cpp`：`ApplyOverride` の見出しと、名前の並びに `kLayerKey` を足す。

```cpp
// ===== Entity 1つ分に、上書き（書いてある名前・有効・レイヤー・親・Component と、外した Component）を当てる =====
// Component は書いてある項目だけを上書きする（merge_patch。{"translation": [...]} なら translation だけ変わる）。Prefab に無い型は足す
void ApplyOverride(SceneJson& record, const SceneJson& override) {
	for (const char* key : {"name", "active", kLayerKey, "parent"}) {
```

`namespace` の閉じかっこの後（`// 並べる` の前）に足す。

```cpp
//=============================================================================
// Entity 1つ分の JSON のレイヤー
//=============================================================================
// ===== 読む =====
uint8_t ReadEntityLayer(const SceneJson& item) {
	const auto it = item.find(kLayerKey); // const の json に [] で無いキーを引くと止まるので、find を使う
	if (it == item.end() || !it->is_number_integer()) {
		return 0;
	}
	const int layer = it->get<int>();
	return (layer >= 0 && layer < 32) ? static_cast<uint8_t>(layer) : uint8_t{0};
}

// ===== 書く =====
void WriteEntityLayer(SceneJson& item, uint8_t layer) {
	if (layer == 0) {
		item.erase(kLayerKey);
		return;
	}
	item[kLayerKey] = layer;
}
```

`ApplyEntry` の一番上を直す（見出しのコメントと、`WriteEntityLayer` の行）。

```cpp
	// --- 一番上（名前・有効・レイヤー・親は、書いていなくても置いた所ごとの値にする）---
	SceneJson& root = records[begin];
	ApplyOverride(root, entry);
	root["active"] = entry.value("active", true);
	WriteEntityLayer(root, ReadEntityLayer(entry)); // 書いていなければ Default
	root["parent"] = entry.value("parent", EntityId{0});
```

### A-12 `Scene/Prefab/PrefabOverrides.cpp`

`MakeInstanceEntry` の「置いた所ごとに必ず書く物」に足す。

```cpp
	// ===== 置いた所ごとに必ず書く物（一番上の番号・名前・親・有効・レイヤー・Prefab のパス・Transform）=====
	const Entity* parent = EntityManager::Get(entity->parent);
	SceneJson entry = SceneJson::object();
	entry["id"] = entity->id;
	entry["name"] = entity->name;
	entry["parent"] = (parent != nullptr) ? parent->id : EntityId{0};
	entry["active"] = entity->isActive;
	WriteEntityLayer(entry, entity->layer); // Default なら書かない（読むときも、無ければ Default）
```

Prefab が読めないときの `keptEntry` に足す。

```cpp
		for (const char* key : {"id", "name", "parent", "active"}) {
			keptEntry[key] = entry[key];
		}
		WriteEntityLayer(keptEntry, entity->layer);
```

中身1つずつの上書きの、`active` の下に足す。

```cpp
		if (content->isActive != record.value("active", true)) {
			override["active"] = content->isActive;
		}
		// レイヤーは違うときだけ書く。Default に戻したときも 0 と書く（書かないと Prefab の値に戻ってしまう）
		if (content->layer != ReadEntityLayer(record)) {
			override[kLayerKey] = content->layer;
		}
```

### A-13 `Scene/Prefab/PrefabManager.cpp`

1周目（Entity を作る）に足す。

```cpp
		EntityManager::Get(created)->isActive = item.value("active", true);
		EntityManager::Get(created)->layer = ReadEntityLayer(item);
		pending.created.push_back(created);
```

### A-14 `Scene/SceneSerializer.h`・`.cpp`

`.h`：版を 5 にする。

```cpp
	// 4：置いた所ごとの上書き（"overrides" など）を書く。3 のファイルはそのまま読める（上書きが無いだけ）
	// 5：Entity のレイヤー（"layer"。Default なら書かない）を書く。4 のファイルはそのまま読める（全部 Default）
	static constexpr int kVersion = 5;
```

`.cpp`：`SaveEntity` に足す。

```cpp
	item["active"] = entity.isActive;
	WriteEntityLayer(item, entity.layer); // Default（0）なら書かない
	item["components"] = std::move(components);
```

`LoadAll` の1周目に足す。

```cpp
		EntityManager::Get(created[i])->isActive = item.value("active", true);
		EntityManager::Get(created[i])->layer = ReadEntityLayer(item);
	}
```

### A-15 `Editor/History/EditorHistory.h`・`.cpp`

`.h`：`RequestSetActive` の下に足す。

```cpp
	static void RequestSetActive(Handle<Entity> handle, bool isActive);
	static void RequestSetLayer(Handle<Entity> handle, uint8_t layer); // 0〜31（子は変えない）
```

`.cpp`：`EntityData` に足す。

```cpp
	bool isActive = true;
	uint8_t layer = 0; // レイヤー
```

`CaptureOne` に足す。

```cpp
	data.isActive = entity->isActive;
	data.layer = entity->layer;
```

`Restore` の1周目に足す。

```cpp
		EntityManager::Get(handle)->isActive = data.isActive;
		EntityManager::Get(handle)->layer = data.layer;
```

`RequestSetActive` の下に足す。

```cpp
void EditorHistory::RequestSetLayer(Handle<Entity> handle, uint8_t layer) { RequestSetEntityValue(handle, &Entity::layer, layer); }
```

### A-16 `Editor/Windows/InspectorWindow.cpp`

include に足す。

```cpp
#include "HTTEngine/Core/NameList.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/Layers.h"
```

`DrawHeader` の名前の下（`// --- 参考情報` の前）に足す。

```cpp
	// --- レイヤー（Unity と同じく名前の下。選択肢は Settings の Layers で名前を付けた物。履歴を通す）---
	int layer = entity->layer;
	GetInspectorUI().NameField("Layer", layer, Layers::GetNameList(), Tip("当たりの相手を絞るのに使う（Hitbox の Targets）。名前は Settings の Layers で付ける"));
	if (layer != entity->layer) {
		EditorHistory::RequestSetLayer(handle, static_cast<uint8_t>(layer));
	}
```

### A-17 `ECS/GameObject.h`・`.cpp`

`.h`：変数のように書ける値に足す。

```cpp
	__declspec(property(get = IsActiveInHierarchy)) bool activeInHierarchy; // 親まで全部有効か（無効の親の下なら false）
	__declspec(property(get = GetLayer, put = SetLayer)) int layer;         // レイヤー（0〜31。名前との行き来は Layers::NameToLayer・LayerToName）
```

一番下に足す。

```cpp
	bool IsActiveInHierarchy() const;
	int GetLayer() const;               // 消えた GameObject なら 0
	void SetLayer(int newLayer) const;  // 0〜31 の外なら変えない（子は変えない。Unity と同じ）
};
```

`.cpp`：`IsActiveInHierarchy` の下に足す。

```cpp
//=============================================================================
// レイヤー
//=============================================================================
int GameObject::GetLayer() const {
	const Entity* entity = EntityManager::Get(handle);
	return (entity != nullptr) ? entity->layer : 0;
}

void GameObject::SetLayer(int newLayer) const {
	Entity* entity = EntityManager::Get(handle);
	if (entity != nullptr && newLayer >= 0 && newLayer < 32) {
		entity->layer = static_cast<uint8_t>(newLayer);
	}
}
```

### A-18 `Systems/EngineSystems.cpp`

include に足す。

```cpp
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/ECS/Layers.h"
#include "HTTEngine/Particle/ParticleManager.h"
```

`Initialize` の `MotionQuery::Initialize` の下に足す。

```cpp
	Layers::Initialize();      // レイヤーの名前（Settings の Layers）。シーンを読む前に、名前の行を登録しておく
```

### A-19 `HTTEngine.vcxproj`

Visual Studio のソリューションエクスプローラーで、フィルターの同じ場所に「既存の項目を追加」する（`.filters` も一緒に直る）。

| フィルター | 足すファイル |
|---|---|
| `HTTEngine\Core` | `NameList.h`・`NameList.cpp` |
| `HTTEngine\ECS` | `Layers.h`・`Layers.cpp` |

ここでビルドできる。Settings に **Layers** のページが出て、Inspector の名前の下に **Layer** の欄が出る。

## パートB 形の層（HitShape）

### B-1 `Math/Geometry/HitShape.h`（新しい）

```cpp
#pragma once
#include <type_traits>

#include "HTTEngine/Math/Geometry/Capsule.h"
#include "HTTEngine/Math/Geometry/Sphere.h"
#include "HTTEngine/Math/Matrix4x4.h"
#include "HTTEngine/Math/Vector2.h"
#include "HTTEngine/Math/Vector3.h"

// 攻撃の形（形の層）。当たり判定・ジャスト判定の範囲・床の予告の図形が、この1つの値と1つの式（符号付き距離）を使う
// 押し合う形（Collider）とは別。押し戻さないので、扇のような「物理では意味の無い形」も持てる


/// <summary>
/// 攻撃の形の種類
/// <para>円・扇・線は「床に置く形」：ローカルの XZ の面に描いた形を、底から上（+Y）へ Height だけ伸ばした柱。Entity を回すと一緒に回る</para>
/// </summary>
enum class HitShapeType {
	Circle,  // 円（Inner Radius を入れるとドーナツ）
	Sector,  // 扇（前 +Z を真ん中に Angle 度開く。Inner Radius を入れると扇のドーナツ）
	Line,    // 線（幅 Width の長方形。中心から前 +Z へ Length だけ伸びる）
	Sphere,  // 球（空中の攻撃・弾）
	Capsule, // カプセル（中心から前 +Z へ Length の線分を、Radius で太らせた形。剣を振る・太いレーザー）
};


/// <summary>
/// 攻撃の形（Component に持たせる値。位置は持たない。付けた Entity の Transform から取る）
/// </summary>
struct HitShape {
	HitShapeType type = HitShapeType::Circle; // 種類
	Vector3 center = {0.0f, 0.0f, 0.0f};      // Entity の位置からのずれ（ローカル。床に置く形は底の真ん中）
	float radius = 2.0f;                      // 円・扇の外側の半径／球・カプセルの半径
	float innerRadius = 0.0f;                 // 円・扇の内側の半径（0 なら穴なし）
	float angle = 90.0f;                      // 扇の開き（度。0〜360。360 なら円と同じ）
	float length = 4.0f;                      // 線・カプセルの長さ（中心から前 +Z へ）
	float width = 1.0f;                       // 線の幅
	float height = 2.0f;                      // 床に置く形（円・扇・線）の高さ（底から上 +Y へ）

	bool operator==(const HitShape&) const = default;
};
static_assert(std::is_trivially_copyable_v<HitShape>);

// 床に置く形（円・扇・線）か。床の予告の図形を描けるのはこれだけ
constexpr bool IsFloorShape(HitShapeType type) { return type == HitShapeType::Circle || type == HitShapeType::Sector || type == HitShapeType::Line; }


/// <summary>
/// ワールドに置いた攻撃の形（判定・ギズモ・床の予告はこれを使う）
/// <para>大きさは Entity の大きさを掛けた後（円・扇の半径は X と Z の大きい方、線の幅は X、長さは Z、高さは Y、球は一番大きい軸）</para>
/// </summary>
struct WorldHitShape {
	HitShapeType type = HitShapeType::Circle;
	Vector3 origin = {0.0f, 0.0f, 0.0f};  // 形の中心（ワールド。床に置く形は底の真ん中）
	Vector3 right = {1.0f, 0.0f, 0.0f};   // 形のローカル +X（長さ1）
	Vector3 up = {0.0f, 1.0f, 0.0f};      // 形のローカル +Y（長さ1）
	Vector3 forward = {0.0f, 0.0f, 1.0f}; // 形のローカル +Z（長さ1）
	float radius = 0.0f;                  // 外側の半径（球・カプセルは半径）
	float innerRadius = 0.0f;             // 内側の半径
	float halfAngle = 0.0f;               // 扇の開きの半分（ラジアン。0〜π）
	float length = 0.0f;                  // 線・カプセルの長さ
	float width = 0.0f;                   // 線の幅
	float height = 0.0f;                  // 床に置く形の高さ

	/// <summary>
	/// HitShape を Entity のワールド行列で置く
	/// </summary>
	static WorldHitShape Make(const HitShape& shape, const Matrix4x4& worldMatrix);

	// ===== 座標 =====
	Vector3 ToLocal(const Vector3& world) const; // ワールドの点 → 形のローカル（origin が原点、向きは right・up・forward）
	Vector3 ToWorld(const Vector3& local) const; // 形のローカルの点 → ワールド

	// ===== 符号付き距離（形の外なら表面までの距離、中ならマイナス。全部の判定・描画の元）=====
	/// <summary>
	/// 床の面（ローカルの XZ）の中での符号付き距離。床に置く形だけ（球・カプセルは、その面で切った断面）
	/// <para>床の予告の図形はこれで塗る（シェーダーに同じ式を書く）</para>
	/// </summary>
	float CalcSignedDistance2D(const Vector2& localXZ) const;
	// 3D の符号付き距離（ワールドの点）
	float CalcSignedDistance(const Vector3& world) const;
	// 表面から外へ向かう向き（符号付き距離の傾き。長さ1）
	Vector3 CalcNormal(const Vector3& world) const;

	// ===== 重なり（margin だけ形を太らせて調べる。ジャスト範囲は margin ＞ 0）=====
	// 球と重なるか
	bool Overlaps(const Sphere& sphere, float margin = 0.0f) const;
	/// <summary>
	/// カプセル（線分を半径で太らせた形）と重なるか。動いた球の跡（すり抜けない判定）にも使う
	/// </summary>
	/// <param name="nearest">線分の上で、形に一番近い点が入る（要らなければ nullptr）</param>
	bool Overlaps(const Capsule& capsule, float margin = 0.0f, Vector3* nearest = nullptr) const;

	// 線分の上で、形への符号付き距離が一番小さい点（Overlaps の中身。並べて調べてから、谷を細かく探す）
	Vector3 FindNearestOnSegment(const Vector3& start, const Vector3& end, float sampleStep) const;
};
```

### B-2 `Math/Geometry/HitShape.cpp`（新しい）

```cpp
#include "HitShape.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace {
constexpr float kMinAxisLength = 1.0e-6f;  // これより短い軸は、大きさ0とみなす（向きが決まらない）
constexpr float kNormalEpsilon = 1.0e-3f;  // 傾き（向き）を差で求めるときにずらす距離
constexpr int kMaxSegmentSamples = 64;     // 線分の上に並べる点の数の上限
constexpr int kGoldenIterations = 24;      // 谷を細かく探す回数（1回で区間が 0.618 倍になる。24 回で 1/10000 より細かい）
constexpr float kInvGoldenRatio = 0.6180339887f; // 黄金比の逆数（(√5 − 1) / 2）

// 行列の1行（ローカルの軸がワールドでどちらを向き、どれだけの長さか）
Vector3 RowOf(const Matrix4x4& m, int row) { return {m.m[row][0], m.m[row][1], m.m[row][2]}; }

// 長さ1にする。短すぎれば fallback
Vector3 NormalizeOr(const Vector3& v, const Vector3& fallback) {
	const float length = Length(v);
	return (length > kMinAxisLength) ? v / length : fallback;
}

//=============================================================================
// 床の面の形（2D の符号付き距離。p はローカルの (x, z)）
//=============================================================================
// ===== 円・ドーナツ・扇・扇のドーナツ（どれも「半径 ri〜R、前 +Z から ±a の範囲」の形）=====
// 左右が同じ形なので、x を正の側にそろえて考える。q＝(|x|, z)、φ＝q の +Z からの角度
//   φ ≦ a（開きの中）：外側の弧・内側の弧・横の辺のうち、一番近い物までの距離
//   φ ＞ a（開きの外）：横の辺（ri・e 〜 R・e の線分。e は開きの端の向き）までの距離
float CalcSectorDistance(const Vector2& p, float outerRadius, float innerRadius, float halfAngle) {
	const Vector2 q = {std::abs(p.x), p.y};
	const float radial = Length(q);
	// 弧だけを見た距離（中ならマイナス）。内側の弧は、内側の半径があるときだけ（0 なら真ん中の1点で、縁ではない）
	const float radialDistance = (innerRadius > 0.0f) ? (std::max)(radial - outerRadius, innerRadius - radial) : radial - outerRadius;
	if (halfAngle >= std::numbers::pi_v<float>) {
		return radialDistance; // 円・ドーナツ（横の辺が無い）
	}
	// 横の辺（開きの端の向き e に、内側の半径から外側の半径までの線分）までの距離
	const Vector2 edge = {std::sin(halfAngle), std::cos(halfAngle)};
	const float along = std::clamp(Dot(q, edge), innerRadius, outerRadius);
	const float edgeDistance = Length(q - edge * along);
	const float phi = std::atan2(q.x, q.y); // +Z からの角度（0〜π）
	if (phi > halfAngle) {
		return edgeDistance; // 開きの外：一番近いのは横の辺（弧の端も辺の端なので含まれる）
	}
	if (radialDistance > 0.0f) {
		return radialDistance; // 開きの中で、弧の外（外側の弧の外・内側の弧の内側）
	}
	return -(std::min)(-radialDistance, edgeDistance); // 中：一番近い縁までの距離をマイナスで
}

// ===== 線（幅 width の長方形。z = 0 から length まで）=====
float CalcLineDistance(const Vector2& p, float width, float length) {
	const Vector2 halfSize = {width * 0.5f, length * 0.5f};
	const Vector2 d = {std::abs(p.x) - halfSize.x, std::abs(p.y - halfSize.y) - halfSize.y};
	const Vector2 outside = {(std::max)(d.x, 0.0f), (std::max)(d.y, 0.0f)};
	return Length(outside) + (std::min)((std::max)(d.x, d.y), 0.0f);
}

// ===== 2D の形を、高さ height の柱にする（y = 0〜height）=====
// 横の距離 d2 と、上下の距離 dy を組み合わせる。角の外では2つの距離の三平方、中では近い方
float Extrude(float d2, float y, float height) {
	const float halfHeight = height * 0.5f;
	const float dy = std::abs(y - halfHeight) - halfHeight;
	const Vector2 outside = {(std::max)(d2, 0.0f), (std::max)(dy, 0.0f)};
	return (std::min)((std::max)(d2, dy), 0.0f) + Length(outside);
}
} // namespace


//=============================================================================
// 置く
//=============================================================================
WorldHitShape WorldHitShape::Make(const HitShape& shape, const Matrix4x4& worldMatrix) {
	WorldHitShape world;
	world.type = shape.type;
	world.origin = TransformPoint(shape.center, worldMatrix);

	// --- 向き：前を先に決めて、右・上を直角にそろえる（親の大きさが軸ごとに違って、少しゆがんでいても直角にする）---
	const Vector3 rowX = RowOf(worldMatrix, 0);
	const Vector3 rowY = RowOf(worldMatrix, 1);
	const Vector3 rowZ = RowOf(worldMatrix, 2);
	world.forward = NormalizeOr(rowZ, {0.0f, 0.0f, 1.0f});
	world.right = NormalizeOr(rowX - world.forward * Dot(rowX, world.forward), {1.0f, 0.0f, 0.0f});
	world.up = Cross(world.forward, world.right); // 前 × 右 ＝ 上

	// --- 大きさ（Entity の大きさを、軸ごとに掛ける）---
	const float scaleX = Length(rowX);
	const float scaleY = Length(rowY);
	const float scaleZ = Length(rowZ);
	const float scaleXZ = (std::max)(scaleX, scaleZ);
	switch (shape.type) {
	case HitShapeType::Circle:
	case HitShapeType::Sector:
		world.radius = shape.radius * scaleXZ;
		world.innerRadius = std::clamp(shape.innerRadius, 0.0f, shape.radius) * scaleXZ;
		break;
	case HitShapeType::Line:
		world.width = shape.width * scaleX;
		world.length = shape.length * scaleZ;
		break;
	case HitShapeType::Sphere:
		world.radius = shape.radius * (std::max)(scaleXZ, scaleY);
		break;
	case HitShapeType::Capsule:
		world.radius = shape.radius * (std::max)(scaleX, scaleY);
		world.length = shape.length * scaleZ;
		break;
	}
	world.height = shape.height * scaleY;
	// 円は「開き 360 度の扇」として同じ式で扱う
	const float angle = (shape.type == HitShapeType::Sector) ? std::clamp(shape.angle, 0.0f, 360.0f) : 360.0f;
	world.halfAngle = angle * 0.5f * (std::numbers::pi_v<float> / 180.0f);
	return world;
}


//=============================================================================
// 座標
//=============================================================================
Vector3 WorldHitShape::ToLocal(const Vector3& point) const {
	const Vector3 offset = point - origin;
	return {Dot(offset, right), Dot(offset, up), Dot(offset, forward)};
}

Vector3 WorldHitShape::ToWorld(const Vector3& local) const { return origin + right * local.x + up * local.y + forward * local.z; }


//=============================================================================
// 符号付き距離
//=============================================================================
// ===== 床の面の中 =====
float WorldHitShape::CalcSignedDistance2D(const Vector2& p) const {
	switch (type) {
	case HitShapeType::Circle:
	case HitShapeType::Sector:
		return CalcSectorDistance(p, radius, innerRadius, halfAngle);
	case HitShapeType::Line:
		return CalcLineDistance(p, width, length);
	case HitShapeType::Sphere:
		return Length(p) - radius; // y = 0 で切った断面
	case HitShapeType::Capsule:
		return Length(Vector2{p.x, p.y - std::clamp(p.y, 0.0f, length)}) - radius;
	}
	return 0.0f;
}

// ===== 3D =====
float WorldHitShape::CalcSignedDistance(const Vector3& world) const {
	const Vector3 p = ToLocal(world);
	switch (type) {
	case HitShapeType::Circle:
	case HitShapeType::Sector:
	case HitShapeType::Line:
		return Extrude(CalcSignedDistance2D({p.x, p.z}), p.y, height);
	case HitShapeType::Sphere:
		return Length(p) - radius;
	case HitShapeType::Capsule:
		return Length(Vector3{p.x, p.y, p.z - std::clamp(p.z, 0.0f, length)}) - radius;
	}
	return 0.0f;
}

// ===== 外へ向かう向き（x・y・z をそれぞれ少しずらしたときの、距離の増え方）=====
Vector3 WorldHitShape::CalcNormal(const Vector3& world) const {
	const Vector3 dx = {kNormalEpsilon, 0.0f, 0.0f};
	const Vector3 dy = {0.0f, kNormalEpsilon, 0.0f};
	const Vector3 dz = {0.0f, 0.0f, kNormalEpsilon};
	const Vector3 gradient = {
	    CalcSignedDistance(world + dx) - CalcSignedDistance(world - dx),
	    CalcSignedDistance(world + dy) - CalcSignedDistance(world - dy),
	    CalcSignedDistance(world + dz) - CalcSignedDistance(world - dz),
	};
	return NormalizeOr(gradient, up);
}


//=============================================================================
// 重なり
//=============================================================================
// ===== 球 =====
bool WorldHitShape::Overlaps(const Sphere& sphere, float margin) const { return CalcSignedDistance(sphere.center) <= sphere.radius + margin; }

// ===== カプセル =====
bool WorldHitShape::Overlaps(const Capsule& capsule, float margin, Vector3* nearest) const {
	// 点を並べる間隔は半径まで（それより広いと、細い形の間を飛び越える）
	const Vector3 point = FindNearestOnSegment(capsule.start, capsule.end, (std::max)(capsule.radius, 0.01f));
	if (nearest != nullptr) {
		*nearest = point;
	}
	return CalcSignedDistance(point) <= capsule.radius + margin;
}

// ===== 線分の上で、形に一番近い点 =====
// 1. 線分の上に、間隔 sampleStep 以下で点を並べて距離を測る
// 2. 両隣より小さい所（谷）ごとに、両隣の間を黄金分割探索で細かく探す（形が凸なら、谷は1つで、ちょうどの答えになる）
Vector3 WorldHitShape::FindNearestOnSegment(const Vector3& start, const Vector3& end, float sampleStep) const {
	const float segmentLength = Length(end - start);
	if (segmentLength <= kMinAxisLength) {
		return start;
	}
	const int intervals = std::clamp(static_cast<int>(std::ceil(segmentLength / sampleStep)), 1, kMaxSegmentSamples - 1);
	const auto pointAt = [&](float t) { return start + (end - start) * t; };
	const auto distanceAt = [&](float t) { return CalcSignedDistance(pointAt(t)); };

	// --- 1. 並べて測る ---
	std::array<float, kMaxSegmentSamples> values{};
	for (int i = 0; i <= intervals; ++i) {
		values[i] = distanceAt(static_cast<float>(i) / static_cast<float>(intervals));
	}

	// --- 2. 谷ごとに細かく探す ---
	float bestT = 0.0f;
	float bestValue = values[0];
	for (int i = 0; i <= intervals; ++i) {
		const bool isValley = (i == 0 || values[i] <= values[i - 1]) && (i == intervals || values[i] <= values[i + 1]);
		if (!isValley) {
			continue;
		}
		// 黄金分割探索：区間 [low, high] の中の2点を比べて、大きい方の外側を捨てる。1回ごとに区間が 0.618 倍になる
		float low = static_cast<float>((std::max)(i - 1, 0)) / static_cast<float>(intervals);
		float high = static_cast<float>((std::min)(i + 1, intervals)) / static_cast<float>(intervals);
		float a = high - (high - low) * kInvGoldenRatio;
		float b = low + (high - low) * kInvGoldenRatio;
		float valueA = distanceAt(a);
		float valueB = distanceAt(b);
		for (int iteration = 0; iteration < kGoldenIterations; ++iteration) {
			if (valueA <= valueB) {
				high = b;
				b = a;
				valueB = valueA;
				a = high - (high - low) * kInvGoldenRatio;
				valueA = distanceAt(a);
			} else {
				low = a;
				a = b;
				valueA = valueB;
				b = low + (high - low) * kInvGoldenRatio;
				valueB = distanceAt(b);
			}
		}
		const float t = (valueA <= valueB) ? a : b;
		const float value = (std::min)(valueA, valueB);
		// 並べた点そのものの方が小さければ、そちら（端の点など）
		if (values[i] < value && values[i] < bestValue) {
			bestValue = values[i];
			bestT = static_cast<float>(i) / static_cast<float>(intervals);
		} else if (value < bestValue) {
			bestValue = value;
			bestT = t;
		}
	}
	return pointAt(bestT);
}
```

### B-3 `HTTEngine.vcxproj`

| フィルター | 足すファイル |
|---|---|
| `HTTEngine\Math\Geometry` | `HitShape.h`・`HitShape.cpp` |

## パートC 当たりの層（Hitbox・Hurtbox・HitSystem）

### C-1 `Combat/HitInfo.h`（新しい）

下半分（ダメージの層の欄）はパート D の DamageSystem が書く。C まででは初めの値のまま。

```cpp
#pragma once
#include <cstdint>

#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector3.h"

/// <summary>
/// 当たったときの扱い（受ける側の Script の OnBeforeHitTaken で変えられる）
/// </summary>
enum class HitResult {
	Hit,     // 当たった（HP が減る・ヒットストップと揺れが出る）
	Guarded, // 防いだ（当たりは記録する＝同じ攻撃の回ではもう当たらない。HP は damage だけ減る。ヒットストップと揺れは出ない）
	Ignored, // 断った（記録しない。無敵と同じで、同じ攻撃の回のうちに、後のフレームでもう一度当たることがある）
};


/// <summary>
/// 攻撃が当たった1件（当てた側の OnHitDealt・受けた側の OnHitTaken に渡す。SYSTEM は HitSystem::GetAllHits で読む）
/// <para>上半分は当たりの層（HitSystem）、下半分はダメージの層（DamageSystem）が書く</para>
/// </summary>
struct HitInfo {
	// ===== 当たりの層（HitSystem）=====
	Handle<Entity> hitbox;          // 当てた Hitbox の Entity
	Handle<Entity> instigator;      // 撃った本人（Hitbox の Instigator。空なら Hitbox の Entity と同じ）
	Handle<Entity> receiver;        // 受けた本体（Health の付いた一番近い親か自分。無ければ Hurtbox の Entity）
	Handle<Entity> hurtbox;         // 当たった Hurtbox の Entity
	uint32_t attackSerial = 0;      // 攻撃の回の番号（Hitbox ごとに、Active になるたびに 1 増える）
	Vector3 point = {0.0f, 0.0f, 0.0f};     // 当たった位置（Hurtbox の芯の、攻撃に一番近い点から、攻撃の形の縁まで寄せた所。芯が形の中なら芯の点。ダメージの数字を出す所）
	Vector3 direction = {0.0f, 0.0f, 1.0f}; // 攻撃の向き（長さ1。動いている攻撃は動いた向き、止まっている攻撃は形の真ん中から外へ）
	float distance = 0.0f;          // Active になった所から、当たった所までの距離（m。弾なら撃った所から）
	float hurtboxMultiplier = 1.0f; // 当たった Hurtbox の倍率（弱点なら 1 より大きい）

	// ===== ダメージの層（DamageSystem）=====
	HitResult result = HitResult::Hit; // 当たったときの扱い
	int damageType = 0;                // 攻撃の種類（Settings の Damage Types の番号。DamageSource が無ければ 0）
	float damage = 0.0f;               // ダメージの量（Damage × Hurtbox の倍率 × 距離の倍率。OnBeforeHitTaken で書き換えた後の値。残りの HP より大きくてもそのまま）
	bool isDefeated = false;           // この当たりで HP が 0 になった
	float healthAfter = 0.0f;          // 当たった後の HP（Health が無ければ 0）
};
```

### C-2 `Components/Combat/HitboxComponent.h`（新しい）

```cpp
#pragma once
#include <cstdint>
#include <type_traits>

#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/ECS/Layers.h"
#include "HTTEngine/Math/Geometry/HitShape.h"

// 攻撃の当たる形と、当たる時間の窓。Entity に付ける Component（カテゴリ：Combat。当たりの層）
// 当たったら HitSystem が「当たりの知らせ」を作る（Script の OnHitDealt・OnHitTaken、SYSTEM は HitSystem::GetAllHits）
// ダメージの量・種類・手応えは同じ Entity の Damage Source（ダメージの層）。無ければ、当たりの知らせだけ来る
// 窓を開け閉めするのは呼ぶ側（攻撃部品・Timeline・Script・SYSTEM）。HitSystem::Warn・Activate・Deactivate か、phase を書く
// 形の値（Shape）は、当たり・ジャスト判定の範囲・床の予告の図形が同じ物を使う（ずれない）

/// <summary>
/// 当たる時間の窓の段階
/// </summary>
enum class HitboxPhase {
	Off,     // 何もしない（当たらない。ジャスト判定の範囲も無い）
	Warning, // 予告（当たらない。床の予告の図形を出す。Active になる Just Lead Time 秒前から、ジャスト判定の範囲が有効）
	Active,  // 当たる（Active になるたびに、新しい攻撃の回になる＝当てた相手の記録が空になる）
};


/// <summary>
/// 攻撃の当たる形と窓
/// </summary>
struct HitboxComponent {
	HitShape shape;                               // 当たる形（ジャスト判定の範囲・床の予告の図形も、これを使う）
	LayerMask targets = LayerMask::Everything();  // 当てる相手のレイヤー（相手の Hurtbox の Entity のレイヤーで見る）
	EntityRef instigator;                         // 撃った本人（空ならこの Entity）。本人とその親子には当たらない。弾は撃った物が作るときに書く

	// --- 窓（保存した値が、Play したとき・Prefab から作ったときの段階になる）---
	HitboxPhase phase = HitboxPhase::Off; // 今の段階
	float warningSeconds = 0.0f;          // Warning の長さ（秒。0 ならずっと Warning）
	bool activateAfterWarning = false;    // Warning が終わったら Active にする
	float activeSeconds = 0.0f;           // Active の長さ（秒。0 ならずっと Active）

	// --- 何回当たるか ---
	float rehitInterval = 0.0f; // 同じ相手にもう一度当たるまでの秒（0 なら、1回の攻撃で1回だけ。正なら多段ヒット）
	int maxTargets = 0;         // 1回の攻撃で当てられる相手の数（0 なら何体でも。弾は 1。当てきったら Off になる）

	// --- ジャスト判定（HitSystem::TryConsumeJust）---
	bool justEnabled = true;   // ジャスト判定に使うか（false ならジャストにならない当たり。弾・ジャストを見せたくない攻撃）
	float justMargin = 0.5f;   // ジャスト判定の範囲：形をこの距離だけ太らせた範囲（m。0 なら形そのものの中だけ）
	float justLeadTime = 0.3f; // Active になる何秒前から、ジャスト判定の範囲が有効か（Warning の長さが 0 なら、Warning の間ずっと）

	// --- 実行中だけの値（保存しない。HitSystem が書く）---
	float phaseTime = 0.0f;    // 今の段階に入ってからの秒
	uint32_t attackSerial = 0; // 攻撃の回の番号（Active になるたびに 1 増える）
	int hitCount = 0;          // この攻撃の回で当てた相手の数

	// 当てきった（Max Targets まで当てた。弾を消す合図）
	bool IsSpent() const { return maxTargets > 0 && hitCount >= maxTargets; }
	// Warning の残りの秒（Warning の長さが 0 なら 0）
	float GetWarningRemaining() const { return (phase == HitboxPhase::Warning && warningSeconds > phaseTime) ? warningSeconds - phaseTime : 0.0f; }
};

static_assert(std::is_trivially_copyable_v<HitboxComponent>);
```

### C-3 `Components/Combat/HurtboxComponent.h`（新しい）

```cpp
#pragma once
#include <type_traits>

#include "HTTEngine/Math/Vector3.h"

// 攻撃を受ける形（やられ判定）。Entity に付ける Component（カテゴリ：Combat。当たりの層）
// 押し合う形（Collider）とは別（見た目より小さくする・体の部分ごとに分けるため）。部分ごとに分けるときは、子の Entity に1つずつ付ける
// どのチームかは、付けた Entity のレイヤー（Hitbox の Targets で絞られる）
// 受ける本体は「Health の付いた一番近い親か自分」（無ければ自分）。頭と手の両方に入っても、1回の攻撃で当たるのは1回

/// <summary>
/// やられ判定の形
/// </summary>
enum class HurtboxShape {
	Sphere,  // 球
	Capsule, // カプセル（ローカルの上 +Y に立つ。Entity と一緒に回る）
};


/// <summary>
/// 攻撃を受ける形
/// </summary>
struct HurtboxComponent {
	bool enabled = true;                         // false の間は当たらない（外さずに、一時的に止めたいとき）
	HurtboxShape shape = HurtboxShape::Capsule;  // 形
	Vector3 center = {0.0f, 0.9f, 0.0f};         // Entity の位置からのずれ（ローカル。回転と大きさも掛かる）
	float radius = 0.4f;                         // 半径（Entity の大きさの X と Z の大きい方を掛ける。球は一番大きい軸）
	float height = 1.8f;                         // カプセルの高さ（下の端から上の端まで。半球も入れる。Entity の Y の大きさを掛ける）
	float damageMultiplier = 1.0f;               // ダメージの倍率（弱点なら 1 より大きく、硬い所は小さく）
};

static_assert(std::is_trivially_copyable_v<HurtboxComponent>);
```

### C-4 `Systems/Combat/HitSystem.h`（新しい）

```cpp
#pragma once
#include <cstdint>
#include <span>
#include <vector>

#include "HTTEngine/Combat/HitInfo.h"
#include "HTTEngine/Components/Combat/HitboxComponent.h"
#include "HTTEngine/Components/Combat/HurtboxComponent.h"
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Geometry/Capsule.h"
#include "HTTEngine/Math/Geometry/HitShape.h"

// 前方宣言
class Camera;
struct TransformComponent;


/// <summary>
/// ジャスト判定で見つかった攻撃（HitSystem::FindJust・TryConsumeJust）
/// </summary>
struct JustCandidate {
	Handle<Entity> hitbox;     // その攻撃の Hitbox の Entity
	Handle<Entity> instigator; // 撃った本人
	uint32_t attackSerial = 0; // その攻撃の回の番号（Warning の間なら、これから始まる回の番号）
	float timeToActive = 0.0f; // Active になるまでの秒（Active なら 0）
};


/// <summary>
/// 攻撃の当たり（当たりの層）。Hitbox の窓を時間で進め、Active の Hitbox と Hurtbox の重なりを調べて「当たりの知らせ」を作る
/// <para>調べる順：段階を進める → 攻撃の回が変わったら記録を空にする → 当たり（動いた跡も調べる＝すり抜けない）→ ダメージの層に聞く → 記録して知らせる</para>
/// <para>受ける本体を決める・受けるかを決めるのはダメージの層（DamageSystem が Initialize で答え方を登録する）。登録が無ければ、Hurtbox の Entity が受けて、いつも受ける</para>
/// <para>1フレームの順番：…→ 破棄の反映 → CollisionSystem::Update → DamageSystem::Update → ここ。知らせは次のフレームの Script（OnHitDealt・OnHitTaken）・SYSTEM が読む</para>
/// </summary>
class HitSystem {
public:
	// Hitbox・Hurtbox を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// 窓を進めて、当たりを調べる（再生中だけ。時間が止まっている間は何もしない）
	/// </summary>
	/// <param name="isGameRunning">再生中か（停止中は実行中の値を全部消す）</param>
	/// <param name="deltaTime">経過秒（Time::GetDeltaTime。ヒットストップ・一時停止の間は 0）</param>
	static void Update(bool isGameRunning, float deltaTime);

	// ===== 窓を開け閉めする（攻撃部品・Timeline・Script・SYSTEM から。今のフレームの Update から効く）=====
	// 予告を始める（warningSeconds 秒たっても Warning のまま。Active へは呼ぶ側が進める。0 ならずっと）
	static void Warn(Handle<Entity> hitbox, float warningSeconds);
	// 予告を始めて、warningSeconds 秒後に Active にし、さらに activeSeconds 秒後に Off にする（0 ならずっと Active）
	static void Warn(Handle<Entity> hitbox, float warningSeconds, float activeSeconds);
	// すぐ当たる窓を開ける（いつも新しい攻撃の回＝当てた相手の記録が空になる）。activeSeconds 秒後に Off（0 ならずっと）
	static void Activate(Handle<Entity> hitbox, float activeSeconds = 0.0f);
	// 窓を閉じる
	static void Deactivate(Handle<Entity> hitbox);

	// ===== 当たりの知らせ（最後の Update で当たった物。次の Update まで変わらない）=====
	static std::span<const HitInfo> GetAllHits();
	// entity が当てた物（Hitbox の Entity か撃った本人が entity）を out に入れる（最初に空にする）
	static void GetHitsDealt(Handle<Entity> entity, std::vector<HitInfo>& out);
	// receiver が受けた物を out に入れる（最初に空にする）
	static void GetHitsTaken(Handle<Entity> receiver, std::vector<HitInfo>& out);

	// ===== ジャスト判定（Script の Update から。位置は前のフレームの終わりの物）=====
	/// <summary>
	/// 受ける本体の Hurtbox が、有効なジャスト判定の範囲に入っている攻撃を探す（記録はしない）
	/// <para>有効なのは「Warning で Active まで Just Lead Time 秒以内（Warning の長さが 0 ならずっと）」か「Active」の Hitbox。
	/// 当てる相手のレイヤーに入っていて、その攻撃の回でまだ当たっていない・よけていない物だけ。いくつもあれば Active に一番近い物</para>
	/// </summary>
	static bool FindJust(Handle<Entity> receiver, JustCandidate& out);
	/// <summary>
	/// FindJust で見つかった攻撃を「よけた」と記録する（同じ攻撃の回では、もうジャストにならない・もう当たらない）
	/// </summary>
	/// <returns>見つかったか（見つからなければ何も記録しない）</returns>
	static bool TryConsumeJust(Handle<Entity> receiver, JustCandidate& out);

	// ===== 形（ワールド座標）=====
	// Hitbox の形をワールドに置く（Transform のワールド行列を使う）
	static WorldHitShape CalcWorldShape(const HitboxComponent& hitbox, const TransformComponent& transform);
	// Hurtbox をワールドのカプセルにする（球は start と end が同じカプセル）
	static Capsule CalcWorldHurtbox(const HurtboxComponent& hurtbox, const TransformComponent& transform);
	// Hurtbox の受ける本体（ダメージの層に聞く。答える係が無ければ hurtbox 自身）
	static Handle<Entity> FindReceiver(Handle<Entity> hurtbox);

	// ===== 答える係（ダメージの層が Initialize で登録する。当たりの層はダメージの層の型を知らない）=====
	// 受ける本体を決める
	using ReceiverFinder = Handle<Entity> (*)(Handle<Entity> hurtbox);
	// 当たり1件を受けるか決めて、ダメージの欄を書く。false なら断った（記録しない）
	using HitResolver = bool (*)(HitInfo& hit);
	static void SetReceiverFinder(ReceiverFinder finder);
	static void SetHitResolver(HitResolver resolver);

	// ===== ギズモ（エディタ用）=====
	// Hitbox の形（段階で色が変わる）・ジャスト判定の範囲・Hurtbox を線で描く
	static void DrawGizmos(Camera* camera);
	// 線を出すか（メニューバーの View から切り替える）
	static bool& GetShowGizmos();

private:
	// Entity が消える直前（EntityManager から呼ばれる）。実行中の値を片付ける
	static void OnEntityDestroyed(Handle<Entity> entity);
};
```

### C-5 `Systems/Combat/HitSystem.cpp`（新しい）

```cpp
#include "HitSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <vector>

#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/ECS/Layers.h"
#include "HTTEngine/Editor/Gizmos/GizmoLines.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Graphics/Renderer/Renderer.h"

namespace {
// ===== すり抜けない判定 =====
constexpr float kMinMotion = 1.0e-4f;   // これより短い動きは、動いていないとみなす（m）
constexpr float kMinSweepStep = 0.01f;  // 動いた跡を調べる間隔の下限（m）
constexpr int kMaxSweepSteps = 32;      // 動いた跡を何か所まで調べるか（これより速いと、間隔が Hurtbox の半径より広くなる）
constexpr float kMaxHurtboxMotion = 5.0f; // 1フレームにこれより大きく動いた Hurtbox は、ワープ（置き直し）とみなして跡を調べない（m。60fps で 300m/s）

// ===== ギズモ =====
constexpr int kContourCells = 40;         // 床に置く形の縁を探す格子の、1辺のます目の数
constexpr float kContourPadding = 0.05f;  // 格子を形より少し広くする割合（縁が格子の端にかからないように）
constexpr int kVerticalLineEvery = 6;     // 縁の線分いくつごとに、縦の線（柱の高さ）を1本引くか
constexpr float kNoFadeStart = 10000.0f;  // 距離でフェードさせないための値（Collider と同じ）
constexpr float kNoFadeEnd = 20000.0f;
constexpr Vector4 kOffColor = {0.6f, 0.6f, 0.6f, 0.5f};       // Off の Hitbox（灰色）
constexpr Vector4 kWarningColor = {1.0f, 0.85f, 0.1f, 1.0f};  // Warning の Hitbox（黄色）
constexpr Vector4 kActiveColor = {1.0f, 0.2f, 0.2f, 1.0f};    // Active の Hitbox（赤）
constexpr Vector4 kJustColor = {0.2f, 0.9f, 1.0f, 1.0f};      // ジャスト判定の範囲（水色）
constexpr Vector4 kJustOffColor = {0.2f, 0.9f, 1.0f, 0.35f};  // Off の間のジャスト判定の範囲（うすい水色）
constexpr Vector4 kHurtboxColor = {0.4f, 1.0f, 0.5f, 1.0f};   // Hurtbox（緑）

//=============================================================================
// 実行中の値
//=============================================================================
/// <summary>
/// 攻撃の回1つの中で、相手1体に何をしたかの記録
/// </summary>
struct HitRecord {
	Handle<Entity> receiver;  // 受ける本体
	uint32_t serial = 0;      // どの攻撃の回の記録か（Warning の間にジャストでよけた物は、これから始まる回の番号）
	float lastHitTime = 0.0f; // 最後に当てた時刻（攻撃の回が始まってからの秒。多段ヒットの間隔に使う）
	bool isHit = false;       // 当てた
	bool isDodged = false;    // ジャストでよけられた（もう当てない・もうジャストにならない）
};

/// <summary>
/// Hitbox 1つの実行中の値（数が変わる記録を持つので、バイト列で写す Component には入れない）
/// </summary>
struct HitboxRuntime {
	HitboxPhase lastPhase = HitboxPhase::Off;   // 前の Update の段階（Off → Active を見つけて、新しい攻撃の回にする）
	uint32_t serial = 0;                        // 記録が今どの攻撃の回か
	float attackTime = 0.0f;                    // 攻撃の回が始まってからの秒
	Vector3 activationOrigin = {0.0f, 0.0f, 0.0f}; // Active になった所（距離の倍率に使う）
	Vector3 previousOrigin = {0.0f, 0.0f, 0.0f};   // 前のフレームの形の中心（動いた跡を調べる）
	bool hasPrevious = false;                   // previousOrigin が使える（同じ攻撃の回で、前のフレームも Active だった）
	std::vector<HitRecord> records;             // 相手ごとの記録
	uint32_t seenFrame = 0;                     // 最後に見つけたフレーム（外された Hitbox を片付ける）
};

/// <summary>
/// 集めたやられ判定1つ（Update の初めに、今の Transform で作る）
/// </summary>
struct WorldHurtbox {
	Handle<Entity> entity;   // Hurtbox の Entity
	Handle<Entity> receiver; // 受ける本体
	Capsule capsule{};       // 形（ワールド座標）
	int layer = 0;           // Entity のレイヤー
	float multiplier = 1.0f; // ダメージの倍率
	Vector3 motion = {0.0f, 0.0f, 0.0f}; // 前のフレームからの動き（すり抜けない判定で、相手の動きを引く）
};

/// <summary>
/// 前のフレームのやられ判定の位置
/// </summary>
struct HurtboxHistory {
	Vector3 center = {0.0f, 0.0f, 0.0f};
	uint32_t seenFrame = 0;
};

/// <summary>
/// HitSystem が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct HitState {
	std::unordered_map<uint64_t, HitboxRuntime> hitboxes;   // Hitbox ごとの実行中の値（キーは Handle）
	std::unordered_map<uint64_t, HurtboxHistory> histories; // Hurtbox ごとの前のフレームの位置
	std::vector<WorldHurtbox> hurtboxes; // このフレームのやられ判定
	std::vector<HitInfo> hits;           // このフレームの当たり
	uint32_t frame = 0;                  // Update を呼んだ回数（片付けに使う）
	HitSystem::ReceiverFinder receiverFinder = nullptr; // 受ける本体を決める係（ダメージの層）
	HitSystem::HitResolver hitResolver = nullptr;       // 受けるか決める係（ダメージの層）
	bool showGizmos = true;              // ギズモを描くか
	Renderer::LineListConfig gizmoLines; // ギズモの線（使い回す）
};
std::unique_ptr<HitState> state;

// Handle を1つの数にする（実行中の値の表のキー）
uint64_t KeyOf(Handle<Entity> entity) { return (static_cast<uint64_t>(entity.index) << 32) | entity.generation; }

// 撃った本人（Instigator が空・居なければ、Hitbox の Entity）
Handle<Entity> FindInstigator(Handle<Entity> hitboxEntity, const HitboxComponent& hitbox) {
	const Handle<Entity> instigator = EntityManager::Find(hitbox.instigator);
	return instigator.IsValid() ? instigator : hitboxEntity;
}

// 自分・撃った本人・その親子の Hurtbox には当てない
bool IsOwnSide(const WorldHurtbox& hurtbox, Handle<Entity> hitboxEntity, Handle<Entity> instigator) {
	return EntityManager::IsRelated(hurtbox.entity, hitboxEntity) || EntityManager::IsRelated(hurtbox.entity, instigator) || EntityManager::IsRelated(hurtbox.receiver, instigator);
}

// 記録を探す（無ければ nullptr）
HitRecord* FindRecord(HitboxRuntime& runtime, Handle<Entity> receiver, uint32_t serial) {
	const auto it = std::ranges::find_if(runtime.records, [&](const HitRecord& record) { return record.receiver == receiver && record.serial == serial; });
	return (it != runtime.records.end()) ? &*it : nullptr;
}

//=============================================================================
// 窓
//=============================================================================
// ===== Active にする（新しい攻撃の回）=====
void BeginActive(HitboxComponent& hitbox, float activeSeconds) {
	hitbox.phase = HitboxPhase::Active;
	hitbox.activeSeconds = activeSeconds;
	hitbox.phaseTime = 0.0f;
	++hitbox.attackSerial;
	hitbox.hitCount = 0;
}

// ===== Warning にする（activateAfterWarning なら、warningSeconds 後に activeSeconds の Active へ）=====
void BeginWarning(HitboxComponent& hitbox, float warningSeconds, bool activateAfterWarning, float activeSeconds) {
	hitbox.phase = HitboxPhase::Warning;
	hitbox.warningSeconds = (std::max)(warningSeconds, 0.0f);
	hitbox.activateAfterWarning = activateAfterWarning;
	hitbox.activeSeconds = (std::max)(activeSeconds, 0.0f);
	hitbox.phaseTime = 0.0f;
}

// ===== 時間で Warning → Active に進める（Active → Off は、当たりを調べた後）=====
void AdvanceWarning(HitboxComponent& hitbox, float deltaTime) {
	hitbox.phaseTime += deltaTime;
	if (hitbox.phase == HitboxPhase::Warning && hitbox.activateAfterWarning && hitbox.warningSeconds > 0.0f && hitbox.phaseTime >= hitbox.warningSeconds) {
		const float over = hitbox.phaseTime - hitbox.warningSeconds; // 越えた分は、Active の時間に入れる
		BeginActive(hitbox, hitbox.activeSeconds);
		hitbox.phaseTime = over;
	}
}

// ===== 時間で Active → Off に進める（どんなに短い窓でも、1フレームは当たりを調べてから閉じる）=====
void AdvanceActive(HitboxComponent& hitbox) {
	if (hitbox.phase == HitboxPhase::Active && hitbox.activeSeconds > 0.0f && hitbox.phaseTime >= hitbox.activeSeconds) {
		hitbox.phase = HitboxPhase::Off;
		hitbox.phaseTime = 0.0f;
	}
}

//=============================================================================
// やられ判定を集める
//=============================================================================
void CollectHurtboxes() {
	state->hurtboxes.clear();
	EntityManager::ForEach<HurtboxComponent>([](Handle<Entity> entity, HurtboxComponent& hurtbox) {
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		const Entity* owner = EntityManager::Get(entity);
		if (!hurtbox.enabled || transform == nullptr || owner == nullptr || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		WorldHurtbox world;
		world.entity = entity;
		world.receiver = HitSystem::FindReceiver(entity);
		world.capsule = HitSystem::CalcWorldHurtbox(hurtbox, *transform);
		world.layer = owner->layer;
		world.multiplier = hurtbox.damageMultiplier;
		// 前のフレームからの動き（初めて見つけた Hurtbox・ワープした Hurtbox は、動いていないとみなす。通っていない所の攻撃に当たらないように）
		const Vector3 center = (world.capsule.start + world.capsule.end) * 0.5f;
		HurtboxHistory& history = state->histories[KeyOf(entity)];
		const Vector3 motion = center - history.center;
		const bool isContinuous = (history.seenFrame + 1 == state->frame) && Length(motion) <= kMaxHurtboxMotion;
		world.motion = isContinuous ? motion : Vector3{0.0f, 0.0f, 0.0f};
		history.center = center;
		history.seenFrame = state->frame;
		state->hurtboxes.push_back(world);
	});
	// 見つからなかった Hurtbox の位置は捨てる（外された・無効になった）
	std::erase_if(state->histories, [](const auto& item) { return item.second.seenFrame != state->frame; });
}

//=============================================================================
// 当たりを調べる
//=============================================================================
/// <summary>
/// 動いた跡を調べた結果
/// </summary>
struct SweepResult {
	WorldHitShape shape;                  // 当たった時の形の位置
	Vector3 nearest = {0.0f, 0.0f, 0.0f}; // Hurtbox の線分の上で、形に一番近い点
	float fraction = 1.0f;                // 動きの何割で当たったか（0＝前のフレームの位置、1＝今の位置）
};

// ===== 形を、前のフレームの位置から今の位置まで少しずつ動かして、最初に重なる所を探す =====
// relativeMotion は Hurtbox から見た形の動き（形の動き − Hurtbox の動き）。Hurtbox は今の位置に置いたまま、形の方を戻して調べる
// 間隔は Hurtbox の半径まで（それより広いと、細い形がその間を飛び越える）
bool Sweep(const WorldHitShape& shape, const Capsule& capsule, const Vector3& relativeMotion, SweepResult& result) {
	const float motionLength = Length(relativeMotion);
	if (motionLength < kMinMotion) {
		result.shape = shape;
		result.fraction = 1.0f;
		return shape.Overlaps(capsule, 0.0f, &result.nearest);
	}
	const int steps = std::clamp(static_cast<int>(std::ceil(motionLength / (std::max)(capsule.radius, kMinSweepStep))), 1, kMaxSweepSteps);
	for (int i = 0; i <= steps; ++i) {
		const float fraction = static_cast<float>(i) / static_cast<float>(steps);
		WorldHitShape moved = shape;
		moved.origin = shape.origin - relativeMotion * (1.0f - fraction);
		if (moved.Overlaps(capsule, 0.0f, &result.nearest)) {
			result.shape = moved;
			result.fraction = fraction;
			return true;
		}
	}
	return false;
}

// ===== 当たり1件の知らせを作る（当たりの層の欄だけ）=====
HitInfo MakeHitInfo(Handle<Entity> hitboxEntity, Handle<Entity> instigator, const HitboxComponent& hitbox, const HitboxRuntime& runtime, const WorldHurtbox& hurtbox,
                    const SweepResult& sweep, const Vector3& relativeMotion) {
	HitInfo hit;
	hit.hitbox = hitboxEntity;
	hit.instigator = instigator;
	hit.receiver = hurtbox.receiver;
	hit.hurtbox = hurtbox.entity;
	hit.attackSerial = hitbox.attackSerial;
	hit.hurtboxMultiplier = hurtbox.multiplier;
	// --- 当たった位置：線分の点から、形の方へ半径だけ出した所（Hurtbox の表面）。線分が形の中なら、線分の点 ---
	const WorldHitShape& shape = sweep.shape;
	const float distance = shape.CalcSignedDistance(sweep.nearest);
	const Vector3 normal = shape.CalcNormal(sweep.nearest);
	hit.point = sweep.nearest - normal * std::clamp(distance, 0.0f, hurtbox.capsule.radius);
	// --- 向き：動いている攻撃は動いた向き。止まっている攻撃は、形の真ん中から当たった位置へ（床に置く形は床に平らに）---
	if (Length(relativeMotion) >= kMinMotion) {
		hit.direction = Normalize(relativeMotion);
	} else {
		Vector3 outward = hit.point - shape.origin;
		if (IsFloorShape(shape.type)) {
			outward -= shape.up * Dot(outward, shape.up);
		}
		hit.direction = (Length(outward) >= kMinMotion) ? Normalize(outward) : shape.forward;
	}
	hit.distance = Length(hit.point - runtime.activationOrigin);
	return hit;
}

/// <summary>
/// 当たりの候補（受ける本体1体につき1つ）
/// </summary>
struct HitCandidate {
	const WorldHurtbox* hurtbox = nullptr; // 一番倍率の大きい Hurtbox
	SweepResult sweep;                     // その Hurtbox との当たり
	Vector3 relativeMotion = {0.0f, 0.0f, 0.0f};
};

// ===== Active の Hitbox 1つの当たりを調べて、記録して知らせる =====
void DetectHits(Handle<Entity> entity, HitboxComponent& hitbox, HitboxRuntime& runtime, const WorldHitShape& shape) {
	const Handle<Entity> instigator = FindInstigator(entity, hitbox);
	// 窓が開いた最初のフレームは、跡を調べない（形も Hurtbox も。開く前の位置で当たらないように）
	const bool canSweep = runtime.hasPrevious;
	const Vector3 shapeMotion = canSweep ? shape.origin - runtime.previousOrigin : Vector3{0.0f, 0.0f, 0.0f};

	// --- 1. 重なっている Hurtbox を、受ける本体ごとに1つ選ぶ（頭と手の両方に入っても1回。倍率の大きい方）---
	static std::vector<HitCandidate> candidates; // 毎フレーム配列を作り直さないように使い回す
	candidates.clear();
	for (const WorldHurtbox& hurtbox : state->hurtboxes) {
		if (!hitbox.targets.Contains(hurtbox.layer) || IsOwnSide(hurtbox, entity, instigator)) {
			continue;
		}
		HitCandidate candidate;
		candidate.hurtbox = &hurtbox;
		candidate.relativeMotion = canSweep ? shapeMotion - hurtbox.motion : Vector3{0.0f, 0.0f, 0.0f};
		if (!Sweep(shape, hurtbox.capsule, candidate.relativeMotion, candidate.sweep)) {
			continue;
		}
		const auto same = std::ranges::find_if(candidates, [&](const HitCandidate& other) { return other.hurtbox->receiver == hurtbox.receiver; });
		if (same == candidates.end()) {
			candidates.push_back(candidate);
		} else if (hurtbox.multiplier > same->hurtbox->multiplier) {
			*same = candidate;
		}
	}
	// 動いた跡の、先に触れた相手から（弾が2体に重なっても、手前の1体に当たる）
	std::ranges::sort(candidates, {}, [](const HitCandidate& candidate) { return candidate.sweep.fraction; });

	// --- 2. 記録を見て、当てる ---
	for (const HitCandidate& candidate : candidates) {
		const Handle<Entity> receiver = candidate.hurtbox->receiver;
		HitRecord* record = FindRecord(runtime, receiver, runtime.serial);
		if (record != nullptr && record->isDodged) {
			continue; // ジャストでよけられた
		}
		const bool isFirstHit = (record == nullptr || !record->isHit);
		if (!isFirstHit && (hitbox.rehitInterval <= 0.0f || runtime.attackTime - record->lastHitTime < hitbox.rehitInterval)) {
			continue; // もう当てた（多段ヒットなら、間隔がたっていない）
		}
		if (isFirstHit && hitbox.IsSpent()) {
			continue; // 当てられる相手の数を使いきった
		}
		HitInfo hit = MakeHitInfo(entity, instigator, hitbox, runtime, *candidate.hurtbox, candidate.sweep, candidate.relativeMotion);
		// ダメージの層に聞く（断られたら記録しない＝無敵が切れた後に、まだ範囲にいれば当たる）
		if (state->hitResolver != nullptr && !state->hitResolver(hit)) {
			continue;
		}
		if (record == nullptr) {
			runtime.records.push_back({receiver, runtime.serial});
			record = &runtime.records.back();
		}
		record->isHit = true;
		record->lastHitTime = runtime.attackTime;
		if (isFirstHit) {
			++hitbox.hitCount;
		}
		state->hits.push_back(hit);
	}
	// 当てきったら閉じる（弾は Is Spent を見て消す）
	if (hitbox.IsSpent()) {
		hitbox.phase = HitboxPhase::Off;
		hitbox.phaseTime = 0.0f;
	}
}

// ===== Hitbox 1つを1フレーム進める =====
void UpdateHitbox(Handle<Entity> entity, HitboxComponent& hitbox, float deltaTime) {
	HitboxRuntime& runtime = state->hitboxes[KeyOf(entity)];
	runtime.seenFrame = state->frame;

	// --- 1. 時間で Warning → Active ---
	AdvanceWarning(hitbox, deltaTime);
	// phase を直接 Active に書いた（Timeline・Inspector）：新しい攻撃の回にする
	if (hitbox.phase == HitboxPhase::Active && runtime.lastPhase != HitboxPhase::Active && hitbox.attackSerial == runtime.serial) {
		BeginActive(hitbox, hitbox.activeSeconds);
		hitbox.phaseTime = deltaTime;
	}
	// 予告をやめた（Warning → Off）：その予告の間にジャストでよけた記録は捨てる（次の攻撃は別の攻撃）
	if (hitbox.phase == HitboxPhase::Off && runtime.lastPhase == HitboxPhase::Warning) {
		std::erase_if(runtime.records, [&](const HitRecord& record) { return record.serial == hitbox.attackSerial + 1; });
	}

	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	if (transform == nullptr || !EntityManager::IsActiveInHierarchy(entity)) {
		runtime.lastPhase = hitbox.phase;
		runtime.hasPrevious = false; // 無効の間に動いた分を、通り道とみなさない
		return;
	}
	const WorldHitShape shape = HitSystem::CalcWorldShape(hitbox, *transform);

	// --- 2. 攻撃の回が変わった：記録を空にする（Warning の間にジャストでよけた、この回の分だけ残す）---
	if (hitbox.attackSerial != runtime.serial) {
		runtime.serial = hitbox.attackSerial;
		runtime.attackTime = 0.0f;
		runtime.activationOrigin = shape.origin;
		runtime.hasPrevious = false; // 窓が開いた最初のフレームは、跡を調べない（出てきた瞬間を、通り道とみなさない）
		std::erase_if(runtime.records, [&](const HitRecord& record) { return record.serial != runtime.serial; });
	}

	// --- 3. 当たり ---
	if (hitbox.phase == HitboxPhase::Active) {
		runtime.attackTime += deltaTime;
		DetectHits(entity, hitbox, runtime, shape);
	}

	// --- 4. 時間で Active → Off ---
	AdvanceActive(hitbox);
	runtime.previousOrigin = shape.origin;
	runtime.hasPrevious = (hitbox.phase == HitboxPhase::Active);
	runtime.lastPhase = hitbox.phase;
}

//=============================================================================
// ジャスト判定
//=============================================================================
// ===== 受ける本体の Hurtbox が、有効なジャスト判定の範囲に入っている攻撃を探す =====
bool FindJustCandidate(Handle<Entity> receiver, JustCandidate& out, Handle<Entity>& foundEntity) {
	// --- 受ける本体の Hurtbox（今の Transform で）---
	struct OwnHurtbox {
		Capsule capsule;
		int layer = 0;
	};
	std::vector<OwnHurtbox> own;
	EntityManager::ForEach<HurtboxComponent>([&](Handle<Entity> entity, HurtboxComponent& hurtbox) {
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		const Entity* owner = EntityManager::Get(entity);
		if (hurtbox.enabled && transform != nullptr && owner != nullptr && EntityManager::IsActiveInHierarchy(entity) && HitSystem::FindReceiver(entity) == receiver) {
			own.push_back({HitSystem::CalcWorldHurtbox(hurtbox, *transform), owner->layer});
		}
	});
	if (own.empty()) {
		return false;
	}

	// --- 有効な Hitbox の中で、Active に一番近い物 ---
	bool isFound = false;
	EntityManager::ForEach<HitboxComponent>([&](Handle<Entity> entity, HitboxComponent& hitbox) {
		if (!hitbox.justEnabled || hitbox.phase == HitboxPhase::Off || hitbox.IsSpent()) {
			return;
		}
		// Warning は、Active になる Just Lead Time 秒前から（Warning の長さが 0 なら、いつ Active になるか分からないので、ずっと）
		const bool isWarning = (hitbox.phase == HitboxPhase::Warning);
		const float timeToActive = isWarning ? hitbox.GetWarningRemaining() : 0.0f;
		if (isWarning && hitbox.warningSeconds > 0.0f && timeToActive > hitbox.justLeadTime) {
			return;
		}
		if (isFound && timeToActive >= out.timeToActive) {
			return;
		}
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		const Handle<Entity> instigator = FindInstigator(entity, hitbox);
		if (transform == nullptr || !EntityManager::IsActiveInHierarchy(entity) || EntityManager::IsRelated(receiver, instigator) || EntityManager::IsRelated(receiver, entity)) {
			return;
		}
		// その攻撃の回で、もう当たった・よけた相手はジャストにならない（Warning の間は、これから始まる回）
		const uint32_t serial = isWarning ? hitbox.attackSerial + 1 : hitbox.attackSerial;
		if (const auto runtime = state->hitboxes.find(KeyOf(entity)); runtime != state->hitboxes.end()) {
			if (FindRecord(runtime->second, receiver, serial) != nullptr) {
				return;
			}
		}
		const WorldHitShape shape = HitSystem::CalcWorldShape(hitbox, *transform);
		const bool isInside = std::ranges::any_of(own, [&](const OwnHurtbox& hurtbox) { return hitbox.targets.Contains(hurtbox.layer) && shape.Overlaps(hurtbox.capsule, hitbox.justMargin); });
		if (!isInside) {
			return;
		}
		out = {entity, instigator, serial, timeToActive};
		foundEntity = entity;
		isFound = true;
	});
	return isFound;
}

//=============================================================================
// ギズモ
//=============================================================================
// ===== 床に置く形の縁（符号付き距離 ＝ level の線）を、格子で探して線にする（marching squares）=====
// 格子の点ごとに距離を測り、ます目の4つの角で符号が変わる辺に、縁が通る点を置いてつなぐ（形の種類を知らなくても、同じ式から描ける）
void PushFloorContour(std::vector<Renderer::LineSegment>& lines, const WorldHitShape& shape, float level, float y, float pillarHeight, const Vector4& color) {
	// --- 格子の範囲（形の縁 ＋ level が入る広さ）---
	float minX = 0.0f;
	float maxX = 0.0f;
	float minZ = 0.0f;
	float maxZ = 0.0f;
	if (shape.type == HitShapeType::Line) {
		const float halfWidth = shape.width * 0.5f + level;
		minX = -halfWidth;
		maxX = halfWidth;
		minZ = -level;
		maxZ = shape.length + level;
	} else {
		const float extent = shape.radius + level;
		minX = -extent;
		maxX = extent;
		minZ = -extent;
		maxZ = extent;
	}
	const float padX = (maxX - minX) * kContourPadding + 0.01f;
	const float padZ = (maxZ - minZ) * kContourPadding + 0.01f;
	minX -= padX;
	maxX += padX;
	minZ -= padZ;
	maxZ += padZ;

	// --- 格子の点ごとの距離（縁の上が 0）---
	constexpr int kPoints = kContourCells + 1; // 1辺の点の数
	constexpr int kCorners = 4;                // ます目の角の数
	static std::vector<float> values(kPoints * kPoints);
	const float stepX = (maxX - minX) / kContourCells;
	const float stepZ = (maxZ - minZ) / kContourCells;
	for (int j = 0; j < kPoints; ++j) {
		for (int i = 0; i < kPoints; ++i) {
			values[j * kPoints + i] = shape.CalcSignedDistance2D({minX + stepX * i, minZ + stepZ * j}) - level;
		}
	}

	// --- ます目ごとに、縁が通る辺を探してつなぐ ---
	int segmentCount = 0;
	const auto pushSegment = [&](const Vector2& a, const Vector2& b) {
		lines.push_back({shape.ToWorld({a.x, y, a.y}), shape.ToWorld({b.x, y, b.y}), color});
		// ところどころに縦の線（柱の高さが分かるように）
		if (pillarHeight > 0.0f && (segmentCount++ % kVerticalLineEvery) == 0) {
			lines.push_back({shape.ToWorld({a.x, 0.0f, a.y}), shape.ToWorld({a.x, pillarHeight, a.y}), color});
		}
	};
	for (int j = 0; j < kContourCells; ++j) {
		for (int i = 0; i < kContourCells; ++i) {
			// 角：0＝左下、1＝右下、2＝右上、3＝左上（反時計回り）
			const std::array<Vector2, kCorners> corners = {
			    Vector2{minX + stepX * i,       minZ + stepZ * j      },
			    Vector2{minX + stepX * (i + 1), minZ + stepZ * j      },
			    Vector2{minX + stepX * (i + 1), minZ + stepZ * (j + 1)},
			    Vector2{minX + stepX * i,       minZ + stepZ * (j + 1)},
			};
			const std::array<float, kCorners> v = {values[j * kPoints + i], values[j * kPoints + i + 1], values[(j + 1) * kPoints + i + 1], values[(j + 1) * kPoints + i]};
			// 辺 k（角 k → 角 k+1）で符号が変われば、縁が通る点を、距離の比で置く
			// 4つとも符号が変わる（鞍の形）なら、crossings[k] がそのまま辺 k の点になる
			std::array<Vector2, kCorners> crossings{};
			int count = 0;
			for (int k = 0; k < kCorners; ++k) {
				const int next = (k + 1) % kCorners;
				if ((v[k] < 0.0f) != (v[next] < 0.0f)) {
					const float t = v[k] / (v[k] - v[next]);
					crossings[count] = corners[k] + (corners[next] - corners[k]) * t;
					++count;
				}
			}
			if (count == 2) {
				pushSegment(crossings[0], crossings[1]);
			} else if (count == kCorners) {
				// 向かい合う角だけが中（鞍の形）：ます目の真ん中が中なら、中の2つの角はつながっている
				const bool isCenterInside = (v[0] + v[1] + v[2] + v[3]) * 0.25f < 0.0f;
				const bool isFirstInside = v[0] < 0.0f;
				if (isCenterInside == isFirstInside) {
					pushSegment(crossings[0], crossings[1]); // 角 1 を切り取る
					pushSegment(crossings[2], crossings[3]); // 角 3 を切り取る
				} else {
					pushSegment(crossings[3], crossings[0]); // 角 0 を切り取る
					pushSegment(crossings[1], crossings[2]); // 角 2 を切り取る
				}
			}
		}
	}
}

// ===== 攻撃の形1つ（margin だけ太らせた形）=====
void PushHitShape(std::vector<Renderer::LineSegment>& lines, const WorldHitShape& shape, float margin, const Vector4& color, bool drawPillars) {
	switch (shape.type) {
	case HitShapeType::Circle:
	case HitShapeType::Sector:
	case HitShapeType::Line:
		// 底と上の縁（太らせた形は、上下にも margin だけ広い）
		PushFloorContour(lines, shape, margin, -margin, drawPillars ? shape.height : 0.0f, color);
		PushFloorContour(lines, shape, margin, shape.height + margin, 0.0f, color);
		break;
	case HitShapeType::Sphere:
		GizmoLines::PushWireSphere(lines, shape.origin, shape.radius + margin, color);
		break;
	case HitShapeType::Capsule:
		GizmoLines::PushWireCapsule(lines, shape.origin, shape.origin + shape.forward * shape.length, shape.radius + margin, color);
		break;
	}
}

//=============================================================================
// 見せ方（Inspector と保存の両方に使う）
//=============================================================================
// ===== 攻撃の形 =====
void DescribeHitShape(ComponentUI& ui, HitShape& shape) {
	ui.Field("Type###shapeType", shape.type, Tip("Circle・Sector・Line は床に置く形（ローカルの XZ に描いて、上 +Y へ Height だけ伸ばした柱）。Sphere・Capsule は空中の形"));
	ui.Field("Center###shapeCenter", shape.center, Tip("Entity の位置からのずれ（ローカル。床に置く形は底の真ん中）"));
	switch (shape.type) {
	case HitShapeType::Circle:
		ui.Field("Radius###shapeRadius", shape.radius, AtLeast(0.0f).Tip("半径"));
		ui.Field("Inner Radius###shapeInnerRadius", shape.innerRadius, AtLeast(0.0f).Tip("内側の半径（0 なら穴なし。入れるとドーナツ）"));
		ui.Field("Height###shapeHeight", shape.height, AtLeast(0.0f).Tip("底から上への高さ（これより高く跳べば当たらない）"));
		break;
	case HitShapeType::Sector:
		ui.Field("Radius###shapeRadius", shape.radius, AtLeast(0.0f).Tip("半径"));
		ui.Field("Inner Radius###shapeInnerRadius", shape.innerRadius, AtLeast(0.0f).Tip("内側の半径（0 なら穴なし）"));
		ui.Field("Angle###shapeAngle", shape.angle, Range(0.0f, 360.0f).Tip("開き（度）。前 +Z を真ん中に、左右に半分ずつ"));
		ui.Field("Height###shapeHeight", shape.height, AtLeast(0.0f).Tip("底から上への高さ"));
		break;
	case HitShapeType::Line:
		ui.Field("Width###shapeWidth", shape.width, AtLeast(0.0f).Tip("幅"));
		ui.Field("Length###shapeLength", shape.length, AtLeast(0.0f).Tip("長さ（Center から前 +Z へ）"));
		ui.Field("Height###shapeHeight", shape.height, AtLeast(0.0f).Tip("底から上への高さ"));
		break;
	case HitShapeType::Sphere:
		ui.Field("Radius###shapeRadius", shape.radius, AtLeast(0.0f).Tip("半径（Entity の大きさの一番大きい軸を掛ける）"));
		break;
	case HitShapeType::Capsule:
		ui.Field("Radius###shapeRadius", shape.radius, AtLeast(0.0f).Tip("半径"));
		ui.Field("Length###shapeLength", shape.length, AtLeast(0.0f).Tip("線分の長さ（Center から前 +Z へ。両端の半球は入れない）"));
		break;
	}
}

// ===== Hitbox =====
void DescribeHitbox(ComponentUI& ui, HitboxComponent& value) {
	ui.Separator("Shape");
	DescribeHitShape(ui, value.shape);
	ui.Separator("Targets");
	ui.NameMaskField("Targets###targets", value.targets.bits, Layers::GetNameList(), Tip("当てる相手のレイヤー（相手の Hurtbox の Entity のレイヤーで見る）"));
	ui.Field("Instigator###instigator", value.instigator, Tip("撃った本人（空ならこの Entity）。本人とその親子には当たらない。弾は撃った物が作るときに書く"));
	ui.Separator("Window");
	ui.Field("Phase###phase", value.phase, Tip("今の段階（Off・Warning＝予告・Active＝当たる）。保存した値が、Play したとき・Prefab から作ったときの段階になる"));
	ui.Field("Warning Seconds###warningSeconds", value.warningSeconds, AtLeast(0.0f).Tip("Warning の長さ（秒。0 ならずっと）"));
	ui.Field("Activate After Warning###activateAfterWarning", value.activateAfterWarning, Tip("Warning が終わったら Active にする"));
	ui.Field("Active Seconds###activeSeconds", value.activeSeconds, AtLeast(0.0f).Tip("Active の長さ（秒。0 ならずっと）"));
	ui.Separator("Hits");
	ui.Field("Rehit Interval###rehitInterval", value.rehitInterval, AtLeast(0.0f).Tip("同じ相手にもう一度当たるまでの秒（0 なら、1回の攻撃で1回だけ。正なら多段ヒット）"));
	ui.Field("Max Targets###maxTargets", value.maxTargets, AtLeast(0.0f).Tip("1回の攻撃で当てられる相手の数（0 なら何体でも。弾は 1。当てきったら Off になる）"));
	ui.Separator("Just");
	// Just Enabled がオフでも、下の2つは出したまま・保存したままにする（オンに戻したとき、前の値が残っているように）
	ui.Field("Just Enabled###justEnabled", value.justEnabled, Tip("ジャスト判定に使うか（オフならジャストにならない当たり。弾・ジャストを見せたくない攻撃）"));
	ui.Field("Just Margin###justMargin", value.justMargin, AtLeast(0.0f).Tip("ジャスト判定の範囲：形をこの距離だけ太らせた範囲（m。0 なら形そのものの中だけ。ギズモは形の線がそのままジャストの範囲になる）"));
	ui.Field("Just Lead Time###justLeadTime", value.justLeadTime, AtLeast(0.0f).Tip("Active になる何秒前から、ジャスト判定の範囲が有効か（Warning の長さが 0 なら、Warning の間ずっと）"));
}

// ===== Hurtbox =====
void DescribeHurtbox(ComponentUI& ui, HurtboxComponent& value) {
	ui.Field("Enabled###enabled", value.enabled);
	ui.Field("Shape###shape", value.shape);
	ui.Field("Center###center", value.center, Tip("Entity の位置からのずれ（ローカル。回転と大きさも掛かる）"));
	ui.Field("Radius###radius", value.radius, AtLeast(0.0f).Tip("半径（Entity の大きさの X と Z の大きい方を掛ける。球は一番大きい軸）"));
	if (value.shape == HurtboxShape::Capsule) {
		ui.Field("Height###height", value.height, AtLeast(0.0f).Tip("下の端から上の端まで（半球も入れる）。ローカルの上 +Y に立ち、Entity と一緒に回る"));
	}
	ui.Field("Damage Multiplier###damageMultiplier", value.damageMultiplier, AtLeast(0.0f).Tip("ダメージの倍率（弱点なら 1 より大きく）。1回の攻撃が同じ本体のいくつもの Hurtbox に入ったら、倍率の大きい方に当たる"));
}
} // namespace


//=============================================================================
// 初期化・終了
//=============================================================================
void HitSystem::Initialize() {
	HTT_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<HitState>();
	state->gizmoLines.fadeStartDistance = kNoFadeStart;
	state->gizmoLines.fadeEndDistance = kNoFadeEnd;

	// Hitbox・Hurtbox の置き場所・保存・Inspector を登録する（見せ方は1つの関数で、保存と Inspector の両方に使う）
	EntityManager::RegisterComponent<HitboxComponent>();
	ComponentSerializer::Register<HitboxComponent>("Hitbox", &DescribeHitbox);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<HitboxComponent>>("Hitbox", "Combat", &DescribeHitbox));
	EntityManager::RegisterComponent<HurtboxComponent>();
	ComponentSerializer::Register<HurtboxComponent>("Hurtbox", &DescribeHurtbox);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<HurtboxComponent>>("Hurtbox", "Combat", &DescribeHurtbox));
	EntityManager::AddDestroyListener(&HitSystem::OnEntityDestroyed);
}

void HitSystem::Release() { state.reset(); }


//=============================================================================
// 更新
//=============================================================================
void HitSystem::Update(bool isGameRunning, float deltaTime) {
	HTT_ASSERT_MSG(state != nullptr, "HitSystem::Initialize()を先に呼んでください");
	state->hits.clear();
	if (!isGameRunning) {
		// 停止中：実行中の値を全部捨てる（次の Play は記録の無い状態から）
		state->hitboxes.clear();
		state->histories.clear();
		state->hurtboxes.clear();
		return;
	}
	if (deltaTime <= 0.0f) {
		return; // 時間が止まっている（一時停止・ヒットストップ）：窓も当たりも進めない
	}
	++state->frame;
	CollectHurtboxes();
	EntityManager::ForEach<HitboxComponent>([deltaTime](Handle<Entity> entity, HitboxComponent& hitbox) { UpdateHitbox(entity, hitbox, deltaTime); });
	// 見つからなかった Hitbox の実行中の値を捨てる（Component を外された）
	std::erase_if(state->hitboxes, [](const auto& item) { return item.second.seenFrame != state->frame; });
}

// ===== Entity が消える直前 =====
void HitSystem::OnEntityDestroyed(Handle<Entity> entity) {
	if (state != nullptr) {
		state->hitboxes.erase(KeyOf(entity));
		state->histories.erase(KeyOf(entity));
	}
}


//=============================================================================
// 窓を開け閉めする
//=============================================================================
void HitSystem::Warn(Handle<Entity> hitbox, float warningSeconds) {
	if (HitboxComponent* component = EntityManager::Get<HitboxComponent>(hitbox)) {
		BeginWarning(*component, warningSeconds, false, component->activeSeconds);
	}
}

void HitSystem::Warn(Handle<Entity> hitbox, float warningSeconds, float activeSeconds) {
	if (HitboxComponent* component = EntityManager::Get<HitboxComponent>(hitbox)) {
		BeginWarning(*component, warningSeconds, true, activeSeconds);
	}
}

void HitSystem::Activate(Handle<Entity> hitbox, float activeSeconds) {
	if (HitboxComponent* component = EntityManager::Get<HitboxComponent>(hitbox)) {
		BeginActive(*component, (std::max)(activeSeconds, 0.0f));
	}
}

void HitSystem::Deactivate(Handle<Entity> hitbox) {
	if (HitboxComponent* component = EntityManager::Get<HitboxComponent>(hitbox)) {
		component->phase = HitboxPhase::Off;
		component->phaseTime = 0.0f;
	}
}


//=============================================================================
// 当たりの知らせ
//=============================================================================
std::span<const HitInfo> HitSystem::GetAllHits() {
	HTT_ASSERT_MSG(state != nullptr, "HitSystem::Initialize()を先に呼んでください");
	return state->hits;
}

void HitSystem::GetHitsDealt(Handle<Entity> entity, std::vector<HitInfo>& out) {
	out.clear();
	for (const HitInfo& hit : GetAllHits()) {
		if (hit.hitbox == entity || hit.instigator == entity) {
			out.push_back(hit);
		}
	}
}

void HitSystem::GetHitsTaken(Handle<Entity> receiver, std::vector<HitInfo>& out) {
	out.clear();
	for (const HitInfo& hit : GetAllHits()) {
		if (hit.receiver == receiver) {
			out.push_back(hit);
		}
	}
}


//=============================================================================
// ジャスト判定
//=============================================================================
bool HitSystem::FindJust(Handle<Entity> receiver, JustCandidate& out) {
	HTT_ASSERT_MSG(state != nullptr, "HitSystem::Initialize()を先に呼んでください");
	Handle<Entity> found;
	return FindJustCandidate(receiver, out, found);
}

bool HitSystem::TryConsumeJust(Handle<Entity> receiver, JustCandidate& out) {
	HTT_ASSERT_MSG(state != nullptr, "HitSystem::Initialize()を先に呼んでください");
	Handle<Entity> found;
	if (!FindJustCandidate(receiver, out, found)) {
		return false;
	}
	// 「よけた」と記録する。まだ Update で一度も見ていない Hitbox（このフレームに作った・窓を開けた）なら、今の回から始める
	const auto [it, isNew] = state->hitboxes.try_emplace(KeyOf(found));
	HitboxRuntime& runtime = it->second;
	if (isNew) {
		const HitboxComponent* hitbox = EntityManager::Get<HitboxComponent>(found);
		runtime.serial = hitbox->attackSerial;
		runtime.lastPhase = hitbox->phase;
		runtime.seenFrame = state->frame;
	}
	HitRecord* record = FindRecord(runtime, receiver, out.attackSerial);
	if (record == nullptr) {
		runtime.records.push_back({receiver, out.attackSerial});
		record = &runtime.records.back();
	}
	record->isDodged = true;
	return true;
}


//=============================================================================
// 形
//=============================================================================
WorldHitShape HitSystem::CalcWorldShape(const HitboxComponent& hitbox, const TransformComponent& transform) { return WorldHitShape::Make(hitbox.shape, transform.worldMatrix); }

Capsule HitSystem::CalcWorldHurtbox(const HurtboxComponent& hurtbox, const TransformComponent& transform) {
	const Matrix4x4& m = transform.worldMatrix;
	const Vector3 rowX = {m.m[0][0], m.m[0][1], m.m[0][2]};
	const Vector3 rowY = {m.m[1][0], m.m[1][1], m.m[1][2]};
	const Vector3 rowZ = {m.m[2][0], m.m[2][1], m.m[2][2]};
	const float scaleXZ = (std::max)(Length(rowX), Length(rowZ));
	const float scaleY = Length(rowY);
	const Vector3 center = TransformPoint(hurtbox.center, m);
	if (hurtbox.shape == HurtboxShape::Sphere) {
		const float radius = hurtbox.radius * (std::max)(scaleXZ, scaleY);
		return {center, center, radius};
	}
	// カプセル：ローカルの上に立つ線分（高さが半径の2倍より小さければ、球と同じ）
	const float radius = hurtbox.radius * scaleXZ;
	const float halfSegment = (std::max)(hurtbox.height * scaleY * 0.5f - radius, 0.0f);
	const Vector3 up = (scaleY > 0.0f) ? rowY / scaleY : Vector3{0.0f, 1.0f, 0.0f};
	return {center - up * halfSegment, center + up * halfSegment, radius};
}

Handle<Entity> HitSystem::FindReceiver(Handle<Entity> hurtbox) {
	return (state != nullptr && state->receiverFinder != nullptr) ? state->receiverFinder(hurtbox) : hurtbox;
}


//=============================================================================
// 答える係
//=============================================================================
void HitSystem::SetReceiverFinder(ReceiverFinder finder) {
	HTT_ASSERT_MSG(state != nullptr, "HitSystem::Initialize()を先に呼んでください");
	state->receiverFinder = finder;
}

void HitSystem::SetHitResolver(HitResolver resolver) {
	HTT_ASSERT_MSG(state != nullptr, "HitSystem::Initialize()を先に呼んでください");
	state->hitResolver = resolver;
}


//=============================================================================
// ギズモ
//=============================================================================
void HitSystem::DrawGizmos(Camera* camera) {
	HTT_ASSERT_MSG(state != nullptr, "HitSystem::Initialize()を先に呼んでください");
	if (!state->showGizmos) {
		return;
	}
	Renderer::LineListConfig& gizmo = state->gizmoLines;
	gizmo.lines.clear();
	gizmo.camera = camera;
	// 停止中も、今の Transform で描く（置きながら形を合わせられるように）
	EntityManager::ForEach<HitboxComponent>([&](Handle<Entity> entity, HitboxComponent& hitbox) {
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		if (transform == nullptr || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		const WorldHitShape shape = CalcWorldShape(hitbox, *transform);
		Vector4 color = kOffColor;
		switch (hitbox.phase) {
		case HitboxPhase::Off:
			color = kOffColor;
			break;
		case HitboxPhase::Warning:
			color = kWarningColor;
			break;
		case HitboxPhase::Active:
			color = kActiveColor;
			break;
		}
		PushHitShape(gizmo.lines, shape, 0.0f, color, true);
		// ジャスト判定の範囲（Just Margin が 0 なら形の線と重なるので描かない。形の線がそのままジャストの範囲）
		if (hitbox.justEnabled && hitbox.justMargin > 0.0f) {
			PushHitShape(gizmo.lines, shape, hitbox.justMargin, (hitbox.phase == HitboxPhase::Off) ? kJustOffColor : kJustColor, false);
		}
	});
	EntityManager::ForEach<HurtboxComponent>([&](Handle<Entity> entity, HurtboxComponent& hurtbox) {
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		if (!hurtbox.enabled || transform == nullptr || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		const Capsule capsule = CalcWorldHurtbox(hurtbox, *transform);
		if (hurtbox.shape == HurtboxShape::Sphere) {
			GizmoLines::PushWireSphere(gizmo.lines, capsule.start, capsule.radius, kHurtboxColor);
		} else {
			GizmoLines::PushWireCapsule(gizmo.lines, capsule.start, capsule.end, capsule.radius, kHurtboxColor);
		}
	});
	Renderer::DrawLines(gizmo); // 線が0本なら中で何もしない
	gizmo.camera = nullptr;     // フレームをまたいで持たない
}

bool& HitSystem::GetShowGizmos() {
	HTT_ASSERT_MSG(state != nullptr, "HitSystem::Initialize()を先に呼んでください");
	return state->showGizmos;
}
```

### C-6 `ECS/Script.h`

include に足す。

```cpp
#include "HTTEngine/Combat/HitInfo.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/ECS/GameObject.h"
```

`OnCollisionExit` の下（`// ===== Timeline =====` の前）に足す。`OnBeforeHitTaken` はパート D の DamageSystem が呼ぶ（C までは呼ばれない）。

```cpp
	// ===== 攻撃の当たり（Hitbox・Hurtbox・Health）=====
	// OnHitDealt・OnHitTaken は、当たった次のフレームの Update の前に来る（Collider の当たりと同じ）。hit.point がダメージの数字を出す所、hit.damage が量
	virtual void OnHitDealt(const HitInfo& /*hit*/) {} // 自分の攻撃が当たった（Hitbox の Entity と、撃った本人（Instigator）に来る）
	virtual void OnHitTaken(const HitInfo& /*hit*/) {} // 攻撃を受けた（受けた本体＝Health の付いた Entity に来る）。hit.isDefeated なら倒れた
	// 受ける前（HP を減らす前に、その場で呼ばれる。受けた本体に来る）。hit.result を Guarded（防いだ）・Ignored（断った）に変える・hit.damage を書き換えられる
	// 無敵のように「いつも断る」物は Health の Invincible に書く方がよい（Inspector で見えて、Script の無い物にも効く）
	virtual void OnBeforeHitTaken(HitInfo& /*hit*/) {}
```

### C-7 `Systems/Gameplay/ScriptSystem.h`・`.cpp`

`.h`：前方宣言に `struct HitInfo;` を足す。

```cpp
class ComponentUI;
struct TimelineEvent;
struct HitInfo;
```

`Update` のコメントを直す。

```cpp
	// Awake・OnEnable → Start → 当たり（OnTrigger〜・OnCollision〜）→ 攻撃の当たり（OnHitDealt・OnHitTaken）→ Update。SYSTEM(...) より前
```

`SendTimelineEvent` の下に足す。

```cpp
	// entity に付いている動いている Script 全部の OnBeforeHitTaken を呼ぶ（DamageSystem から。HP を減らす前に、その場で）
	static void SendBeforeHitTaken(Handle<Entity> entity, HitInfo& hit);
```

`.cpp`：include に足す。

```cpp
#include "HTTEngine/Editor/Inspector/ScriptEditor.h"
#include "HTTEngine/Systems/Combat/HitSystem.h"
#include "HTTEngine/Systems/Physics/CollisionSystem.h"
```

`Update` の「3. 当たり」の後（`// --- 4. Update ---` の前）に足す。

```cpp
	// --- 3.5 攻撃の当たり（前のフレームの終わりに HitSystem が作った一覧）---
	// 当てた側（Hitbox の Entity・撃った本人）には OnHitDealt、受けた本体には OnHitTaken
	for (size_t i = 0; i < count; ++i) {
		Script& script = *scripts[i];
		if (!IsRunning(script)) {
			continue;
		}
		const Handle<Entity> self = script.gameObject_.handle;
		for (const HitInfo& hit : HitSystem::GetAllHits()) {
			if (!IsRunning(script) || isSkipped(script)) {
				break; // 知らせの途中で自分を無効にした・消した
			}
			if (hit.hitbox == self || hit.instigator == self) {
				script.OnHitDealt(hit);
			}
			if (hit.receiver == self && IsRunning(script)) {
				script.OnHitTaken(hit);
			}
		}
	}
```

`SendTimelineEvent` の後に足す。

```cpp
// ===== entity に付いている動いている Script 全部の OnBeforeHitTaken を呼ぶ（DamageSystem から）=====
void ScriptSystem::SendBeforeHitTaken(Handle<Entity> entity, HitInfo& hit) {
	// 呼んだ先で Script が増えても大丈夫なように、添字で回す（Update と同じ）
	const size_t count = scripts.size();
	for (size_t i = 0; i < count; ++i) {
		Script& script = *scripts[i];
		if (script.gameObject_.handle == entity && IsRunning(script)) {
			script.OnBeforeHitTaken(hit);
		}
	}
}
```

### C-8 `ECS/GameComponent.h`

ゲームの Script・COMPONENT から使えるように、`// Camera` の下に足す（パート D でもう2行足す）。

```cpp
// Camera
#include "HTTEngine/Systems/Camera/CameraSystem.h"
// Combat
#include "HTTEngine/ECS/Layers.h"
#include "HTTEngine/Systems/Combat/HitSystem.h"
```

### C-9 `Systems/EngineSystems.cpp`

include の `// Camera` の下に足す。

```cpp
// Camera
#include "HTTEngine/Systems/Camera/CameraSystem.h"
// Combat
#include "HTTEngine/Systems/Combat/HitSystem.h"
```

`Initialize` の `TimelineTriggerSystem::Initialize` の下に足す。

```cpp
	HitSystem::Initialize();              // Hitbox・Hurtbox（攻撃の当たり）
```

`Release` の `TimelineTriggerSystem::Release` の下に足す。

```cpp
	HitSystem::Release();
```

`Update` の `CollisionSystem::Update` の下に足す。

```cpp
	HitSystem::Update(isGameRunning, Time::GetDeltaTime());    // 攻撃の当たり（全部が動いた後の位置で。消すと決めた Entity はもう当たらない。ダメージの層に聞いて HP を減らす。知らせは次のフレームの Script・SYSTEM が読む）
```

`Draw` の `WindZoneSystem::DrawGizmos` の下に足す。

```cpp
	HitSystem::DrawGizmos(gameCamera);      // 攻撃の形（段階で色が変わる）・ジャスト判定の範囲・Hurtbox
```

### C-10 `Editor/ImGuiManager.cpp`

include に足す。

```cpp
#include "HTTEngine/Systems/Animation/AnimationSystem.h"
#include "HTTEngine/Systems/Combat/HitSystem.h"
#include "HTTEngine/Systems/Physics/CollisionSystem.h"
```

View メニューの `Colliders` の下に足す。

```cpp
		ImGui::MenuItem("Colliders", nullptr, &CollisionSystem::GetShowGizmos());
		ImGui::MenuItem("Hit Shapes", nullptr, &HitSystem::GetShowGizmos()); // 攻撃の形・ジャスト判定の範囲・Hurtbox
```

### C-11 `HTTEngine.vcxproj`

| フィルター | 足すファイル |
|---|---|
| `HTTEngine\Combat`（新しい） | `HitInfo.h` |
| `HTTEngine\Components\Combat`（新しい） | `HitboxComponent.h`・`HurtboxComponent.h` |
| `HTTEngine\Systems\Combat`（新しい） | `HitSystem.h`・`HitSystem.cpp` |

ここでビルドできる。Add Component → **Combat** に Hitbox・Hurtbox が出る。当たると Script の `OnHitTaken` が来る（HP はまだ無い）。

## パートD ダメージの層（Health・Damage Source・DamageSystem）

### D-1 `Combat/DamageTypes.h`・`.cpp`（新しい）

```cpp
#pragma once
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

// 前方宣言
class NameList;

/// <summary>
/// 攻撃の種類（0〜31 の番号。名前は Settings の Damage Types で付ける。Unreal の DamageType の役目）
/// <para>DamageSource で1つ選び、Health の Immune Types（効かない種類）・受ける側の OnBeforeHitTaken（ガードできない種類など）で使う</para>
/// <para>0 は Default（名前は変えられない）</para>
/// </summary>
class DamageTypes {
public:
	static constexpr int kDefault = 0; // 何も選ばなかったときの種類

	// Settings に名前の行を登録する（EngineSystems::Initialize の初めに1回）
	static void Initialize();

	// 名前の一覧（Inspector の選択肢）
	static const NameList& GetNameList();

	// 番号 → 名前（名前が無ければ空）
	static std::string TypeToName(int type);
	// 名前 → 番号（無ければ -1）
	static int NameToType(std::string_view name);
	// 名前を並べてビットの並びを作る（Immune Types に書く値）
	static uint32_t GetMask(std::initializer_list<std::string_view> names);
};
```

```cpp
#include "DamageTypes.h"

#include "HTTEngine/Core/NameList.h"

//=============================================================================
// 名前の一覧
//=============================================================================
// ===== 名前の一覧（関数の中の static。初めて呼んだときに作る）=====
const NameList& DamageTypes::GetNameList() {
	static const NameList names("DamageTypes", "Type", {"Default"}); // 保存先のフォルダの名前になるので、空白を入れない
	return names;
}

// ===== Settings に名前の行を登録する =====
void DamageTypes::Initialize() { GetNameList().Register(); }


//=============================================================================
// 番号 ↔ 名前
//=============================================================================
std::string DamageTypes::TypeToName(int type) { return GetNameList().GetName(type); }

int DamageTypes::NameToType(std::string_view name) { return GetNameList().Find(name); }

uint32_t DamageTypes::GetMask(std::initializer_list<std::string_view> names) { return GetNameList().MakeMask(names); }
```

### D-2 `Components/Combat/HealthComponent.h`（新しい）

```cpp
#pragma once
#include <cstdint>
#include <type_traits>

// HP と、攻撃を断る設定（受ける本体）。Entity に付ける Component（カテゴリ：Combat。ダメージの層）
// 同じ Entity か子の Hurtbox に当たった攻撃を、DamageSystem がここで受ける（無敵なら断る → 減らす → 0 になったら倒れた）
// 同じ Entity に Attack Table があれば、攻撃表は HP の割合をここから読む（Health Ratio を書かなくてよい）

/// <summary>
/// HP と、攻撃を断る設定
/// </summary>
struct HealthComponent {
	float maxHealth = 100.0f;        // 最大の HP
	float health = 100.0f;           // 今の HP（保存した値から始まる。ふつうは最大と同じ）。0 以下なら倒れている（もう当たらない）
	bool invincible = false;         // 無敵（ゲームが書く。true の間は攻撃を断る＝記録しないので、切れた後に同じ攻撃の範囲にいれば当たる）
	float invincibleAfterHit = 0.0f; // 当たった後、この秒だけ攻撃を断る（被弾の後の無敵）
	uint32_t immuneTypes = 0;        // 効かない攻撃の種類（Settings の Damage Types。断る）

	// --- 実行中だけの値（保存しない。DamageSystem が書く）---
	float invincibleTimer = 0.0f; // 被弾の後の無敵の残りの秒

	// HP の割合（0〜1。最大が 0 なら 0）
	float GetRatio() const { return (maxHealth > 0.0f) ? health / maxHealth : 0.0f; }
	// 倒れている（HP が 0 以下）
	bool IsDefeated() const { return health <= 0.0f; }
	// 今、攻撃を断るか（無敵か、被弾の後の無敵の間）
	bool IsInvincible() const { return invincible || invincibleTimer > 0.0f; }
};

static_assert(std::is_trivially_copyable_v<HealthComponent>);
```

### D-3 `Components/Combat/DamageSourceComponent.h`（新しい）

```cpp
#pragma once
#include <cstdint>
#include <type_traits>

// 攻撃が当たったときのダメージと手応え。Hitbox と同じ Entity に付ける Component（カテゴリ：Combat。ダメージの層）
// 量 ＝ Damage × 当たった Hurtbox の倍率 × 距離の倍率（Falloff Curve）。受ける側の OnBeforeHitTaken で書き換えられる
// 手応え（ヒットストップ・カメラの揺れ）は、受けた側が断らずに Hit になったときだけ出る

/// <summary>
/// 攻撃のダメージと手応え
/// </summary>
struct DamageSourceComponent {
	float damage = 10.0f; // ダメージの量（倍率を掛ける前）
	int damageType = 0;   // 攻撃の種類（Settings の Damage Types の番号）

	// --- 距離で変わる倍率（Hitbox が Active になった所から、当たった所までの距離。弾なら撃った所から）---
	uint32_t falloffCurve = 0; // 倍率のカーブ（.curve。横軸＝距離を Near〜Far で 0〜1 に直した値、縦軸＝倍率）。未選択なら倍率は 1
	float falloffNear = 0.0f;  // この距離で、カーブの横軸が 0
	float falloffFar = 10.0f;  // この距離で、カーブの横軸が 1（それより遠くても 1）

	// --- 手応え（0 なら無し）---
	float hitStop = 0.0f; // ヒットストップの秒（Time::HitStop）
	float shake = 0.0f;   // カメラの揺れの強さ（CameraSystem::AddImpulse。向きは攻撃の向き）
};

static_assert(std::is_trivially_copyable_v<DamageSourceComponent>);
```

### D-4 `Systems/Combat/DamageSystem.h`（新しい）

```cpp
#pragma once
#include "HTTEngine/Combat/HitInfo.h"
#include "HTTEngine/Components/Combat/DamageSourceComponent.h"
#include "HTTEngine/Components/Combat/HealthComponent.h"
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"

/// <summary>
/// 攻撃のダメージ（ダメージの層）。当たりの層（HitSystem）から当たり1件を受け取り、受けるか決めて HP を減らす
/// <para>流れ：倒れている・無敵・効かない種類なら断る → 量を決める（Damage × Hurtbox の倍率 × 距離の倍率）→ 受ける側の Script の OnBeforeHitTaken
/// （Guarded・Ignored に変える・量を書き換える）→ HP を減らす → 被弾の後の無敵 → 手応え（ヒットストップ・カメラの揺れ）</para>
/// <para>Initialize で HitSystem に答え方を2つ登録する（受ける本体＝Health の付いた一番近い親か自分／受けるか）。当たりの層はこの型を知らない</para>
/// </summary>
class DamageSystem {
public:
	// Health・Damage Source を登録する（置き場所・保存・Inspector）。HitSystem::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// 被弾の後の無敵の残りを減らす（HitSystem::Update の前に呼ぶ）
	/// </summary>
	/// <param name="isGameRunning">再生中か</param>
	/// <param name="deltaTime">経過秒（Time::GetDeltaTime。ヒットストップの間は 0 なので、無敵の残りも減らない）</param>
	static void Update(bool isGameRunning, float deltaTime);

	// Hurtbox の受ける本体（Health の付いた一番近い親か自分。無ければ hurtbox 自身）
	static Handle<Entity> FindReceiver(Handle<Entity> hurtbox);

	// 距離の倍率（Damage Source の Falloff Curve。カーブが無ければ 1）
	static float CalcFalloff(const DamageSourceComponent& source, float distance);

private:
	// 当たり1件を受けるか決めて、ダメージの欄を書く（HitSystem から呼ばれる）。false なら断った
	static bool Resolve(HitInfo& hit);
};
```

### D-5 `Systems/Combat/DamageSystem.cpp`（新しい）

```cpp
#include "DamageSystem.h"

#include <algorithm>
#include <memory>

#include "HTTEngine/Combat/DamageTypes.h"
#include "HTTEngine/Curve/Curve.h"
#include "HTTEngine/Curve/CurveManager.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Systems/Camera/CameraSystem.h"
#include "HTTEngine/Systems/Combat/HitSystem.h"
#include "HTTEngine/Systems/Gameplay/ScriptSystem.h"
#include "HTTEngine/Time/EngineTime.h"

namespace {
constexpr uint32_t kMaxReceiverDepth = 64; // 受ける本体を探して、親を何段までたどるか（EntityManager の親子の深さの上限と同じ）

//=============================================================================
// 見せ方（Inspector と保存の両方に使う）
//=============================================================================
// ===== Health =====
void DescribeHealth(ComponentUI& ui, HealthComponent& value) {
	ui.Field("Max Health###maxHealth", value.maxHealth, AtLeast(0.0f).Tip("最大の HP"));
	ui.Field("Health###health", value.health, AtLeast(0.0f).Tip("今の HP（保存した値から始まる）。0 以下なら倒れている（もう当たらない）"));
	ui.Field("Invincible###invincible", value.invincible, Tip("無敵（ゲームが書く）。攻撃を断る＝記録しないので、切れた後に同じ攻撃の範囲にいれば当たる"));
	ui.Field("Invincible After Hit###invincibleAfterHit", value.invincibleAfterHit, AtLeast(0.0f).Tip("当たった後、この秒だけ攻撃を断る（被弾の後の無敵）"));
	ui.NameMaskField("Immune Types###immuneTypes", value.immuneTypes, DamageTypes::GetNameList(), Tip("効かない攻撃の種類（断る）。名前は Settings の DamageTypes"));
}

// ===== Damage Source =====
void DescribeDamageSource(ComponentUI& ui, DamageSourceComponent& value) {
	ui.Field("Damage###damage", value.damage, AtLeast(0.0f).Tip("ダメージの量（Hurtbox の倍率・距離の倍率を掛ける前）"));
	ui.NameField("Damage Type###damageType", value.damageType, DamageTypes::GetNameList(), Tip("攻撃の種類（受ける側の Immune Types・OnBeforeHitTaken で使う）。名前は Settings の DamageTypes"));
	ui.Separator("Falloff");
	ui.AssetField("Falloff Curve###falloffCurve", value.falloffCurve, AssetType::Curve, Tip("距離で変わる倍率（横軸＝距離を Near〜Far で 0〜1 に直した値、縦軸＝倍率）。(none) なら倍率は 1"));
	ui.BeginDisabled(value.falloffCurve == 0);
	ui.Field("Falloff Near###falloffNear", value.falloffNear, AtLeast(0.0f).Tip("この距離（m）で、カーブの横軸が 0。距離は Hitbox が Active になった所から（弾なら撃った所から）"));
	ui.Field("Falloff Far###falloffFar", value.falloffFar, AtLeast(0.0f).Tip("この距離（m）で、カーブの横軸が 1（それより遠くても 1）"));
	ui.EndDisabled();
	ui.Separator("Feedback");
	ui.Field("Hit Stop###hitStop", value.hitStop, AtLeast(0.0f).Tip("当たったときのヒットストップの秒（0 なら無し）。断られた・防がれたときは出ない"));
	ui.Field("Shake###shake", value.shake, AtLeast(0.0f).Tip("当たったときのカメラの揺れの強さ（0 なら無し）。向きは攻撃の向き"));
}
} // namespace


//=============================================================================
// 初期化・終了
//=============================================================================
void DamageSystem::Initialize() {
	// Health・Damage Source の置き場所・保存・Inspector を登録する
	EntityManager::RegisterComponent<HealthComponent>();
	ComponentSerializer::Register<HealthComponent>("Health", &DescribeHealth);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<HealthComponent>>("Health", "Combat", &DescribeHealth));
	EntityManager::RegisterComponent<DamageSourceComponent>();
	ComponentSerializer::Register<DamageSourceComponent>("Damage Source", &DescribeDamageSource);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<DamageSourceComponent>>("Damage Source", "Combat", &DescribeDamageSource));

	// 当たりの層に答え方を登録する（当たりの層は、Health・Damage Source を知らない）
	HitSystem::SetReceiverFinder(&DamageSystem::FindReceiver);
	HitSystem::SetHitResolver(&DamageSystem::Resolve);
}

void DamageSystem::Release() {}


//=============================================================================
// 更新
//=============================================================================
void DamageSystem::Update(bool isGameRunning, float deltaTime) {
	if (!isGameRunning || deltaTime <= 0.0f) {
		return;
	}
	// 被弾の後の無敵の残りを減らす
	EntityManager::ForEach<HealthComponent>([deltaTime](Handle<Entity>, HealthComponent& health) { health.invincibleTimer = (std::max)(health.invincibleTimer - deltaTime, 0.0f); });
}


//=============================================================================
// 受ける本体
//=============================================================================
Handle<Entity> DamageSystem::FindReceiver(Handle<Entity> hurtbox) {
	// 自分から親へたどって、Health の付いた一番近い物（輪になっていても止まるように、回数に上限）
	Handle<Entity> current = hurtbox;
	for (uint32_t i = 0; i < kMaxReceiverDepth && current.IsValid(); ++i) {
		if (EntityManager::Get<HealthComponent>(current) != nullptr) {
			return current;
		}
		const Entity* entity = EntityManager::Get(current);
		current = (entity != nullptr) ? entity->parent : Handle<Entity>{};
	}
	return hurtbox;
}


//=============================================================================
// 量
//=============================================================================
float DamageSystem::CalcFalloff(const DamageSourceComponent& source, float distance) {
	const Curve* curve = CurveManager::Get(source.falloffCurve);
	if (curve == nullptr) {
		return 1.0f;
	}
	const float range = source.falloffFar - source.falloffNear;
	const float t = (range > 0.0f) ? std::clamp((distance - source.falloffNear) / range, 0.0f, 1.0f) : 0.0f;
	return curve->Evaluate(t);
}


//=============================================================================
// 当たり1件を受ける
//=============================================================================
bool DamageSystem::Resolve(HitInfo& hit) {
	HealthComponent* health = EntityManager::Get<HealthComponent>(hit.receiver);
	const DamageSourceComponent* source = EntityManager::Get<DamageSourceComponent>(hit.hitbox);
	hit.damageType = (source != nullptr) ? source->damageType : DamageTypes::kDefault;

	// --- 1. 断る（倒れている・無敵・効かない種類）---
	if (health != nullptr) {
		const bool isImmune = hit.damageType >= 0 && hit.damageType < 32 && ((health->immuneTypes >> hit.damageType) & 1u) != 0u;
		if (health->IsDefeated() || health->IsInvincible() || isImmune) {
			return false;
		}
	}

	// --- 2. 量（Damage × Hurtbox の倍率 × 距離の倍率）---
	hit.result = HitResult::Hit;
	hit.damage = (source != nullptr) ? source->damage * hit.hurtboxMultiplier * CalcFalloff(*source, hit.distance) : 0.0f;

	// --- 3. 受ける側の Script に聞く（ガード・条件付きの無敵・量の書き換え）---
	ScriptSystem::SendBeforeHitTaken(hit.receiver, hit);
	if (hit.result == HitResult::Ignored) {
		return false;
	}

	// --- 4. HP を減らす ---
	if (health != nullptr) {
		health->health = (std::max)(health->health - (std::max)(hit.damage, 0.0f), 0.0f);
		hit.isDefeated = health->IsDefeated();
		hit.healthAfter = health->health;
		if (hit.result == HitResult::Hit && health->invincibleAfterHit > 0.0f) {
			health->invincibleTimer = health->invincibleAfterHit;
		}
	}

	// --- 5. 手応え（当たったときだけ。防いだときは出さない）---
	if (source != nullptr && hit.result == HitResult::Hit) {
		if (source->hitStop > 0.0f) {
			Time::HitStop(source->hitStop);
		}
		if (source->shake > 0.0f) {
			CameraSystem::AddImpulse(source->shake, hit.direction);
		}
	}
	return true;
}
```

### D-6 `Systems/EngineSystems.cpp`

include に足す（2か所）。

```cpp
#include "HTTEngine/AI/AttackTableManager.h"
#include "HTTEngine/Combat/DamageTypes.h"
#include "HTTEngine/Curve/CurveManager.h"
```

```cpp
// Combat
#include "HTTEngine/Systems/Combat/DamageSystem.h"
#include "HTTEngine/Systems/Combat/HitSystem.h"
```

`Initialize` の `Layers::Initialize` の下に足す。

```cpp
	DamageTypes::Initialize(); // 攻撃の種類の名前（Settings の DamageTypes）
```

`HitSystem::Initialize` の下に足す（HitSystem に答え方を登録するので、HitSystem の後）。

```cpp
	DamageSystem::Initialize();           // Health・Damage Source（攻撃のダメージ）。HitSystem に答え方を登録するので、HitSystem の後
```

`Release` の `HitSystem::Release` の上に足す。

```cpp
	DamageSystem::Release();
	HitSystem::Release();
```

`Update` の `HitSystem::Update` の上に足す。

```cpp
	DamageSystem::Update(isGameRunning, Time::GetDeltaTime()); // 被弾の後の無敵の残りを減らす（攻撃の当たりより前）
```

### D-7 `ECS/GameComponent.h`

C-8 の `// Combat` を、この4行にする。

```cpp
// Combat
#include "HTTEngine/Combat/DamageTypes.h"
#include "HTTEngine/ECS/Layers.h"
#include "HTTEngine/Systems/Combat/DamageSystem.h"
#include "HTTEngine/Systems/Combat/HitSystem.h"
```

### D-8 `Components/Gameplay/AttackTableComponent.h`・`Systems/Gameplay/AttackTableSystem.cpp`

`.h`：`healthRatio` のコメントを直す。

```cpp
	float healthRatio = 1.0f; // 残りの HP の割合（0〜1）。同じ Entity に Health があれば AttackTableSystem がそこから書く（無ければゲームが書く）。フェーズの切り替えと撃破はこの値で決まる
```

`.cpp`：include に足す。

```cpp
#include "HTTEngine/AI/AttackPart.h"
#include "HTTEngine/Components/Combat/HealthComponent.h"
#include "HTTEngine/Diagnostics/LogManager.h"
```

`DescribeAttackTableComponent` の Health Ratio の説明を直す。

```cpp
	ui.Field("Health Ratio###healthRatio", value.healthRatio, Range(0.0f, 1.0f).Tip("残りの HP の割合（0〜1）。同じ Entity に Health があれば、再生中は毎フレームそこから書かれる。無ければゲームが書く。再生中につまむと、フェーズの移行と撃破を試せる（Health があるときは Health の値をつまむ）"));
```

`AttackTableSystem::Update` の `ForEach` を直す。

```cpp
	EntityManager::ForEach<AttackTableComponent>([deltaTime](Handle<Entity> entity, AttackTableComponent& component) {
		// 同じ Entity に Health があれば、HP の割合はそこから読む（ゲームが毎フレーム書かなくてよい。書き忘れ・書く順番のずれが出ない）
		if (const HealthComponent* health = EntityManager::Get<HealthComponent>(entity)) {
			component.healthRatio = health->GetRatio();
		}
		UpdateEntity(entity, component, deltaTime);
	});
}
```

### D-9 `AI/AttackPart.h`・`.cpp`

`.h`：`Fields` の下に足す。

```cpp
	virtual void Fields(ComponentUI& /*ui*/) {}

protected:
	/// <summary>
	/// Hitbox の窓を、今の段階に合わせる（OnStage で呼ぶ）。攻撃表と Hitbox はお互いを知らないので、つなぐのは攻撃部品
	/// <para>予備動作 → Warning（予告。段階の残りの秒）、攻撃 → Active（新しい攻撃の回。段階の残りの秒）、硬直 → Off。OnEnd でも呼ぶか HitSystem::Deactivate で閉じる</para>
	/// </summary>
	static void SyncHitbox(GameObject hitbox, const AttackPartContext& context);
};
```

`.cpp`：include に足す。

```cpp
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Systems/Combat/HitSystem.h"
```

一番下に足す。

```cpp
//=============================================================================
// 攻撃部品の道具
//=============================================================================
// ===== Hitbox の窓を、今の段階に合わせる =====
void AttackPart::SyncHitbox(GameObject hitbox, const AttackPartContext& context) {
	const float remaining = (std::max)(context.stageDuration - context.stageTime, 0.0f); // 今の段階の残りの秒
	switch (context.stage) {
	case AttackStage::Windup:
		HitSystem::Warn(hitbox.handle, remaining); // 予告。Active へは、攻撃の段階に入ったときに進める（AI が止められている間に、勝手に当たらないように）
		break;
	case AttackStage::Active:
		HitSystem::Activate(hitbox.handle, remaining); // 新しい攻撃の回。段階の長さが過ぎたら、Hitbox の方でも閉じる
		break;
	case AttackStage::Recovery:
	case AttackStage::None:
		HitSystem::Deactivate(hitbox.handle);
		break;
	}
}
```

### D-10 `HTTEngine.vcxproj`

| フィルター | 足すファイル |
|---|---|
| `HTTEngine\Combat` | `DamageTypes.h`・`DamageTypes.cpp` |
| `HTTEngine\Components\Combat` | `HealthComponent.h`・`DamageSourceComponent.h` |
| `HTTEngine\Systems\Combat` | `DamageSystem.h`・`DamageSystem.cpp` |

ここでビルドできる。

## パートE Sandbox のテスト

### E-1 `Sandbox/Parameters/Layers/Layers.json`・`Sandbox/Parameters/DamageTypes/DamageTypes.json`（新しい）

Settings で名前を付けて Save しても同じ物ができる（書く順番は名前の順になる）。

```json
{
    "Layer 1": "Player",
    "Layer 2": "Enemy",
    "Layer 3": "",
    "Layer 4": "",
    "Layer 5": "",
    "Layer 6": "",
    "Layer 7": "",
    "Layer 8": "",
    "Layer 9": "",
    "Layer 10": "",
    "Layer 11": "",
    "Layer 12": "",
    "Layer 13": "",
    "Layer 14": "",
    "Layer 15": "",
    "Layer 16": "",
    "Layer 17": "",
    "Layer 18": "",
    "Layer 19": "",
    "Layer 20": "",
    "Layer 21": "",
    "Layer 22": "",
    "Layer 23": "",
    "Layer 24": "",
    "Layer 25": "",
    "Layer 26": "",
    "Layer 27": "",
    "Layer 28": "",
    "Layer 29": "",
    "Layer 30": "",
    "Layer 31": ""
}
```

```json
{
    "Type 1": "Melee",
    "Type 2": "Projectile",
    "Type 3": "Area",
    "Type 4": "",
    "Type 5": "",
    "Type 6": "",
    "Type 7": "",
    "Type 8": "",
    "Type 9": "",
    "Type 10": "",
    "Type 11": "",
    "Type 12": "",
    "Type 13": "",
    "Type 14": "",
    "Type 15": "",
    "Type 16": "",
    "Type 17": "",
    "Type 18": "",
    "Type 19": "",
    "Type 20": "",
    "Type 21": "",
    "Type 22": "",
    "Type 23": "",
    "Type 24": "",
    "Type 25": "",
    "Type 26": "",
    "Type 27": "",
    "Type 28": "",
    "Type 29": "",
    "Type 30": "",
    "Type 31": ""
}
```

### E-2 `Sandbox/Source/Scripts/TestBullet.h`・`.cpp`（新しい）

弾は COMPONENT / SYSTEM で回す形の見本。

```cpp
#pragma once
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/ECS/GameObject.h"

/// <summary>
/// まっすぐ飛ぶ弾（Sandbox のテスト用。数が多い物は COMPONENT / SYSTEM で回す形の見本）
/// <para>同じ Entity に Hitbox（Phase を Active、Max Targets を 1）と Damage Source を付ける。当てきった（Is Spent）か、寿命が尽きたら消える</para>
/// </summary>
struct TestBullet {
	Vector3 velocity = {0.0f, 0.0f, 10.0f}; // 速さ（m/s。ワールド座標）
	float lifeTime = 3.0f;                  // 残りの寿命（秒）
};

/// <summary>
/// 弾を1つ作る（Hitbox・Damage Source・TestBullet・見た目の球を付ける。付くのは次のフレームの頭）
/// </summary>
/// <param name="position">出す位置（ワールド）</param>
/// <param name="velocity">速さ（m/s。ワールド）</param>
/// <param name="shooter">撃った物（弾のレイヤーはこれと同じ。Instigator にも入れる＝撃った物には当たらない）</param>
/// <param name="targets">当たるレイヤー</param>
/// <param name="damage">ダメージ</param>
GameObject SpawnTestBullet(const Vector3& position, const Vector3& velocity, Handle<Entity> shooter, LayerMask targets, float damage);

/// <summary>
/// 決まった間隔で弾を撃つ（Sandbox のテスト用。Entity の前 +Z へ）
/// </summary>
struct TestBulletSpawner {
	float interval = 1.0f;     // 撃つ間隔（秒）
	float speed = 40.0f;       // 弾の速さ（m/s。速くても Hurtbox をすり抜けないかを見る）
	float damage = 5.0f;       // 弾のダメージ
	LayerMask targets = LayerMask::Everything(); // 弾が当たるレイヤー
	float timer = 0.0f;        // 次に撃つまでの秒（実行中だけ）
};
```

```cpp
#include "Source/Scripts/TestBullet.h"

#include "HTTEngine/Components/Combat/DamageSourceComponent.h"
#include "HTTEngine/Components/Combat/HitboxComponent.h"
#include "HTTEngine/Components/Rendering3D/PrimitiveRendererComponent.h"
#include "HTTEngine/Core/NameList.h"

namespace {
constexpr float kBulletRadius = 0.15f; // 弾の当たる形の半径（m）
constexpr float kBulletScale = 0.3f;   // 弾の見た目の球の大きさ
} // namespace

//=============================================================================
// 弾
//=============================================================================
// ===== 見せ方 =====
COMPONENT(TestBullet, "Test") {
	ui.Field("Velocity###velocity", value.velocity, Tip("速さ（m/s。ワールド座標）"));
	ui.Field("Life Time###lifeTime", value.lifeTime, AtLeast(0.0f).Tip("残りの寿命（秒）"));
}

// ===== 動かす・消す =====
void TestBulletUpdate(Handle<Entity> entity, TestBullet& bullet, TransformComponent& transform, HitboxComponent& hitbox, float deltaTime) {
	// 当てきった（前のフレームの終わりに HitSystem が数えた）か、寿命が尽きたら消える
	bullet.lifeTime -= deltaTime;
	if (hitbox.IsSpent() || bullet.lifeTime <= 0.0f) {
		EntityManager::Destroy(entity);
		return;
	}
	transform.translation += bullet.velocity * deltaTime;
}
SYSTEM(TestBulletUpdate);

// ===== 弾を1つ作る =====
GameObject SpawnTestBullet(const Vector3& position, const Vector3& velocity, Handle<Entity> shooter, LayerMask targets, float damage) {
	const GameObject bullet = GameObject::Create("Bullet");
	bullet.transform.translation = position;
	bullet.transform.scale = {kBulletScale, kBulletScale, kBulletScale};
	bullet.layer = GameObject{shooter}.layer;

	// --- 当たる形：出たときから Active、1体に当たったら Off（Is Spent）---
	HitboxComponent hitbox;
	hitbox.shape.type = HitShapeType::Sphere;
	hitbox.shape.radius = kBulletRadius / kBulletScale; // 大きさを掛けると kBulletRadius になる
	hitbox.targets = targets;
	hitbox.instigator = EntityManager::RefOf(shooter);
	hitbox.phase = HitboxPhase::Active;
	hitbox.maxTargets = 1;
	hitbox.justEnabled = false; // 弾はジャスト判定に使わない
	bullet.AddComponent(hitbox);

	DamageSourceComponent source;
	source.damage = damage;
	bullet.AddComponent(source);

	TestBullet move;
	move.velocity = velocity;
	bullet.AddComponent(move);

	PrimitiveRendererComponent look;
	look.shape = PrimitiveShape::Sphere;
	look.material.color = {1.0f, 0.6f, 0.2f, 1.0f};
	bullet.AddComponent(look);
	return bullet;
}


//=============================================================================
// 弾を撃つ係
//=============================================================================
// ===== 見せ方 =====
COMPONENT(TestBulletSpawner, "Test") {
	ui.Field("Interval###interval", value.interval, AtLeast(0.01f).Tip("撃つ間隔（秒）"));
	ui.Field("Speed###speed", value.speed, AtLeast(0.0f).Tip("弾の速さ（m/s）。速くてもすり抜けないかを見る"));
	ui.Field("Damage###damage", value.damage, AtLeast(0.0f).Tip("弾のダメージ"));
	ui.NameMaskField("Targets###targets", value.targets.bits, Layers::GetNameList(), Tip("弾が当たるレイヤー"));
}

// ===== 撃つ =====
void TestBulletSpawnerUpdate(Handle<Entity> entity, TestBulletSpawner& spawner, TransformComponent& transform, float deltaTime) {
	spawner.timer -= deltaTime;
	if (spawner.timer > 0.0f) {
		return;
	}
	spawner.timer += spawner.interval;
	SpawnTestBullet(GetWorldPosition(transform), GetWorldForward(transform) * spawner.speed, entity, spawner.targets, spawner.damage);
}
SYSTEM(TestBulletSpawnerUpdate);
```

### E-3 `Sandbox/Source/Scripts/HitboxCycler.h`・`.cpp`（新しい）

```cpp
#pragma once
#include "HTTEngine/ECS/Script.h"

/// <summary>
/// Hitbox の窓をくり返し開ける（Sandbox のテスト用。窓を開けるのは呼ぶ側、の見本）
/// <para>Off（Rest Seconds）→ Warning（Warning Seconds）→ Active（Active Seconds）→ Off … をくり返す。Warn に Active の秒も渡して、Hitbox に進めてもらう</para>
/// <para>同じ Entity に Hitbox（と Damage Source）を付ける</para>
/// </summary>
class HitboxCycler : public Script {
public:
	void Start() override;
	void Update() override;
	void Fields(ComponentUI& ui) override;

private:
	float restSeconds_ = 1.5f;    // 窓を閉じている秒
	float warningSeconds_ = 1.2f; // 予告の秒
	float activeSeconds_ = 0.3f;  // 当たる秒
	float startDelay_ = 0.0f;     // 最初の予告までの秒（いくつも置いたとき、ずらす）
	float timer_ = 0.0f;          // 次の予告までの秒（実行中だけ）
};
```

```cpp
#include "Source/Scripts/HitboxCycler.h"

SCRIPT(HitboxCycler, "Test");

//=============================================================================
// 毎フレーム
//=============================================================================
void HitboxCycler::Start() { timer_ = startDelay_; }

void HitboxCycler::Update() {
	const HitboxComponent* hitbox = GetComponent<HitboxComponent>();
	if (hitbox == nullptr || hitbox->phase != HitboxPhase::Off) {
		return; // 窓が開いている間は、Hitbox が時間で進める
	}
	timer_ -= Time::GetDeltaTime();
	if (timer_ > 0.0f) {
		return;
	}
	timer_ = restSeconds_;
	HitSystem::Warn(gameObject.handle, warningSeconds_, activeSeconds_); // 予告 → 時間が来たら Active → 時間が来たら Off
}


//=============================================================================
// Inspector と保存
//=============================================================================
void HitboxCycler::Fields(ComponentUI& ui) {
	ui.Field("Rest Seconds###restSeconds", restSeconds_, AtLeast(0.0f).Tip("窓を閉じている秒"));
	ui.Field("Warning Seconds###warningSeconds", warningSeconds_, AtLeast(0.0f).Tip("予告の秒（Just Lead Time 秒前から、ジャスト判定の範囲が有効）"));
	ui.Field("Active Seconds###activeSeconds", activeSeconds_, AtLeast(0.0f).Tip("当たる秒"));
	ui.Field("Start Delay###startDelay", startDelay_, AtLeast(0.0f).Tip("最初の予告までの秒（いくつも置いたとき、ずらす）"));
}
```

### E-4 `Sandbox/Source/Scripts/HitTestPlayer.h`・`.cpp`（新しい）

```cpp
#pragma once
#include "HTTEngine/ECS/Script.h"

/// <summary>
/// 攻撃の当たり・ダメージを手で試すプレイヤー役（Sandbox のテスト用）
/// <para>I：無敵の切り替え（Health の Invincible）／J：ジャスト判定（HitSystem::TryConsumeJust。入っていれば連続数 +1）／K を押している間：ガード（OnBeforeHitTaken で Guarded・量 × Guard Rate）／F：前（+Z）へ弾を撃つ</para>
/// <para>同じ Entity に Health・Hurtbox・Character Controller（CharacterKeyboardMover で歩く）を付ける。当たり・ジャストは Console に出す</para>
/// </summary>
class HitTestPlayer : public Script {
public:
	void Update() override;
	void OnBeforeHitTaken(HitInfo& hit) override;
	void OnHitTaken(const HitInfo& hit) override;
	void OnHitDealt(const HitInfo& hit) override;
	void Fields(ComponentUI& ui) override;

private:
	// 前へ弾を撃つ（Hitbox・Damage Source・TestBullet を付けた Entity を作る）
	void Shoot();

	float guardRate_ = 0.2f;      // ガードしたときのダメージの倍率
	float bulletSpeed_ = 30.0f;   // 撃つ弾の速さ（m/s）
	float bulletDamage_ = 20.0f;  // 撃つ弾のダメージ
	bool isInvincible_ = false;   // 無敵（I で切り替え）
	int justCount_ = 0;           // ジャストの連続数（被弾で 0 に戻る。3 の次も 0 に戻る）
};
```

```cpp
#include "Source/Scripts/HitTestPlayer.h"

#include <format>

#include "HTTEngine/Components/Combat/HealthComponent.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Input/InputManager.h"
#include "Source/Scripts/TestBullet.h"

SCRIPT(HitTestPlayer, "Test");

namespace {
constexpr int kMaxJustCount = 3;                       // ジャストの連続数の上限（3 の次は 0 に戻る）
constexpr Vector3 kMuzzleOffset = {0.0f, 1.0f, 0.6f};  // 弾を出す所（足元からのずれ）
} // namespace


//=============================================================================
// 毎フレーム
//=============================================================================
void HitTestPlayer::Update() {
	// --- 無敵（I で切り替えて、Health に書く。エンジンはここを見て断る）---
	if (InputManager::IsKeyTriggered(DIK_I)) {
		isInvincible_ = !isInvincible_;
		LogManager::Log(std::format("無敵 {}", isInvincible_ ? "ON" : "OFF"));
	}
	if (HealthComponent* health = GetComponent<HealthComponent>()) {
		health->invincible = isInvincible_;
	}

	// --- ジャスト判定（J を押した瞬間に、ジャスト判定の範囲に入っているか）---
	if (InputManager::IsKeyTriggered(DIK_J)) {
		JustCandidate just;
		if (HitSystem::TryConsumeJust(gameObject.handle, just)) {
			justCount_ = (justCount_ >= kMaxJustCount) ? 0 : justCount_ + 1;
			LogManager::Log(std::format("ジャスト！ {}（連続 {}・Active まで {:.2f} 秒）", GameObject{just.hitbox}.GetName(), justCount_, just.timeToActive));
		} else {
			LogManager::Log("ジャストではない");
		}
	}

	// --- 撃つ（前 +Z へ。当てる相手は Enemy レイヤー）---
	if (InputManager::IsKeyTriggered(DIK_F)) {
		Shoot();
	}
}

// ===== 前へ弾を撃つ =====
void HitTestPlayer::Shoot() {
	const Vector3 position = transform.translation + kMuzzleOffset;
	SpawnTestBullet(position, {0.0f, 0.0f, bulletSpeed_}, gameObject.handle, Layers::GetMask({"Enemy"}), bulletDamage_);
}


//=============================================================================
// 攻撃の当たり
//=============================================================================
// ===== 受ける前（K を押している間はガード）=====
void HitTestPlayer::OnBeforeHitTaken(HitInfo& hit) {
	if (InputManager::IsKeyPressed(DIK_K)) {
		hit.result = HitResult::Guarded;
		hit.damage *= guardRate_;
	}
}

// ===== 受けた =====
void HitTestPlayer::OnHitTaken(const HitInfo& hit) {
	justCount_ = 0; // 被弾で連続数は 0 に戻る
	LogManager::Log(std::format("被弾 {}：{} {:.1f}（残り {:.1f}）位置({:.2f}, {:.2f}, {:.2f}) 向き({:.2f}, {:.2f}, {:.2f})", GameObject{hit.hitbox}.GetName(),
	                            hit.result == HitResult::Guarded ? "ガード" : "ダメージ", hit.damage, hit.healthAfter, hit.point.x, hit.point.y, hit.point.z, hit.direction.x,
	                            hit.direction.y, hit.direction.z));
}

// ===== 当てた =====
void HitTestPlayer::OnHitDealt(const HitInfo& hit) {
	LogManager::Log(std::format("命中 {}：{:.1f}（倍率 {:.1f}・残り {:.1f}{}）", GameObject{hit.receiver}.GetName(), hit.damage, hit.hurtboxMultiplier, hit.healthAfter, hit.isDefeated ? "・撃破" : ""));
}


//=============================================================================
// Inspector と保存
//=============================================================================
void HitTestPlayer::Fields(ComponentUI& ui) {
	ui.Field("Guard Rate###guardRate", guardRate_, Range(0.0f, 1.0f).Tip("K を押している間に受けたダメージの倍率"));
	ui.Field("Bullet Speed###bulletSpeed", bulletSpeed_, AtLeast(0.0f).Tip("F で撃つ弾の速さ（m/s）"));
	ui.Field("Bullet Damage###bulletDamage", bulletDamage_, AtLeast(0.0f).Tip("F で撃つ弾のダメージ"));
}
```

### E-5 `Sandbox/Sandbox.vcxproj`

`Source\Scripts` に `TestBullet.h`・`TestBullet.cpp`・`HitboxCycler.h`・`HitboxCycler.cpp`・`HitTestPlayer.h`・`HitTestPlayer.cpp` を足す。

### E-6 `Sandbox/Resources/scenes/HitDamageTest.scene.json`（新しい）

```json
{
	"version": 5,
	"entities": [
		{
			"id": 1,
			"name": "Main Camera",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 16.0, -16.0],
					"rotation": [45.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Camera": {
					"fovY": 60.0,
					"nearZ": 0.1,
					"farZ": 1000.0,
					"priority": 0
				},
				"Camera Follow": {
					"enabled": true,
					"target": 4,
					"plane": "XZ",
					"mode": "Center",
					"offset": [0.0, 0.0],
					"deadZone": [1.0, 1.0],
					"groundSnapY": true,
					"smoothTime": 0.15,
					"maxLag": [6.0, 6.0],
					"useBounds": false
				},
				"Camera Shake": {}
			}
		},
		{
			"id": 2,
			"name": "Directional Light",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 3.0, 0.0],
					"rotation": [50.0, -30.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Directional Light": {
					"color": [1.0, 1.0, 1.0]
				}
			}
		},
		{
			"id": 3,
			"name": "Ground",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, -0.5, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [40.0, 1.0, 40.0]
				},
				"Collider": {
					"enabled": true,
					"isTrigger": false,
					"shape": "Box",
					"center": [0.0, 0.0, 0.0],
					"physicMaterial": "",
					"size": [1.0, 1.0, 1.0]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [0.55, 0.55, 0.55, 1.0]
				}
			}
		},
		{
			"id": 4,
			"name": "Player",
			"parent": 0,
			"active": true,
			"layer": 1,
			"components": {
				"Transform": {
					"translation": [0.0, 0.0, -6.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Collider": {
					"enabled": true,
					"isTrigger": false,
					"shape": "Capsule",
					"center": [0.0, 0.9, 0.0],
					"physicMaterial": "",
					"radius": 0.4,
					"height ": 1.8
				},
				"Character Controller": {
					"enabled": true
				},
				"Hurtbox": {
					"enabled": true,
					"shape": "Capsule",
					"center": [0.0, 0.9, 0.0],
					"radius": 0.3,
					"height": 1.6,
					"damageMultiplier": 1.0
				},
				"Health": {
					"maxHealth": 100.0,
					"health": 100.0,
					"invincible": false,
					"invincibleAfterHit": 0.5,
					"immuneTypes": 0
				},
				"CharacterKeyboardMover": {
					"moveSpeed": 6.0,
					"jumpSpeed": 6.0,
					"dashSpeed": 18.0,
					"dashTime": 0.18
				},
				"HitTestPlayer": {
					"guardRate": 0.2,
					"bulletSpeed": 30.0,
					"bulletDamage": 20.0
				}
			}
		},
		{
			"id": 5,
			"name": "Player Body",
			"parent": 4,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 0.9, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [0.8, 1.8, 0.8]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Sphere",
					"color": [0.3, 0.6, 1.0, 1.0]
				}
			}
		},
		{
			"id": 6,
			"name": "Boss",
			"parent": 0,
			"active": true,
			"layer": 2,
			"components": {
				"Transform": {
					"translation": [0.0, 0.0, 10.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Hurtbox": {
					"enabled": true,
					"shape": "Capsule",
					"center": [0.0, 1.5, 0.0],
					"radius": 1.0,
					"height": 3.0,
					"damageMultiplier": 1.0
				},
				"Health": {
					"maxHealth": 500.0,
					"health": 500.0,
					"invincible": false,
					"invincibleAfterHit": 0.0,
					"immuneTypes": 0
				}
			}
		},
		{
			"id": 7,
			"name": "Boss Body",
			"parent": 6,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 1.5, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [2.0, 3.0, 2.0]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [0.9, 0.4, 0.4, 1.0]
				}
			}
		},
		{
			"id": 8,
			"name": "Boss Weak Point",
			"parent": 6,
			"active": true,
			"layer": 2,
			"components": {
				"Transform": {
					"translation": [0.0, 1.0, -1.2],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Hurtbox": {
					"enabled": true,
					"shape": "Sphere",
					"center": [0.0, 0.0, 0.0],
					"radius": 0.5,
					"damageMultiplier": 2.0
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Sphere",
					"color": [1.0, 0.9, 0.2, 1.0]
				}
			}
		},
		{
			"id": 9,
			"name": "Circle Attack",
			"parent": 0,
			"active": true,
			"layer": 2,
			"components": {
				"Transform": {
					"translation": [-6.0, 0.0, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Hitbox": {
					"shapeType": "Circle",
					"shapeCenter": [0.0, 0.0, 0.0],
					"shapeRadius": 2.5,
					"shapeInnerRadius": 0.0,
					"shapeHeight": 2.0,
					"targets": 2,
					"instigator": 6,
					"phase": "Off",
					"warningSeconds": 0.0,
					"activateAfterWarning": false,
					"activeSeconds": 0.0,
					"rehitInterval": 0.0,
					"maxTargets": 0,
					"justMargin": 0.8,
					"justLeadTime": 0.4
				},
				"Damage Source": {
					"damage": 10.0,
					"damageType": 3,
					"falloffCurve": "",
					"falloffNear": 0.0,
					"falloffFar": 10.0,
					"hitStop": 0.08,
					"shake": 0.5
				},
				"HitboxCycler": {
					"restSeconds": 1.5,
					"warningSeconds": 1.2,
					"activeSeconds": 0.3,
					"startDelay": 0.5
				}
			}
		},
		{
			"id": 10,
			"name": "Sector Attack",
			"parent": 0,
			"active": true,
			"layer": 2,
			"components": {
				"Transform": {
					"translation": [0.0, 0.0, 4.0],
					"rotation": [0.0, 180.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Hitbox": {
					"shapeType": "Sector",
					"shapeCenter": [0.0, 0.0, 0.0],
					"shapeRadius": 6.0,
					"shapeInnerRadius": 1.5,
					"shapeAngle": 90.0,
					"shapeHeight": 2.0,
					"targets": 2,
					"instigator": 6,
					"phase": "Off",
					"warningSeconds": 0.0,
					"activateAfterWarning": false,
					"activeSeconds": 0.0,
					"rehitInterval": 0.0,
					"maxTargets": 0,
					"justMargin": 0.8,
					"justLeadTime": 0.4
				},
				"Damage Source": {
					"damage": 15.0,
					"damageType": 1,
					"falloffCurve": "",
					"falloffNear": 0.0,
					"falloffFar": 10.0,
					"hitStop": 0.08,
					"shake": 0.5
				},
				"HitboxCycler": {
					"restSeconds": 1.5,
					"warningSeconds": 1.2,
					"activeSeconds": 0.3,
					"startDelay": 1.5
				}
			}
		},
		{
			"id": 11,
			"name": "Line Attack",
			"parent": 0,
			"active": true,
			"layer": 2,
			"components": {
				"Transform": {
					"translation": [6.0, 0.0, 8.0],
					"rotation": [0.0, 180.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Hitbox": {
					"shapeType": "Line",
					"shapeCenter": [0.0, 0.0, 0.0],
					"shapeWidth": 1.5,
					"shapeLength": 14.0,
					"shapeHeight": 2.0,
					"targets": 2,
					"instigator": 6,
					"phase": "Off",
					"warningSeconds": 0.0,
					"activateAfterWarning": false,
					"activeSeconds": 0.0,
					"rehitInterval": 0.0,
					"maxTargets": 0,
					"justMargin": 0.8,
					"justLeadTime": 0.4
				},
				"Damage Source": {
					"damage": 20.0,
					"damageType": 3,
					"falloffCurve": "",
					"falloffNear": 0.0,
					"falloffFar": 10.0,
					"hitStop": 0.1,
					"shake": 0.8
				},
				"HitboxCycler": {
					"restSeconds": 1.5,
					"warningSeconds": 1.2,
					"activeSeconds": 0.3,
					"startDelay": 2.5
				}
			}
		},
		{
			"id": 12,
			"name": "Multi Hit Zone",
			"parent": 0,
			"active": true,
			"layer": 2,
			"components": {
				"Transform": {
					"translation": [-6.0, 0.0, -8.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Hitbox": {
					"shapeType": "Circle",
					"shapeCenter": [0.0, 0.0, 0.0],
					"shapeRadius": 1.5,
					"shapeInnerRadius": 0.0,
					"shapeHeight": 1.0,
					"targets": 2,
					"instigator": 0,
					"phase": "Active",
					"warningSeconds": 0.0,
					"activateAfterWarning": false,
					"activeSeconds": 0.0,
					"rehitInterval": 0.5,
					"maxTargets": 0,
					"justEnabled": false,
					"justMargin": 0.0,
					"justLeadTime": 0.3
				},
				"Damage Source": {
					"damage": 2.0,
					"damageType": 3,
					"falloffCurve": "",
					"falloffNear": 0.0,
					"falloffFar": 10.0,
					"hitStop": 0.0,
					"shake": 0.0
				}
			}
		},
		{
			"id": 13,
			"name": "Turret",
			"parent": 0,
			"active": true,
			"layer": 2,
			"components": {
				"Transform": {
					"translation": [14.0, 1.0, -3.0],
					"rotation": [0.0, -90.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [0.7, 0.3, 0.9, 1.0]
				},
				"TestBulletSpawner": {
					"interval": 1.0,
					"speed": 40.0,
					"damage": 5.0,
					"targets": 2
				}
			}
		}
	]
}
```

### E-7 試し方

Sandbox を起動して、File → Open で **HitDamageTest** を開き、Play。メニューの View → Gizmos → Hit Shapes が付いていること。Console に知らせが出る。

| 置いてある物 | 何が分かるか |
|---|---|
| Player（青） | W・A・S・D で歩く・Space で跳ぶ・Left Shift でダッシュ。I：無敵の切り替え／J：ジャスト判定／K を押している間：ガード／F：前へ弾を撃つ |
| Circle Attack（左） | 円。1.5 秒休み → 1.2 秒予告（黄）→ 0.3 秒当たる（赤）をくり返す |
| Sector Attack（真ん中） | 扇のドーナツ（90 度・半径 1.5〜6）。手前（Player の方）を向いている |
| Line Attack（右） | 幅 1.5・長さ 14 の線 |
| Multi Hit Zone（左手前） | ずっと Active。0.5 秒ごとに当たる（Player の被弾の後の無敵も 0.5 秒なので、ちょうど切れたときに次が当たる） |
| Main Camera | Camera Shake を付けてある（カメラの揺れは Camera Shake の付いたカメラだけが受ける） |
| Turret（右の紫） | 1 秒ごとに 40m/s の弾を -X へ撃つ（z ＝ -3 の線の上） |
| Boss（奥の赤） | HP 500。前に弱点（黄・倍率 2） |

1. 円の予告（黄）の中に立つ → 赤になった瞬間に「被弾 Circle Attack：ダメージ 10.0」が1回だけ出る（3 秒ごと）。カメラが揺れて、一瞬止まる
2. 円の外に立つ → 何も出ない
3. 扇の開きの外・ドーナツの穴の中（Sector Attack のすぐ前）に立つ → 当たらない
4. Inspector で Circle Attack の Hitbox の Height を 0.5 にして、赤になる直前に Space で跳ぶ → 当たらない（足元が 0.3 より上にあれば。Hurtbox の下の端は足元から 0.2）
5. I で無敵を ON にして中に立つ → 当たらない。赤の間に OFF にすると、その場で当たる
6. 予告の終わりごろ（黄の最後の 0.4 秒）に、形の外・水色の線の中で J → 「ジャスト！（連続 1）」。もう一度 J → 「ジャストではない」（同じ攻撃では1回）。そのまま形の中へ入っても当たらない
7. 予告の始めのうちに J → 「ジャストではない」
8. ジャストを 3 回 → 4 回目で連続が 0 に戻る。被弾しても 0 に戻る
9. K を押しながら当たる → 「被弾 …：ガード 2.0」（10 × 0.2）。ヒットストップ・揺れは出ない
10. z ＝ -3 の線（Turret の弾の通り道）に立つ → 弾が当たって消える（速くても通り抜けない）
11. Boss の前で F → 「命中 Boss：40.0（倍率 2.0…）」（弱点と体の両方に入っても1回・倍率の大きい方）
12. Multi Hit Zone に立ち続ける → 2.0 ずつ何度も当たる
13. Stop → HP・窓は元に戻る

Inspector で、Player の Health の Health・Circle Attack の Hitbox の Phase や Shape をつまむと、その場で変わる。Hit Shapes を外すと、ギズモが消える。

## 2章 確かめたこと・確かめていないこと

写し（develop の 2653045 から作った別のフォルダ）で、パートA〜E をそのまま入れて確かめた。

- 3構成（Debug・Development・Release）でビルド。この変更のファイルに警告は 0
- 符号付き距離の式：縁を細かい点にして総当たりで測った値と、44000 点でずれ 0.000001 以下（1.2）
- 写しだけの自動テスト（md には入れていない）を Debug と Release で動かし、35 項目全部通った：円・扇（回した向き）・線の当たりと外れ／同じ攻撃で1回／当たった位置と向き／無敵の間は当たらず切れたら当たる／ジャストの時間の窓・1回だけ・よけた攻撃は当たらない／ジャスト範囲だけでは当たらない／予告をやめたらジャストの記録が消える／多段ヒット（1 秒で 4 回）／被弾の後の無敵で間引かれる／Max Targets と Is Spent／レイヤーと撃った本人／1フレーム 3m の弾がすり抜けない／ガード・断る／弱点（1回・倍率 2）／効かない種類／phase を直接書く・予告から自動で Active／倒れた・ヒットストップ・攻撃表の HP の割合・OnHitDealt と OnHitTaken／倒れた物にもう当たらない／距離の倍率（Falloff Curve）／レイヤーと Hitbox の保存／名前の一覧
- 自動テストで見つけて直した：窓が開いた最初のフレームに Hurtbox の動きの跡を調べて、開く前の位置で当たる／Hurtbox をワープさせると、通っていない所の攻撃に当たる（1.5）
- HitDamageTest を開いて Play：読み込みの警告なし。ギズモの色と形を画面で確認。Player を Circle Attack の中に置いた写しで、3 秒ごとに「被弾 Circle Attack：ダメージ 10.0」が1回ずつ出る（HP 100 → 90 → 80 → 70）

確かめていない：

- キーを手で押す操作（E-7 の 1〜13）
- Inspector の Layer 欄・名前の一覧の選択肢・Undo・Prefab の置いた所ごとのレイヤーの上書き
- AL4_01 で使うこと

## 3章 後で足す候補

- **床の予告の描画**：`HitShape` の値を定数バッファに入れ、`CalcSignedDistance2D` と同じ式をピクセルシェーダーに書いて、床に塗る（Decal か、床に置いた板）。d ≦ 0 を塗り、d ≦ Just Margin を薄く、Warning の残りで塗る範囲を広げる演出もできる。値は Hitbox の物をそのまま読む（予告用の形を持たない）
- **Tag**：Entity に名前の札を付けて探す。絞り込みが要るときは `targets` を `HitTargetFilter`（レイヤー ＋ Tag）にして足す
- **CollisionSystem・Raycast のレイヤー**：`CollisionFilter` の代わりに `LayerMask` で絞る・Unity の衝突の表（どのレイヤーとどのレイヤーが当たるか）
- **当たった所に出す物**：Damage Source に「当たった所に出す Prefab・鳴らす音」を足す（手応えと同じ所）
- **同じ攻撃の形をいくつも**（剣の根元と先など）：同じ攻撃の回の記録を、いくつかの Hitbox で分け合う（Group）
- **攻撃する側の倍率**（強化・弱体）：受ける前の問い合わせと対に、攻撃側の Script に聞く
- **ダメージを直接与える・回復**：`DamageSystem::ApplyDamage`（毒・落下）・`Heal`。今は Health の health を直接書けば減るが、無敵・知らせは通らない
- **レイヤーを子にもまとめて変える**：Unity の「子も変えますか？」
- **Hurtbox の数がとても多いとき**：今は Hitbox ごとに全部の Hurtbox を調べる（弾 数百 × Hurtbox 数個なら軽い）。足りなくなったら、Hurtbox を格子に分けて近い物だけ調べる
