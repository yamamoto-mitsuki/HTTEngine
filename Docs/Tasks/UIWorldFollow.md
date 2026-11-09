参考：
- Unreal の Widget Component（Space が Screen：ワールドの物に付けた UI を、画面の上に描く）：https://dev.epicgames.com/documentation/en-us/unreal-engine/widget-components-in-unreal-engine
- Unreal の ProjectWorldLocationToWidgetPosition（ワールドの点 → UI の位置）：https://dev.epicgames.com/documentation/en-us/unreal-engine/BlueprintAPI/Viewport/ProjectWorldtoScreen
- Unity の Camera.WorldToScreenPoint と、LateUpdate で UI を置く書き方：https://docs.unity3d.com/ScriptReference/Camera.WorldToScreenPoint.html ・ https://docs.unity3d.com/ScriptReference/MonoBehaviour.LateUpdate.html
- Unity の Driven Properties（Layout などが RectTransform の値を決め、保存の値を汚さない考え方）：https://docs.unity3d.com/Packages/com.unity.ugui@1.0/manual/UIAutoLayout.html
- 画面の端に寄せる矢印（オフスクリーンインジケーター）の考え方：https://www.gamedeveloper.com/design/off-screen-indicators
- `std::atan2`：https://en.cppreference.com/w/cpp/numeric/math/atan2
- 前の md：Docs/Tasks/VectorShape.md（UI の位置・Anchor・Pivot）・Docs/Tasks/VirtualCamera.md（カメラの1フレームの順番）・Docs/Tasks/UIAnimation.md（UI の Tween・Timeline）

# UI をワールドの物・点に重ね続ける（UI World Follow）

画面に重ねる UI（Image・Text・Vector Shape・Polyline）を、**ワールドの物（Entity ＋ ずれ）か、ワールドの点に重ね続ける** Component「**UI World Follow**」をエンジンに足す。

例：敵に重ねるロックオンのマーク・名札・当たった所に出すダメージの数字・画面の端に出す「あっちにいる」の矢印。

- **パートA ワールド行列の手直しの窓口**（`ECS/EntityManager`）：UpdateTransforms がワールド行列を作った直後に呼んでもらう関数を登録できるようにする
- **パートB UI World Follow**（新しい Component と System）：重ねる点をカメラで写して、置く所を決める。画面の外・カメラの後ろの扱い（そのまま・隠す・端に寄せる）。結果を Script から読める
- **パートC 組み込む**（`EngineSystems`・`SpriteRenderSystem`・`VectorShapeSystem`）：カメラの後に回す。隠した物は描かず、マウスも当てない
- **パートD Sandbox**：円を描いて動く的・端の矢印・浮く数字の見本と、テストシーン UIWorldFollowTest

パートは **A → B → C → D の順に写す**。A の後・B の後・C の後の、どこでもビルドできる（B の後は Inspector に出て保存もされるが、まだ動かない。C で動く）。

ブランチは `feature/UIWorldFollow`（develop e3e8343 から）。一覧の Field（Docs/Tasks/ListField.md）とは別の機能なので、どちらを先に写してもよい。

写しで確かめたこと：3構成（Debug・Development・Release）のビルドでエラー・自分のファイルの警告 0。自動テスト 18 項目（Debug・Development）が合格。その中に「カメラが追いかけている間、置いた所と、そのフレームのカメラで写した所の差が 0.0000 px」「前のフレームのカメラで置くと 4.4 px ずれる（Script で置けない理由）」がある。エディタで停止中・再生中の見た目と Inspector を撮って確かめた。Release でもテストシーンを動かし、結果の値（ログ）が Debug と同じ計算になることを確かめた。

## 0章 全体の形

### 0.1 何が困っていたか

- `Camera::WorldToScreenPoint` はあるが、それを毎フレーム UI に当てる Component が無い
- **Script で置くと揺れる**：1フレームの順番が「Script・SYSTEM → … → カメラの追いかけ（`CameraSystem::UpdateFollow`）→ `CameraSystem::Update`（カメラに写す）」なので、Script が使えるのは**前のフレームのカメラ**。カメラが動くたびに、マークがカメラの動いた分だけずれて揺れる。カメラの後に置く仕組みは、エンジンにしか作れない
- UI の位置は Transform の Translation で決まるので、Script が Translation を毎フレーム書くと、UI のアニメ（Tween・Timeline）が書く Translation とぶつかる

### 0.2 作る物・直す物

| パート | ファイル | すること |
|---|---|---|
| A | `ECS/EntityManager.h`・`.cpp` | `TransformAdjuster`・`AddTransformAdjuster`。UpdateTransforms が、1つ作るたびに呼ぶ |
| B | `Components/UI/UIWorldFollowComponent.h`（新しい） | `OffscreenMode`・`UIWorldFollowComponent`（設定と、毎フレームの結果） |
| B | `Systems/UI/UIWorldFollowSystem.h`・`.cpp`（新しい） | 写す・端に寄せる・置く・Anchor の警告・見せ方 |
| B | `HTTEngine.vcxproj`・`.filters` | 上の3つを足す |
| C | `Systems/EngineSystems.cpp` | Initialize・Release・Update（`CameraSystem::Update` の後） |
| C | `Systems/Rendering2D/SpriteRenderSystem.cpp` | `CalcGroupAlpha` で隠した物を 0 に・`IsMouseOver` で隠した物に当てない |
| C | `Systems/UI/VectorShapeSystem.cpp` | `IsMouseOver` で隠した物に当てない |
| D | `Sandbox/Source/Scripts/WorldFollowSamples.h`・`.cpp`（新しい） | CircleMover・EdgeArrow・FloatUpText |
| D | `Sandbox/Resources/scenes/UIWorldFollowTest.scene.json`（新しい） | テストシーン |
| D | `Sandbox/Sandbox.vcxproj`・`.filters` | 上の2つを足す |

### 0.3 使い方

**Inspector**：UI の Entity（Image・Text・Vector Shape・Polyline のどれかが付いている物か、その親）に Add Component → UI → **UI World Follow**。

| 項目 | 意味 |
|---|---|
| Enabled | false の間は重ねない（Transform のとおりに置く） |
| Target | 重ねる相手（none なら World Offset をワールドの点として使う） |
| World Offset | 相手の位置からのずれ（ワールドの向き。m）。相手が none なら、重ねるワールドの点 |
| Offscreen | 重ねる点が画面の外・カメラの後ろのときの扱い（Keep・Hide・Clamp To Edge） |
| Edge Margin | Clamp To Edge で、画面の端から内側へ空ける幅（ピクセル。マークが切れない大きさにする） |

- 置く所は「重ねる点を写した所 ＋ **Transform の Translation の x・y**（ピクセル。上が＋）」。Translation は書き換えないので、**Tween・Timeline で Translation を動かすと、写した点からのずれが動く**（当たった所からポンと上へ浮く、が親子に分けずに書ける）
- 子は、置いた後の位置について来る（マークの輪を子に何個も並べられる）
- 停止中も重ねる（エディタで、重なった見た目を見ながら置ける）。Game ビューも Scene ビューの上の UI も、ゲームのメインのカメラで写した所に出る
- **決まり：この Entity と子の Image などの Anchor は Center にする**（違う物があると、Inspector に注意が出る。1.4）

**Script から**：相手やずれは、Component の値を書くだけ（次の置き直しから効く）。

```cpp
UIWorldFollowComponent* follow = mark.GetComponent<UIWorldFollowComponent>();
follow->target = EntityManager::RefOf(lockTarget.handle); // ロックオンの相手が変わった
follow->worldOffset = {0.0f, 1.2f, 0.0f};                  // 胸の高さ

// ダメージの数字：相手は none にして、当たった点をそのまま書く
number->target = {};
number->worldOffset = hit.point;
```

**結果を読む**（UI World Follow が毎フレーム書く。保存しない）

| 値 | 意味 |
|---|---|
| `isHidden` | 隠している（画面の外・カメラの後ろ・相手が居ない）。この Entity と子は描かず、マウスも当たらない |
| `isOnScreen` | 重ねる点が画面の中に写っている（カメラの前で、画面の端の内側） |
| `isClamped` | 画面の端に寄せた（Clamp To Edge のときだけ） |
| `screenPosition` | 置いた所（画面の真ん中から、ピクセル。上が＋。Transform のずれは入らない） |
| `edgeAngle` | 画面の真ん中から重ねる点への向き（度。右が 0、上が 90。端の矢印を回すのに使う） |

結果は UI World Follow が **Script の後**（カメラの後）に書くので、Script の Update で読むのは**前のフレームの結果**（端の矢印の向きが1フレーム遅れるが、見て分からない）。

### 0.4 決めたこと

| 項目 | 決めたこと | 理由 |
|---|---|---|
| md の分け方 | **ListField.md と UIWorldFollow.md の2つ**（ユーザーが決めた） | 層が違い、互いに使わない |
| 名前 | **UI World Follow**（`UIWorldFollowComponent`・`UIWorldFollowSystem`）（ユーザーが決めた） | Camera Follow と同じ「追う」の言い方で、UI の物と分かる。Image の「Anchor」と混ざらないように Anchor は使わない |
| 置く所を書く先 | **ワールド行列だけ**（ユーザーが決めた）。Translation は「写した点からのずれ」として残す | Unreal の Widget Component（Screen）と同じ考え方。Tween・Timeline が Translation を動かしてもぶつからない（追う親・動く子に分ける決まりが要らない）。停止中も重ねて見せられて、シーンの値が汚れない（Unity の Driven Properties と同じ考え方） |
| 画面の外・後ろ | **Keep・Hide・Clamp To Edge から選ぶ＋結果を読める**（ユーザーが決めた） | 端の矢印を回す・マークを小さくするはゲームごとに違うので、エンジンは結果（向き・寄せたか）だけ渡す |
| 更新の順番 | `CameraSystem::Update` の**すぐ後**。置いたらワールド行列を計算し直す | カメラの追いかけ（UpdateFollow）と、カメラへ写す（Update）の後でないと、このフレームのカメラで写せない |
| ワールド行列への入れ方 | `EntityManager::AddTransformAdjuster` で、**UpdateTransforms がワールド行列を作るたびに**入れてもらう | UpdateTransforms は1フレームに何度も呼ばれる（物理の後・Timeline のプレビューの後など）。後から呼ばれても置いた所が消えない。子のワールド行列の計算も、今の UpdateTransforms がそのまま行う（同じ計算を写さない） |
| 親子 | 親の**大きさ・回転はずれと見た目に掛かる**。親の**位置は使わない** | HUD をまとめる親を小さくすると、ずれも一緒に小さくなる。位置は写した点で決まる |
| Anchor | この Entity と子の UI の **Anchor は Center** にする決まり。違えば Inspector に注意 | Anchor の場所はワールド行列の後に足される（`CalcScreenPoint`）ので、Center 以外は画面の端の分だけずれる。4つの描く係を全部変えずに済む |
| どのカメラ | ゲームのメインのカメラ（`EngineSystems::Update` に来る `gameCamera`＝`CameraSystem::GetSharedMainCamera`） | Game ビューも、Scene ビューに重ねる UI も、ゲームの画面の座標で描いている |
| 隠し方 | Alpha Group と同じ「親までたどって掛けるアルファ」（`CalcGroupAlpha`）を 0 にする。マウスも当てない | Image・Text・Vector Shape・Polyline の4つがまとめて消える。Entity の有効・無効は触らないので、Script や Timeline が持っている Enabled とぶつからない |
| 相手が居なくなった・無効 | **隠す** | 消えた相手の最後の所にマークが残らないように |
| カメラの後ろ（Keep） | 隠す | 写すと裏返った所（反対側）に出てしまう |

### 0.5 1フレームの順番

```
シーンの Update → Script の Update → SYSTEM
→ 攻撃表・アニメ・UI State・Timeline → ワールド行列
→ Character Controller・物理 → ワールド行列
→ カメラの追いかけ（Camera Follow）→ ワールド行列
→ CameraSystem::Update（このフレームのカメラの位置・向き・行列）
→ ★ UIWorldFollowSystem::Update（写す → 置く所を決める → ワールド行列を計算し直す）
→ パーティクル・ライト・…・当たり判定 → 描画
```

- ★ で決めた置く所は、次に Update が来るまで残る。その間に UpdateTransforms が呼ばれても（Timeline のプレビューの後など）、手直しの関数が同じ所に入れる
- 次のフレームの Script は、前のフレームに置いた所（ワールド行列）と結果を読む

### 0.6 今回やらないこと

3章にまとめた（距離で大きさを変える・カメラを選ぶ・端の矢印を回す選択肢・骨に重ねる など）。

## 1章 解説

### 1.1 写す計算（ワールドの点 → UI のピクセル）

`Camera::WorldToScreenPoint` で正規化座標（画面の真ん中が 0、端が ±1）にして、ゲームの画面の半分の大きさを掛ける。UI の Transform の数え方（画面の真ん中から、ピクセル。上が＋）と同じになる。

```
pixel = (ndc.x × 幅 / 2, ndc.y × 高さ / 2)
```

### 1.2 カメラの後ろの点の向き

カメラの後ろの点は、ビュー・プロジェクション行列を掛けた w（奥行き）がマイナスになる。w で割ると**符号が裏返り**、反対側に写ってしまう。そこで後ろの点は w で割らず、**カメラから見た位置（ビュー空間の x・y）**を向きに使う。

```
direction = (view.x × 投影の[0][0] × 幅/2, view.y × 投影の[1][1] × 高さ/2)
```

投影行列の `[0][0]`・`[1][1]` は横・縦の広がりの倍率（画角と縦横の比）。掛けておくと、前にある点の `pixel` と同じ比の向きになる（右後ろの点は右の端に寄る。振り向く向きと合う）。

### 1.3 端に寄せる計算と `std::atan2`

端から Edge Margin だけ内側の四角（`inner`）の縁に、真ん中から `direction` の向きに伸ばした線が当たる所に置く。

```
scale = min(inner.x / |direction.x|, inner.y / |direction.y|)   // 横の縁と縦の縁の、先に当たる方
position = direction × scale
```

`edgeAngle = atan2(direction.y, direction.x)`（度にする）。`std::atan2(y, x)` は、x が 0 でも割らずに、4つの向きの全部（-180〜180 度）を正しく返す。右が 0、上が 90、左が ±180、下が -90。矢印を上向きに作ったら、`edgeAngle - 90` だけ Z で回すとその向きを指す（Sandbox の EdgeArrow）。

### 1.4 Anchor を Center にする理由

Image などの位置は `CalcScreenPoint` で「ワールド行列の x・y ＋ Anchor の場所」になる。UI World Follow はワールド行列の位置を「画面の真ん中からのピクセル」で決めるので、Anchor が TopLeft だと、さらに左上の角の分（幅の半分・高さの半分）がずれる。

4つの描く係（Image・Text・Vector Shape・Polyline）に「UI World Follow の下なら Anchor を使わない」を入れる案もあるが、描くたびに親をたどる所が増え、マウスの当たり・エディタの枠など Anchor を使う所を全部直すことになる。そこで決まりにして、**エディタだけ**で Center でない物を数え、Inspector に注意を出す（Center でない物の数だけ親をたどるので軽い）。

### 1.5 ワールド行列の手直し（TransformAdjuster）の考え方

`EntityManager::UpdateTransforms` は、浅い順に「自分の行列 × 親のワールド行列」を作る。UI World Follow は、作った**直後**（子に掛ける前）に、位置の所だけを書き換えてもらう。

```
今の位置 ＝ 親の位置 ＋ 親の向き・大きさを掛けた Translation
置く位置 ＝ 写した所 ＋（今の位置 − 親の位置）
```

親の位置だけを引いて写した所に置き換えるので、ずれ・回転・大きさはそのまま残る。子は、書き換えた後の行列を掛けるので、ついて来る。

置く物の一覧は `unordered_map<Handle の index, 置く所>` で持つ。UpdateTransforms は全部の Entity について聞きに来るので、探すのが速い表にする。置く物が無いフレームは、表が空なのを見てすぐ戻る。

## パートA ワールド行列の手直しの窓口

### A-1 `ECS/EntityManager.h`

`AddDestroyListener` の後に足す。

```cpp
	// ===== ワールド行列の手直し =====
	// UpdateTransforms が1つの Entity のワールド行列を作った直後（子に掛ける前）に呼んでほしい関数を登録する
	// ワールド行列を自分の計算で決める System（UI World Follow：UI を画面の上の決まった所に置く）が使う
	// parent は親の Transform（親が居なければ nullptr。親のワールド行列はもう決まっている）
	// UpdateTransforms は1フレームに何度も呼ばれるので、そのたびに同じ手直しが入る（後から呼んでも、手直しが消えない）
	using TransformAdjuster = void (*)(Handle<Entity> handle, TransformComponent& transform, const TransformComponent* parent);
	static void AddTransformAdjuster(TransformAdjuster adjuster);
```

メンバの `destroyListeners_` の次に足す。

```cpp
	std::vector<DestroyListener> destroyListeners_; // 消える直前に呼ぶ関数（AddDestroyListener）
	std::vector<TransformAdjuster> transformAdjusters_; // ワールド行列を作った直後に呼ぶ関数（AddTransformAdjuster）
```

### A-2 `ECS/EntityManager.cpp`

`UpdateTransforms` の、親の行列を掛ける所を書き換えて、手直しを呼ぶ。後に `AddTransformAdjuster` を足す。

```cpp
		// 自分の分（大きさ → 回転 → 位置）
		transform->worldMatrix = MakeAffineMatrix(transform->scale, transform->rotation, transform->translation);
		// 親がいれば、親のワールド行列を掛ける（親は先に確定している）
		const TransformComponent* parentTransform = Get<TransformComponent>(entity->parent);
		if (parentTransform != nullptr) {
			transform->worldMatrix = transform->worldMatrix * parentTransform->worldMatrix;
		}
		// 手直し（子は、手直しした後の行列を掛ける）
		for (const TransformAdjuster adjuster : self.transformAdjusters_) {
			adjuster(handle, *transform, parentTransform);
		}
	}
}

void EntityManager::AddTransformAdjuster(TransformAdjuster adjuster) {
	EnsureInitialized();
	instance_->transformAdjusters_.push_back(adjuster);
}
```

- `parentTransform` を `if` の外に出したのは、手直しの関数にも渡すため

## パートB UI World Follow

### B-1 `Components/UI/UIWorldFollowComponent.h`（新しい）

```cpp
#pragma once
#include <cstdint>
#include <type_traits>

#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector2.h"
#include "HTTEngine/Math/Vector3.h"

// 画面に重ねる UI（Image・Text・Vector Shape・Polyline）を、ワールドの物・ワールドの点に重ね続ける Component（カテゴリ：UI）
// 例：敵に重ねるマーク・名札・当たった所に出す数字・画面の端に出す「あっちにいる」の矢印
//
// 置き方（Unreal の Widget Component の Screen と同じ考え方）
//   ・重ねる点を、ゲームのメインのカメラで画面に写す。写した所を、この Entity のワールド行列の位置にする（カメラが動いた後に置くので、カメラを追いかけても揺れない）
//   ・Transform の Translation は書き換えない。x・y は「写した点からのずれ（ピクセル。上が＋）」として足される
//     → Tween・Timeline が Translation を動かすと、写した点からのずれが動く（当たった所からポンと上へ浮く、を親子に分けずに書ける）
//   ・親の大きさ・回転は、ずれと見た目に掛かる。親の位置は使わない（位置は写した点で決まる）
//   ・子は、置いた後の位置について来る（マークの輪を子に何個も並べられる）
//   ・停止中も重ねる（エディタで、重なった見た目を見ながら置ける）
//
// 決まり：この Entity と子の Image などの Anchor は Center にする（Anchor の場所は、ワールド行列の後に足されるので、Center 以外はずれる。Inspector に警告が出る）
// Script から：GetComponent<UIWorldFollowComponent>() の target・worldOffset を書くと、その次の置き直しから相手が変わる。結果（isOnScreen など）を読める

/// <summary>
/// 重ねる点が画面の外・カメラの後ろにあるときの扱い
/// </summary>
enum class OffscreenMode {
	Keep,        // そのまま（画面の外に出る。カメラの後ろなら隠す）
	Hide,        // 隠す（画面の中に入ったら、また出る）
	ClampToEdge, // 画面の端（Edge Margin だけ内側）に寄せる。カメラの後ろなら、向きの合う端に寄せる
};

/// <summary>
/// UI をワールドの物・ワールドの点に重ね続ける
/// </summary>
struct UIWorldFollowComponent {
	bool enabled = true;                           // false の間は重ねない（Transform のとおりに置く）
	EntityRef target;                              // 重ねる相手（none なら World Offset をワールドの点として使う）
	Vector3 worldOffset = {0.0f, 0.0f, 0.0f};      // 相手の位置からのずれ（ワールドの向き。m）。相手が none なら、重ねるワールドの点
	OffscreenMode offscreen = OffscreenMode::Hide; // 重ねる点が画面の外・カメラの後ろのときの扱い
	float edgeMargin = 48.0f;                      // Clamp To Edge で、画面の端から内側へ空ける幅（ピクセル。マークが切れない大きさにする）

	// --- 結果（UIWorldFollowSystem が毎フレーム書く。保存しない。Script から読む）---
	bool isHidden = false;                     // 隠している（画面の外・カメラの後ろ・相手が居ない）。この Entity と子は描かず、マウスも当たらない
	bool isOnScreen = false;                   // 重ねる点が画面の中に写っている（カメラの前で、画面の端の内側）
	bool isClamped = false;                    // 画面の端に寄せた（Clamp To Edge のときだけ）
	Vector2 screenPosition = {0.0f, 0.0f};     // 置いた所（画面の真ん中から、ピクセル。上が＋。Transform のずれは入らない）
	float edgeAngle = 0.0f;                    // 画面の真ん中から重ねる点への向き（度。右が 0、上が 90。端の矢印を回すのに使う）
	uint32_t nonCenterAnchorCount = 0;         // エディタ用：この Entity と子の UI で、Anchor が Center でない物の数（Inspector の警告）
};

static_assert(std::is_trivially_copyable_v<UIWorldFollowComponent>);
```

### B-2 `Systems/UI/UIWorldFollowSystem.h`（新しい）

```cpp
#pragma once
#include "HTTEngine/Components/UI/UIWorldFollowComponent.h"
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"

// 前方宣言
class Camera;

/// <summary>
/// UI World Follow（UI をワールドの物・点に重ね続ける）を動かす
/// <para>Update はカメラの後（EngineSystems::Update の CameraSystem::Update の後）。カメラを動かした後の絵で写すので、カメラを追いかけても揺れない</para>
/// <para>置いた所は、EntityManager::UpdateTransforms がワールド行列を作るたびに入れる（AddTransformAdjuster）。同じフレームの後で UpdateTransforms が呼ばれても、置いた所が消えない</para>
/// </summary>
class UIWorldFollowSystem {
public:
	// Component を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// 重ねる点を camera で画面に写して、置く所を決め、ワールド行列を計算し直す（停止中も動く）
	/// </summary>
	/// <param name="camera">ゲームのメインのカメラ（CameraSystem::Update で、このフレームの位置・向きにした物）。nullptr なら何もしない</param>
	static void Update(const Camera* camera);

	// その Entity か親のどれかが、UI World Follow に隠されているか（描かない・マウスを当てない）
	static bool IsHiddenInHierarchy(Handle<Entity> entity);
};
```

### B-3 `Systems/UI/UIWorldFollowSystem.cpp`（新しい）

```cpp
#include "UIWorldFollowSystem.h"

#include <cmath>
#include <cfloat>
#include <format>
#include <memory>
#include <string>
#include <algorithm>
#include <unordered_map>

#include "HTTEngine/Camera/Camera.h"
#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Graphics/Renderer/GameScreen.h"
#include "HTTEngine/Math/MathUtility.h"
#ifdef USE_IMGUI
#include "HTTEngine/Components/UI/ImageComponent.h"
#include "HTTEngine/Components/UI/PolylineComponent.h"
#include "HTTEngine/Components/UI/TextComponent.h"
#include "HTTEngine/Components/UI/VectorShapeComponent.h"
#endif

namespace {
constexpr float kMinDirectionLength = 1.0e-4f; // 向きがこれより短ければ、向きが無いとみなす（真後ろ）。そのときは下の端に寄せる

/// <summary>
/// 置く所1つ（UpdateTransforms がワールド行列を作るたびに入れる）
/// </summary>
struct Placement {
	uint32_t generation = 0;        // Handle の generation（同じ番号を、消えた後の別の Entity が使ったときに間違えないように）
	Vector2 position = {0.0f, 0.0f}; // 置く所（画面の真ん中から、ピクセル。上が＋）
};

// Handle の index → 置く所。Update で作り直す
// UpdateTransforms は全部の Entity について聞きに来るので、探すのが速い unordered_map にする
std::unordered_map<uint32_t, Placement> placements;

/// <summary>
/// 重ねる点を、画面に写した結果
/// </summary>
struct ScreenProjection {
	bool isBehind = false;            // カメラの後ろ
	Vector2 pixel = {0.0f, 0.0f};     // 写った所（画面の真ん中から、ピクセル。上が＋。カメラの後ろなら使わない）
	Vector2 direction = {0.0f, 0.0f}; // 画面の真ん中から、点の方への向き（ピクセルの縦横の比で。後ろでも、振り向く向きが合う）
};

//=============================================================================
// 写す・寄せる
//=============================================================================
// ===== 画面の半分の大きさ（ピクセル）=====
Vector2 HalfScreen() { return {GameScreen::GetWidth() * 0.5f, GameScreen::GetHeight() * 0.5f}; }

// ===== ワールドの点を画面に写す =====
ScreenProjection Project(const Camera& camera, const Vector3& world) {
	ScreenProjection result;
	const Vector2 half = HalfScreen();
	Vector2 ndc;
	if (camera.WorldToScreenPoint(world, &ndc)) {
		// 正規化座標（端が ±1）→ UI のピクセル（画面の真ん中から。Image の Transform と同じ数え方）
		result.pixel = {ndc.x * half.x, ndc.y * half.y};
		result.direction = result.pixel;
		return result;
	}
	// --- カメラの後ろ：w で割ると裏返るので、カメラから見た向き（ビュー空間の x・y）を使う ---
	// 投影行列の [0][0]・[1][1] は、横・縦の広がりの倍率（画角と縦横の比）。掛けておくと、画面の上での向きと同じ比になる
	result.isBehind = true;
	const Vector3 view = TransformPoint(world, camera.GetViewMatrix());
	const Matrix4x4& projection = camera.GetProjectionMatrix();
	result.direction = {view.x * projection.m[0][0] * half.x, view.y * projection.m[1][1] * half.y};
	return result;
}

// ===== 点が、端から margin だけ内側の四角の中か =====
bool IsInside(const Vector2& pixel, float margin) {
	const Vector2 half = HalfScreen();
	return std::abs(pixel.x) <= half.x - margin && std::abs(pixel.y) <= half.y - margin;
}

// ===== 向きの先の、端から margin だけ内側の四角の縁の点 =====
// 真ん中から direction の向きへ伸ばして、四角の縁に当たった所（横の縁と縦の縁の、先に当たる方）
Vector2 ClampToEdge(Vector2 direction, float margin) {
	const Vector2 half = HalfScreen();
	const Vector2 inner = {(std::max)(half.x - margin, 0.0f), (std::max)(half.y - margin, 0.0f)};
	if (Length(direction) < kMinDirectionLength) {
		direction = {0.0f, -1.0f}; // 真後ろ：下の端
	}
	const float scaleX = (std::abs(direction.x) > 0.0f) ? inner.x / std::abs(direction.x) : FLT_MAX;
	const float scaleY = (std::abs(direction.y) > 0.0f) ? inner.y / std::abs(direction.y) : FLT_MAX;
	const float scale = (std::min)(scaleX, scaleY);
	return {direction.x * scale, direction.y * scale};
}

// ===== 1つ置く（結果を Component に書く。置くなら true）=====
bool Place(const Camera& camera, const Vector3& world, UIWorldFollowComponent& follow) {
	const ScreenProjection projection = Project(camera, world);
	follow.isOnScreen = !projection.isBehind && IsInside(projection.pixel, 0.0f);
	follow.edgeAngle = std::atan2(projection.direction.y, projection.direction.x) * MathUtility::kRadToDeg;

	switch (follow.offscreen) {
	case OffscreenMode::Keep:
		follow.isHidden = projection.isBehind; // 後ろの点は、写すと裏返った所に出るので隠す
		follow.screenPosition = projection.pixel;
		break;
	case OffscreenMode::Hide:
		follow.isHidden = !follow.isOnScreen;
		follow.screenPosition = projection.pixel;
		break;
	case OffscreenMode::ClampToEdge:
		// 端から Edge Margin の内側にあれば、そのまま。外か後ろなら、向きの先の縁に寄せる
		if (!projection.isBehind && IsInside(projection.pixel, follow.edgeMargin)) {
			follow.screenPosition = projection.pixel;
		} else {
			follow.screenPosition = ClampToEdge(projection.direction, follow.edgeMargin);
			follow.isClamped = true;
		}
		break;
	}
	return !follow.isHidden;
}

// ===== 結果を消す（重ねていない状態）=====
void ResetResults(UIWorldFollowComponent& follow) {
	follow.isHidden = false;
	follow.isOnScreen = false;
	follow.isClamped = false;
	follow.nonCenterAnchorCount = 0;
}

//=============================================================================
// ワールド行列に置く（EntityManager::UpdateTransforms から、Entity ごとに呼ばれる）
//=============================================================================
void ApplyPlacement(Handle<Entity> entity, TransformComponent& transform, const TransformComponent* parent) {
	if (placements.empty()) {
		return; // 重ねている物が無いフレームは、何もしない（探す手間も掛けない）
	}
	const auto found = placements.find(entity.index);
	if (found == placements.end() || found->second.generation != entity.generation) {
		return;
	}
	// 今のワールド行列の位置は「親の位置 ＋ 親の向き・大きさを掛けた Translation」
	// 親の位置だけを引いて、写した点に置き換える（ずれ・回転・大きさは、そのまま残る）
	Matrix4x4& world = transform.worldMatrix;
	const Vector3 parentPosition = (parent != nullptr) ? GetWorldPosition(*parent) : Vector3{0.0f, 0.0f, 0.0f};
	world.m[3][0] = found->second.position.x + (world.m[3][0] - parentPosition.x);
	world.m[3][1] = found->second.position.y + (world.m[3][1] - parentPosition.y);
}

//=============================================================================
// エディタ：Anchor が Center でない物を数える（Inspector の警告）
//=============================================================================
#ifdef USE_IMGUI
// 自分か親で、一番近い UI World Follow（無ければ nullptr）
UIWorldFollowComponent* FindFollower(Handle<Entity> entity) {
	for (Handle<Entity> current = entity; current.IsValid();) {
		if (UIWorldFollowComponent* follow = EntityManager::Get<UIWorldFollowComponent>(current); follow != nullptr && follow->enabled) {
			return follow;
		}
		const Entity* owner = EntityManager::Get(current);
		current = (owner != nullptr) ? owner->parent : Handle<Entity>{};
	}
	return nullptr;
}

// T（Anchor を持つ UI の Component）のうち、Center でない物を、一番近い UI World Follow に数える
// UI World Follow の数ではなく、Center でない UI の数だけ親をたどる（多くの UI は Center なので軽い）
template<class T> void CountNonCenterAnchors() {
	EntityManager::ForEach<T>([](Handle<Entity> entity, const T& component) {
		if (component.anchor == ScreenAnchor::Center) {
			return;
		}
		if (UIWorldFollowComponent* follow = FindFollower(entity)) {
			++follow->nonCenterAnchorCount;
		}
	});
}
#endif

//=============================================================================
// 見せ方（Inspector・保存）
//=============================================================================
// ===== 今の状態の文字（Inspector の灰色の説明）=====
std::string MakeStatusText(const UIWorldFollowComponent& follow) {
	if (!follow.enabled) {
		return "今：重ねていない";
	}
	const char* state = follow.isHidden ? "隠している" : (follow.isClamped ? "端に寄せた" : (follow.isOnScreen ? "画面の中" : "画面の外"));
	return std::format("今：{}  置いた所 ({:.0f}, {:.0f})  向き {:.0f} 度", state, follow.screenPosition.x, follow.screenPosition.y, follow.edgeAngle);
}

void DescribeUIWorldFollow(ComponentUI& ui, UIWorldFollowComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("false の間は重ねない（Transform のとおりに置く）"));
	ui.Field("Target###target", value.target, Tip("重ねる相手（none なら World Offset をワールドの点として使う）"));
	ui.Field("World Offset###worldOffset", value.worldOffset, Drag(0.01f).Tip("相手の位置からのずれ（ワールドの向き。m）。相手が none なら、重ねるワールドの点"));
	ui.Field("Offscreen###offscreen", value.offscreen, Tip("重ねる点が画面の外・カメラの後ろのときの扱い"));
	ui.BeginDisabled(value.offscreen != OffscreenMode::ClampToEdge);
	ui.Field("Edge Margin###edgeMargin", value.edgeMargin, AtLeast(0.0f).Drag(1.0f).Tip("Clamp To Edge で、画面の端から内側へ空ける幅（ピクセル。マークが切れない大きさにする）"));
	ui.EndDisabled();
	// --- 結果と警告（飾り。保存しない）---
	ui.Label(MakeStatusText(value).c_str());
	ui.Label("位置は写した点で決まる。Transform の Translation の x・y は、そこからのずれ（ピクセル）");
	if (value.nonCenterAnchorCount > 0) {
		ui.Label(std::format("注意：この Entity と子の UI のうち {} 個の Anchor が Center ではありません（Anchor の分だけずれます）", value.nonCenterAnchorCount).c_str());
	}
}
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
void UIWorldFollowSystem::Initialize() {
	// 置き場所・保存・Inspector を登録する。見せ方は DescribeUIWorldFollow 1つで、保存と Inspector の両方に使う
	EntityManager::RegisterComponent<UIWorldFollowComponent>();
	ComponentSerializer::Register<UIWorldFollowComponent>("UI World Follow", &DescribeUIWorldFollow);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<UIWorldFollowComponent>>("UI World Follow", "UI", &DescribeUIWorldFollow));
	// ワールド行列を作るたびに、置いた所を入れてもらう
	EntityManager::AddTransformAdjuster(&ApplyPlacement);
}

void UIWorldFollowSystem::Release() { placements.clear(); }


//=============================================================================
// 更新
//=============================================================================
void UIWorldFollowSystem::Update(const Camera* camera) {
	const bool hadPlacements = !placements.empty();
	placements.clear();

	// --- 1. 重ねる点を写して、置く所を決める ---
	EntityManager::ForEach<UIWorldFollowComponent>([camera](Handle<Entity> entity, UIWorldFollowComponent& follow) {
		ResetResults(follow);
		if (camera == nullptr || !follow.enabled || !EntityManager::IsActiveInHierarchy(entity)) {
			return; // 重ねない（Transform のとおり）
		}
		// 重ねるワールドの点（相手が居なくなった・無効なら隠す。消えた相手の所に残らないように）
		Vector3 world = follow.worldOffset;
		if (follow.target.IsSet()) {
			const Handle<Entity> target = EntityManager::Find(follow.target);
			const TransformComponent* targetTransform = EntityManager::Get<TransformComponent>(target);
			if (targetTransform == nullptr || !EntityManager::IsActiveInHierarchy(target)) {
				follow.isHidden = true;
				return;
			}
			world = GetWorldPosition(*targetTransform) + follow.worldOffset;
		}
		if (Place(*camera, world, follow)) {
			placements[entity.index] = {entity.generation, follow.screenPosition};
		}
	});

#ifdef USE_IMGUI
	// --- 2. Anchor の警告（エディタだけ）---
	CountNonCenterAnchors<ImageComponent>();
	CountNonCenterAnchors<TextComponent>();
	CountNonCenterAnchors<VectorShapeComponent>();
	CountNonCenterAnchors<PolylineComponent>();
#endif

	// --- 3. ワールド行列を計算し直す（置く物の子も、置いた所について来る）---
	// 前のフレームに置いていた物があれば、置かなくなった物を Transform のとおりに戻すためにも計算し直す
	if (hadPlacements || !placements.empty()) {
		EntityManager::UpdateTransforms();
	}
}

bool UIWorldFollowSystem::IsHiddenInHierarchy(Handle<Entity> entity) {
	for (Handle<Entity> current = entity; current.IsValid();) {
		if (const UIWorldFollowComponent* follow = EntityManager::Get<UIWorldFollowComponent>(current); follow != nullptr && follow->enabled && follow->isHidden) {
			return true;
		}
		const Entity* owner = EntityManager::Get(current);
		current = (owner != nullptr) ? owner->parent : Handle<Entity>{};
	}
	return false;
}
```

- `Place` の `switch` は `OffscreenMode` の3つ全部を書く（enum の分岐は switch）
- `ResetResults` は毎フレームの頭に結果を消す（Enabled を外した物・相手が居なくなった物に、前の結果が残らないように）
- `hadPlacements`：前のフレームに置いていた物が今は置かない（Enabled を外した・隠した）とき、ワールド行列を Transform のとおりに戻すためにも計算し直す
- Inspector の `ui.Label` は保存には関係しない（飾り）。今の状態と注意を出すだけ

### B-4 `HTTEngine.vcxproj`・`HTTEngine.vcxproj.filters`

ソリューションエクスプローラーで足してもよい。手で書くなら：

`HTTEngine.vcxproj`

```xml
    <ClCompile Include="HTTEngine\Systems\UI\UIWorldFollowSystem.cpp" />
```

（`HTTEngine\Systems\UI\VectorShapeSystem.cpp` の前）

```xml
    <ClInclude Include="HTTEngine\Components\UI\UIWorldFollowComponent.h" />
```

（`HTTEngine\Components\UI\VectorShapeComponent.h` の前）

```xml
    <ClInclude Include="HTTEngine\Systems\UI\UIWorldFollowSystem.h" />
```

（`HTTEngine\Systems\UI\VectorShapeSystem.h` の前）

`HTTEngine.vcxproj.filters`（それぞれ同じ所の前）

```xml
    <ClCompile Include="HTTEngine\Systems\UI\UIWorldFollowSystem.cpp">
      <Filter>HTTEngine\Systems\UI</Filter>
    </ClCompile>
```

```xml
    <ClInclude Include="HTTEngine\Components\UI\UIWorldFollowComponent.h">
      <Filter>HTTEngine\Components\UI</Filter>
    </ClInclude>
```

```xml
    <ClInclude Include="HTTEngine\Systems\UI\UIWorldFollowSystem.h">
      <Filter>HTTEngine\Systems\UI</Filter>
    </ClInclude>
```

## パートC 組み込む

### C-1 `Systems/EngineSystems.cpp`

include（UI の所の最後）

```cpp
#include "HTTEngine/Systems/UI/VectorShapeSystem.h"
#include "HTTEngine/Systems/UI/UIWorldFollowSystem.h"
```

`Initialize`（`UIMaskSystem::Initialize();` の次）

```cpp
	UIMaskSystem::Initialize();           // UI Mask
	UIWorldFollowSystem::Initialize();    // UI World Follow（UI をワールドの物・点に重ねる）
```

`Release`（`UIMaskSystem::Release();` の次）

```cpp
	UIMaskSystem::Release();
	UIWorldFollowSystem::Release();
```

`Update`（`CameraSystem::Update(isGameRunning);` の次）

```cpp
	CameraSystem::Update(isGameRunning);                         // Camera Component の位置・向きをゲームのカメラへ写す（ワールド行列の後、使う人たちの前）
	UIWorldFollowSystem::Update(gameCamera.get());               // UI をワールドの物・点に重ねる（カメラをこのフレームの位置にした後。置いたら、ワールド行列を計算し直す）
```

### C-2 `Systems/Rendering2D/SpriteRenderSystem.cpp`

include（`UIMaskSystem.h` の次）

```cpp
#include "HTTEngine/Systems/UI/UIMaskSystem.h"
#include "HTTEngine/Systems/UI/UIWorldFollowSystem.h"
```

`IsMouseOver`（最初の `if` の後）

```cpp
	if (image == nullptr || transform == nullptr || !image->enabled || !EntityManager::IsActiveInHierarchy(entity) || !InputManager::IsMouseInGame()) {
		return false;
	}
	// UI World Follow が隠している物（描いていない）には当たらない
	if (UIWorldFollowSystem::IsHiddenInHierarchy(entity)) {
		return false;
	}
```

`CalcGroupAlpha`（コメントを1行足し、親をたどる中で UI World Follow を見る）

```cpp
float SpriteRenderSystem::CalcGroupAlpha(Handle<Entity> entity) {
	// 自分 → 親 → 親の親 … と一番上までたどって、有効な Alpha Group の Alpha を掛けていく（親子は輪にならない。SetParent が断る）
	// UI World Follow が隠している（画面の外など）なら 0（Image・Text・Vector Shape・Polyline は 0 なら描かない）
	float alpha = 1.0f;
	for (Handle<Entity> current = entity; current.IsValid();) {
		if (const AlphaGroupComponent* group = EntityManager::Get<AlphaGroupComponent>(current); group != nullptr && group->enabled) {
			alpha *= std::clamp(group->alpha, 0.0f, 1.0f);
		}
		if (const UIWorldFollowComponent* follow = EntityManager::Get<UIWorldFollowComponent>(current); follow != nullptr && follow->enabled && follow->isHidden) {
			return 0.0f;
		}
		const Entity* owner = EntityManager::Get(current);
		current = (owner != nullptr) ? owner->parent : Handle<Entity>{};
	}
	return alpha;
}
```

- Alpha Group の Alpha が 0 の物にはマウスが当たる（今までどおり。フェードの途中のボタンを押せる）。UI World Follow が隠した物だけ、`IsMouseOver` で別に断る

### C-3 `Systems/UI/VectorShapeSystem.cpp`

include（`UIMaskSystem.h` の次）

```cpp
#include "HTTEngine/Systems/UI/UIMaskSystem.h"
#include "HTTEngine/Systems/UI/UIWorldFollowSystem.h"
```

`IsMouseOver`（最初の `if` の後）

```cpp
	if (shape == nullptr || transform == nullptr || !shape->enabled || !EntityManager::IsActiveInHierarchy(entity) || !InputManager::IsMouseInGame()) {
		return false;
	}
	// UI World Follow が隠している物（描いていない）には当たらない
	if (UIWorldFollowSystem::IsHiddenInHierarchy(entity)) {
		return false;
	}
```

## パートD Sandbox で確かめる

### D-1 `Sandbox/Source/Scripts/WorldFollowSamples.h`（新しい）

```cpp
#pragma once
#include <random>

#include "HTTEngine/ECS/Script.h"

/// <summary>
/// 円を描いて動く（Sandbox のテスト用。UI World Follow の相手・Camera Follow の相手にする）
/// </summary>
class CircleMover : public Script {
public:
	void Start() override;
	void Update() override;
	void Fields(ComponentUI& ui) override;

private:
	float radius_ = 3.0f;               // 円の半径（m）
	float degreesPerSecond_ = 90.0f;    // 1秒に回る角度（度）
	Vector3 center_ = {0.0f, 0.0f, 0.0f}; // 円の真ん中（実行中だけ。Start の位置）
	float angle_ = 0.0f;                // 今の角度（度。実行中だけ）
};

/// <summary>
/// UI World Follow の結果を読んで、端に寄せている間だけ矢印を出し、重ねる点の方へ向ける（Sandbox のテスト用）
/// <para>UI World Follow（Clamp To Edge）の Entity に付ける。Arrow に子の矢印（Vector Shape など）を入れる</para>
/// <para>結果は UI World Follow が Script の後（カメラの後）に書くので、Script が読むのは前のフレームの結果（向きが1フレーム遅れるが、見て分からない）</para>
/// </summary>
class EdgeArrow : public Script {
public:
	void Update() override;
	void Fields(ComponentUI& ui) override;

private:
	GameObject arrow_; // 端に寄せている間だけ出して回す物（子の矢印。上向きに作る）
};

/// <summary>
/// ダメージの数字のように、ワールドの点に出て、上へ浮いて消える（Sandbox のテスト用）
/// <para>UI World Follow（Target は none）の Entity に付ける。Interval 秒ごとに、Spawn Center のまわりの別の点に出し直す</para>
/// <para>浮くのは Transform の Translation の y（写した点からのずれ）。UI World Follow は Translation を書き換えないので、ぶつからない</para>
/// </summary>
class FloatUpText : public Script {
public:
	void Update() override;
	void Fields(ComponentUI& ui) override;

private:
	float interval_ = 1.0f;                     // 出し直すまでの秒
	float riseSpeed_ = 60.0f;                   // 上へ浮く速さ（ピクセル／秒）
	Vector3 spawnCenter_ = {0.0f, 1.0f, 0.0f};  // 出す点の真ん中（ワールドの位置）
	float spawnRadius_ = 2.0f;                  // 出す点を、真ん中からどれだけ散らすか（m）
	float timer_ = 0.0f;                        // 出してからの秒（実行中だけ）
	std::mt19937 random_{std::random_device{}()}; // 出す点を散らす乱数（実行中だけ）
};
```

### D-2 `Sandbox/Source/Scripts/WorldFollowSamples.cpp`（新しい）

```cpp
#include "Source/Scripts/WorldFollowSamples.h"

#include <cmath>

#include "HTTEngine/Components/UI/UIWorldFollowComponent.h"

SCRIPT(CircleMover, "Test");
SCRIPT(EdgeArrow, "Test");
SCRIPT(FloatUpText, "Test");

//=============================================================================
// CircleMover
//=============================================================================
void CircleMover::Start() { center_ = transform.translation; }

void CircleMover::Update() {
	angle_ += degreesPerSecond_ * Time::GetDeltaTime();
	const float radians = angle_ * kDegToRad;
	transform.translation = center_ + Vector3{std::cos(radians) * radius_, std::sin(radians) * radius_, 0.0f};
}

void CircleMover::Fields(ComponentUI& ui) {
	ui.Field("Radius###radius", radius_, AtLeast(0.0f).Tip("円の半径（m）"));
	ui.Field("Degrees Per Second###degreesPerSecond", degreesPerSecond_, Drag(1.0f).Tip("1秒に回る角度（度）"));
}


//=============================================================================
// EdgeArrow
//=============================================================================
void EdgeArrow::Update() {
	const UIWorldFollowComponent* follow = GetComponent<UIWorldFollowComponent>();
	if (follow == nullptr || !arrow_) {
		return;
	}
	// 端に寄せている間だけ出す。矢印は上向き（90 度）に作ってあるので、向きから 90 度引いて回す
	arrow_.SetActive(follow->isClamped);
	SetEulerAngles(arrow_.transform, {0.0f, 0.0f, follow->edgeAngle - 90.0f});
}

void EdgeArrow::Fields(ComponentUI& ui) { ui.Field("Arrow###arrow", arrow_, Tip("端に寄せている間だけ出して回す物（子の矢印。上向きに作る）")); }


//=============================================================================
// FloatUpText
//=============================================================================
void FloatUpText::Update() {
	timer_ += Time::GetDeltaTime();
	transform.translation.y += riseSpeed_ * Time::GetDeltaTime(); // 写した点からのずれ（ピクセル）を増やす＝上へ浮く
	if (timer_ < interval_) {
		return;
	}
	// --- 出し直す：別の点に出して、ずれを 0 に戻す ---
	timer_ = 0.0f;
	transform.translation.y = 0.0f;
	std::uniform_real_distribution<float> spread(-spawnRadius_, spawnRadius_);
	if (UIWorldFollowComponent* follow = GetComponent<UIWorldFollowComponent>()) {
		follow->worldOffset = spawnCenter_ + Vector3{spread(random_), 0.0f, spread(random_)}; // Target が none なので、ワールドの点
	}
}

void FloatUpText::Fields(ComponentUI& ui) {
	ui.Field("Interval###interval", interval_, AtLeast(0.01f).Tip("出し直すまでの秒"));
	ui.Field("Rise Speed###riseSpeed", riseSpeed_, Drag(1.0f).Tip("上へ浮く速さ（ピクセル／秒）"));
	ui.Field("Spawn Center###spawnCenter", spawnCenter_, Tip("出す点の真ん中（ワールドの位置）"));
	ui.Field("Spawn Radius###spawnRadius", spawnRadius_, AtLeast(0.0f).Tip("出す点を、真ん中からどれだけ散らすか（m）"));
}
```

### D-3 `Sandbox/Sandbox.vcxproj`・`Sandbox.vcxproj.filters`

`Sandbox.vcxproj`（`Source\Scripts\TestBullet.cpp`・`TestBullet.h` の前）

```xml
    <ClCompile Include="Source\Scripts\WorldFollowSamples.cpp" />
```

```xml
    <ClInclude Include="Source\Scripts\WorldFollowSamples.h" />
```

`Sandbox.vcxproj.filters`（`TestBullet.cpp`・`TestBullet.h` の前）

```xml
    <ClCompile Include="Source\Scripts\WorldFollowSamples.cpp">
      <Filter>Source\Scripts</Filter>
    </ClCompile>
```

```xml
    <ClInclude Include="Source\Scripts\WorldFollowSamples.h">
      <Filter>Source\Scripts</Filter>
    </ClInclude>
```

### D-4 `Sandbox/Resources/scenes/UIWorldFollowTest.scene.json`（新しい）

- Main Camera：Camera Follow で Mover を追う（Center・遊び 0・Smooth Time 0：カメラが的と一緒に毎フレーム動く。Script で置くと一番揺れる形）
- Mover：円を描いて動く赤い箱（CircleMover）
- Behind Box：カメラの後ろ・右にある青い箱
- Lock Mark（＋子の Lock Mark Inner）：Mover の 1m 上に重ねる二重の輪（Hide）
- Edge Marker（＋子の Edge Arrow）：Behind Box に重ねる青い丸（Clamp To Edge）。EdgeArrow が矢印を回す
- Float Number：ワールドの点に出て上へ浮く「123」（Keep。FloatUpText）

```json
{
	"version": 1,
	"entities": [
		{
			"id": 1,
			"name": "Main Camera",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 5.0, -12.0],
					"rotation": [5.0, 0.0, 0.0],
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
					"target": 3,
					"mode": "Center",
					"offset": [0.0, 0.0],
					"deadZone": [0.0, 0.0],
					"smoothTime": 0.0,
					"maxLag": [100.0, 100.0],
					"lookAheadMax": [0.0, 0.0]
				}
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
			"name": "Mover",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 4.0, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [0.9, 0.4, 0.3, 1.0]
				},
				"CircleMover": {
					"radius": 3.0,
					"degreesPerSecond": 90.0
				}
			}
		},
		{
			"id": 4,
			"name": "Behind Box",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [6.0, 1.0, -24.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [0.3, 0.5, 0.9, 1.0]
				}
			}
		},
		{
			"id": 5,
			"name": "Ground",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, -0.5, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [40.0, 1.0, 40.0]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [0.6, 0.6, 0.6, 1.0]
				}
			}
		},
		{
			"id": 10,
			"name": "Lock Mark",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 0.0, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"UI World Follow": {
					"enabled": true,
					"target": 3,
					"worldOffset": [0.0, 1.0, 0.0],
					"offscreen": "Hide",
					"edgeMargin": 48.0
				},
				"Vector Shape": {
					"enabled": true,
					"shape": "Ring",
					"size": [80.0, 80.0],
					"ringThickness": 6.0,
					"color": [1.0, 0.85, 0.2, 1.0]
				}
			}
		},
		{
			"id": 11,
			"name": "Lock Mark Inner",
			"parent": 10,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 0.0, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Vector Shape": {
					"enabled": true,
					"shape": "Ring",
					"size": [48.0, 48.0],
					"ringThickness": 4.0,
					"color": [1.0, 0.4, 0.2, 1.0]
				}
			}
		},
		{
			"id": 20,
			"name": "Edge Marker",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 0.0, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"UI World Follow": {
					"enabled": true,
					"target": 4,
					"worldOffset": [0.0, 0.0, 0.0],
					"offscreen": "ClampToEdge",
					"edgeMargin": 48.0
				},
				"Vector Shape": {
					"enabled": true,
					"shape": "Circle",
					"size": [28.0, 28.0],
					"color": [0.3, 0.6, 1.0, 1.0]
				},
				"EdgeArrow": {
					"arrow": 21
				}
			}
		},
		{
			"id": 21,
			"name": "Edge Arrow",
			"parent": 20,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 0.0, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Vector Shape": {
					"enabled": true,
					"shape": "Rectangle",
					"size": [8.0, 40.0],
					"pivot": [0.5, 0.0],
					"color": [0.3, 0.6, 1.0, 1.0]
				}
			}
		},
		{
			"id": 30,
			"name": "Float Number",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 0.0, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"UI World Follow": {
					"enabled": true,
					"target": 0,
					"worldOffset": [0.0, 1.0, 0.0],
					"offscreen": "Keep",
					"edgeMargin": 48.0
				},
				"Text": {
					"enabled": true,
					"text": "123",
					"fontSize": 36.0,
					"color": [1.0, 1.0, 1.0, 1.0]
				},
				"FloatUpText": {
					"interval": 1.0,
					"riseSpeed": 60.0,
					"spawnCenter": [0.0, 1.0, 0.0],
					"spawnRadius": 2.0
				}
			}
		}
	]
}
```

### D-5 確かめること

Sandbox を起動して、Project ウィンドウから UIWorldFollowTest を開き、Game タブを見る。

| # | すること | 見えること |
|---|---|---|
| 1 | 停止中に Game タブ | 赤い箱の少し上に黄色とオレンジの二重の輪。箱の所に「123」。画面の右下の端に青い丸（後ろの Behind Box の向き） |
| 2 | Hierarchy で Lock Mark を選ぶ | Inspector の UI World Follow に Target: Mover・World Offset (0, 1, 0)・Hide。下に `今：画面の中  置いた所 (…)  向き … 度` と説明。Edge Margin は灰色（Hide なので効かない） |
| 3 | Lock Mark の Transform の Translation の Y を 40 にする | 輪が 40 ピクセル上へずれる（写した点からのずれ）。Ctrl+Z で戻る |
| 4 | Play | 箱が円を描き、カメラが一緒に動く。**輪が箱からずれず、揺れない**。Edge Marker は画面の端を動き、青い矢印が Behind Box の方を指す。「123」が1秒ごとに別の所に出て、上へ浮く |
| 5 | 再生中に Lock Mark の Target を Behind Box にする | 輪が消える（後ろの物なので Hide で隠す） |
| 6 | Offscreen を Clamp To Edge にする | 輪が画面の端に出る |
| 7 | Target を none にして World Offset を (0, 0, 0) | 輪がワールドの原点の所に出る |
| 8 | Lock Mark Inner の Vector Shape の Anchor を TopLeft にする | Lock Mark の Inspector に `注意：この Entity と子の UI のうち 1 個の Anchor が Center ではありません` |
| 9 | Stop | 置いた所に戻る（Translation は書き換わっていない） |

## 2章 AL4_01 での使い方（ロックオンマーク・ダメージの数字）

エンジンのサブモジュールをこのコミットへ進めたら：

- **ロックオンマーク**：マークの親 Entity に UI World Follow（Hide か Clamp To Edge）、子に輪（Vector Shape）を段階の数だけ並べる。LockOn（Script）が相手を変えたら `follow->target = EntityManager::RefOf(相手)`・`follow->worldOffset = 胸の高さ`。ロックしていないときは親の Entity を無効にするか、UI World Follow の Enabled を外して Alpha Group で消す
- 段階で二重にする・色を変える・光る・縮むは、子の輪の Vector Shape と Tween・Timeline で（UI World Follow は Translation を書き換えないので、子でも親でもぶつからない）
- 胸の高さがボスの向きで回る必要があるときは、ボスの胸に空の子 Entity を置いて、それを Target にする（World Offset は 0）
- **ダメージの数字**（来週）：Text に UI World Follow（Target は none・Keep か Hide）。`OnHitDealt`・`OnHitTaken` で `follow->worldOffset = hit.point`、Translation の Y を Tween で上げて、Alpha Group で消す。数字が何個も出るなら Prefab にして `GameObject::Instantiate`

## 3章 次の候補

- **距離で大きさを変える**（遠い敵のマークを小さく）：今は Script で `distance` を見て Transform の scale を書く。たくさん使うようになったら、UI World Follow に「Scale By Distance」の値を足す
- **カメラを選ぶ**：今はゲームのメインのカメラだけ。画面を分ける遊び方（2人で遊ぶ）を作るときに、Camera の Entity を選べるようにする
- **端の矢印を回す選択肢**：今は Script が `edgeAngle` を読んで回す（1フレーム遅れ）。よく使うなら「Rotate To Edge」の選択肢を足して、エンジンが置くのと同じ所で回す
- **骨に重ねる**：今は Entity の位置だけ。アニメで動く胸に重ねたいときは、骨の名前を選べるようにする（AnimationSystem から骨のワールド位置を読む）
- **Anchor を自動で Center として扱う**：決まりではなく、UI World Follow の下では描く係が Anchor を使わないようにする（1.4 の手間を払う価値が出たら）
