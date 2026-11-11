> HTTEngine リファレンスの1章。ほかの章は [索引](../Reference.md) から探す。

# 10. カメラ

**カメラは Entity に付ける Camera Component。** Hierarchy の **＋ → Camera** で「Main Camera」ができる（Add Component → Camera → Camera でも付く。3D・2D どちらでも使うので Rendering3D の中ではない）。

| 項目 | 中身 |
|---|---|
| Transform | カメラの位置・向き（大きさは使わない）。**親の下に置くと一緒に動く** |
| Field Of View | 縦の視野角（**度**）。小さいほど望遠 |
| Near / Far | 映る距離の範囲 |
| Priority | 有効な Camera が複数あるとき、大きい方を使う。切り替えは、この値か Entity の有効・無効で |
| Background（Type・Color・Sky・Horizon・Ground・Horizon Width） | 物が描かれていない所の塗り方。`Skybox`（天球）／`SolidColor`（Color の1色）／`Gradient`（見る向きの上下で、真上の Sky・地平線の Horizon・真下の Ground を混ぜる。Horizon Width は地平線の色が残る幅で、0.15 なら上下に約 9°。その間をなめらかにぼかす）。SolidColor・Gradient は霧が掛からない。環境光（IBL）は天球のまま。Scene ビューは見ない（初めはエディタのグラデーション。View → Skybox で天球に切り替える） |
| Blend（Style・Time・Ease） | このカメラへ切り替わるときの動き方。`Cut`（すぐ切り替える）／`Ease`（Time の秒をかけて、位置・向き・視野角・Camera Volume の値を補間する） |
| Time Mode | このカメラの切り替え・Camera Follow・Camera Orbit・Camera Look At・Camera Shake が使う時間。`Game`（スローで遅くなり、ヒットストップ・ポーズで止まる）／`Real`（止まらない。エディタの一時停止では止まる） |

- Save される。Play 中に動いたカメラは Stop で戻る。停止中も Game ビューはそのカメラから撮る。
- **Transform はカメラの本来の位置・向き。** 注視点・揺れ・切り替えの補間は写す値にだけ入り、Transform は変わらない。画面に写っている位置・向きは `CameraSystem::GetMainCamera()` で読む。
- 補間の途中でさらに切り替えても、そのときに見えている値から始まるので絵は飛ばない。補間の途中かは `CameraSystem::IsBlending()` で分かる。`GetMainCameraEntity()` は補間の途中でも切り替わった先のカメラを返す。
- シーンを読み直したとき・Play を押したときは補間しない。
- Camera が1つも無いときは、既定のカメラ（(0, 0, -10) から +Z を向く）で撮る。
- シーンの `GetCamera()` は**上書きしない**（上書きしなければ、`CameraSystem` が選んだカメラが返る）。エンジンの描画・パーティクル・3Dの音は `GetCamera()` のカメラを使う。
- **カメラを動かす＝その Entity の Transform を動かす**（SYSTEM の中で）。

```cpp
void MoveCameraUp(float deltaTime) {
	const Handle<Entity> camera = CameraSystem::GetMainCameraEntity(); // Camera Component が無ければ無効な Handle
	if (TransformComponent* transform = EntityManager::Get<TransformComponent>(camera)) {
		transform->translation.y += 2.0f * deltaTime;
	}
}
```

## 追いかけるカメラ（Camera Follow）

**Add Component → Camera → Camera Follow**（`CameraFollowComponent`）。Camera と同じ Entity に付けて、Target に追う Entity を Hierarchy からドラッグする。動かすのは Plane の面の上で、カメラの向きは置いたまま。再生中だけ動く（停止中は置いた位置のまま）。

| 項目 | 中身 |
|---|---|
| Target | 追う Entity |
| Plane | カメラを動かす面。`XY`（ワールドの X・Y。横から見るゲーム）／`XZ`（地面。見下ろしのゲーム。傾けたカメラでも、置いた通りの見え方のまま追う）／`View`（カメラの右・上。どの向きのカメラでも。奥行きも追いかけ始めの長さに保つ） |
| Mode | 追い方。`Center`（追う物を真ん中に）／`LookAhead`（追う物の速さ × Look Ahead Time だけ先を真ん中に。止まると戻る）。選んだ追い方の値だけが出る |
| Look Ahead Time・Max・Smooth Time（LookAhead のとき） | 何秒先を見せるか（X 0.4・Y 0。XY で Y を入れると跳ぶたびに上下に揺れる）・先読みの上限（m）・先読みが変わるまでの時間（指数補間） |
| Offset | 追う物から、画面の真ん中をどれだけずらすか（m）。XY で Y を上げると足元が広く見える |
| Zone Space | Dead Zone・Soft Zone を何で測るか。`World`（Plane の面の上の長さ m。カメラが寄っても引いても、追い始める距離は同じ）／`Screen`（画面の割合。真ん中 0・端 1。Camera Look At の Screen Position と同じ測り方。カメラが引くとワールドでは広がる）。選んだ方の値だけが出る |
| Dead Zone | 遊び。focus（追う物 ＋ Offset ＋ 先読み）が、画面の真ん中から横・縦にこの範囲で動いても、カメラは動かない |
| Soft Zone | focus を出さない範囲。Dead Zone の外ではなめらかに（Smooth Time）追い、ここに着いたらすぐ付いていく（速く動いても画面から出ない。前の Max Lag）。Dead Zone より小さい軸は Dead Zone と同じ |
| Ground Snap Y | Plane が XY のとき。追う物（Rigidbody か Character Controller）が地面にいる間は、縦の遊びを使わずに合わせる。跳んでも画面は揺れず、段に乗ると上がる |
| Smooth Time | focus が Dead Zone の外へ出たとき、追いつくまでの時間の目安（秒）。走っている間は「速さ × Smooth Time」くらい遅れる |
| Show Gizmo | 線を出す（エディタだけ）。緑：Dead Zone／青：Soft Zone／黄：focus の十字と、追う物からの線／赤：Bounds。World の範囲・focus・Bounds は Scene・Game ビュー、Screen の範囲は Game ビューだけ（今写しているカメラの物だけ）。View → Gizmos → Camera Follow Zones で全部まとめて消せる |
| Bounds（Use Bounds・Min・Max） | 画面の端が Min〜Max の外を写さない（ステージの端で止まる）。XY はワールドの X・Y で追う物の奥行きで測る。XZ はワールドの X・Z で追う物の高さで測る（カメラが回っても範囲は回らない）。View には無い |

- 横・縦の向きは、XY はワールドの X・Y。XZ はカメラの右とカメラの前を地面に寝かせた向き、View はカメラの右・上で、カメラの Transform を回すと毎フレーム付いてくる（追う物のまわりを回る動きになる）。
- Camera Follow が読む向きは Transform の向きだけ。Camera Look At の向きは読まない（Screen の範囲も、Transform の向きと Camera の Fov Y で測る）。
- 範囲の線は focus を通る面の上に描くので、Game ビューでは画面の上の範囲にぴったり重なる（Screen なら画面の四角、XZ の World なら地面の台形）。
- 「画面の真ん中」は、カメラの前の向きが追う物を通る面（XY：追う物の奥行き、XZ：追う物の高さ）と交わる点。傾けたカメラでも、追い始め・Ground Snap Y で focus は画面の真ん中に来る。Bounds の画面の端も、画面の四隅をその面に当てて測る（下へ傾けると、下の方が遠くまで写る）。
- 動かすのは物理の後・`CameraSystem::Update` の前（[1フレームの順番](01-Overview.md#1フレームの順番) の 5）なので、追う物と1フレームずれない。
- 自分で動かしたいカメラ（演出など）は、Camera Follow を外す（Enabled）か、付けずに SYSTEM・Script で Transform を動かす（上の例）。
- 追う物のまわりを回るカメラ・相手を見ながら後ろに付くカメラは、Camera Follow ではなく Camera Orbit（次の節）。Camera Follow はカメラの向きを変えない。

## 追う物のまわりを回るカメラ（Camera Orbit）

**Add Component → Camera → Camera Orbit**（`CameraOrbitComponent`）。Camera と同じ Entity に付けて、Target に追う Entity を Hierarchy からドラッグする。カメラを「回る中心（追う物 ＋ Pivot Offset）から、横の向きの後ろへ Distance、Pitch だけ上」に置き、回る中心を向かせる（Transform の位置と向きを書く）。再生中だけ動く（停止中は置いた位置のまま。Scene ビューの線が、再生したら置かれる位置を見せる）。

| 項目 | 中身 |
|---|---|
| Target | 追う Entity |
| Pivot Offset | 追う物の位置から回る中心へのずれ（m。ワールドの向き）。足元が原点のキャラなら Y を胸・頭の高さに |
| Distance | 回る中心からカメラまでの長さ（m） |
| Pitch | 見下ろす角度（度。-80〜80）。＋ で上から見下ろす |
| Heading | カメラが追う物のどちら側に付くか。`World`（ワールドの決まった向き。Yaw 0 で +Z を向く）／`TargetForward`（追う物の前の向き＝追う物の後ろに付く）／`LookAt`（同じ Entity の Camera Look At が見る点の向き＝見る点と反対の側に付く。**ロックオン**） |
| Yaw | 横の角度（度。＋ で上から見て時計回り）。World は向きそのもの、ほかは決まった向きに足す角度（20 くらいずらすと肩越し） |
| Heading Smooth Time | 横の向きが追いつくまでの時間の目安（秒）。一番近い回り方で回る |
| Smooth Time | 回る中心が追う物に追いつくまでの時間の目安（秒） |
| Show Gizmo | Scene ビューに線を出す（エディタだけ）。黄：回る中心の十字と追う物からの線／紫：回る輪と、回る中心からカメラを置く位置への線／赤：LookAt の見る点から回る中心への線。View → Gizmos → Camera Orbits で全部まとめて消せる |

- **相手を見ながら追う物の後ろに付く（ロックオン）**：Camera Orbit（Target＝プレイヤー・Heading＝LookAt）＋ Camera Look At（Target＝ボス）。上から見て、ボス・プレイヤー・カメラが一直線に並び、写す向きは Camera Look At がボスへ向ける（Screen Position を (0, 0.15) くらいにすると、ボスが少し上・プレイヤーが下に写る）。
- ロックオンを入れる・切るは、ロックオンのカメラを別に置いて Priority か Entity の有効・無効で切り替える（Camera の Blend で補間する）。相手を変えるのは Camera Look At の Target を書き換えるだけ（Camera Orbit は同じ Entity の Camera Look At を読むので、ボスを2か所に入れない）。
- 横の向きが決まらないとき（LookAt で見る点が回る中心の真上・真下・同じ所、Camera Look At が無い・切ってある、TargetForward で追う物が真上・真下を向いている）は、そのときの向きを保つ。回り始めに決まらなければ、置いたカメラから回る中心への向きから始める。
- LookAt の向きは Camera Look At の見たい点（Smooth Time をかける前の点）で決める。回り込みの遅れは Heading Smooth Time だけで決まる。
- 同じ Entity に Camera Follow も付いていて、どちらも有効なら、**Camera Orbit だけが動かす**（Camera Follow の範囲の線も出ない）。Enabled を切った・Entity を無効にした Camera Orbit は、また動かすときに決まった位置からすぐ始める。
- 「カメラが追っている物」（`CameraSystem::GetFollowTargetEntity`・Camera Volume と Depth Of Field の FollowTarget・Trigger Activator の FollowTarget・Wind Zone の線）は、有効な Camera Orbit があればその Target。
- 親の下に置いたカメラでも、ワールドでその位置・向きになる（`EntityManager::SetWorldRotation`）。
- 壁に当たって寄る（Cinemachine の Deoccluder）・スティックで回す専用の項目は無い。回すなら Heading を World にして、Script で `yaw` を書き換える。

```cpp
// ロックオンの相手を変える（lockOnCamera は Camera Orbit の Heading が LookAt のカメラ）
if (CameraLookAtComponent* lookAt = EntityManager::Get<CameraLookAtComponent>(lockOnCamera)) {
	lookAt->target = EntityManager::RefOf(nextEnemy); // カメラは Heading Smooth Time で新しい相手の反対側へ回り込む
}
```

## 注視点（Camera Look At）

**Add Component → Camera → Camera Look At**（`CameraLookAtComponent`）。カメラが Target を見続ける。変えるのは写す向きだけで、Transform の向きは変えない。停止中も Game ビューに効く。

| 項目 | 中身 |
|---|---|
| Target | 見る Entity。空なら Offset をワールドの座標として見る |
| Offset | 見る点のずれ（m。ワールドの向き） |
| Screen Position | 見る点を写す画面の位置（真ん中が 0、右と上が＋、端が ±1。スプライトの position と同じ）。横 1/3 なら X を -0.333（左から）か 0.333（右から）。カメラは横に傾けない |
| Smooth Time | 見る点が追いつくまでの時間の目安（秒。再生中だけ効く） |

カメラの子の Entity は Transform に付いていくので、注視点の向きには回らない。Screen Position の向きの計算は `../Tasks/TriggerSaturationScreenPosition.md` の 1.3。

## カメラの揺れ（Camera Shake）

**Add Component → Camera → Camera Shake**（`CameraShakeComponent`）。揺れは写す値にだけ足すので、揺れが終わると必ず元の位置に戻る。再生中だけ揺れる。

| 項目 | 中身 |
|---|---|
| Noise：Position・Rotation | ずっと続く揺れの幅。位置は m（カメラの右・上・前）、向きは度（上下の首振り・左右の首振り・傾き） |
| Noise：Frequency・Gain | 1秒に揺れる回数の目安・揺れ幅に掛ける倍率 |
| Impulse：Gain | 受けた強さに掛ける倍率（0 なら受けない） |
| Impulse：Frequency・Damping | 1秒に揺れる回数・収まる速さ（減衰比。0 に近いほど長く揺れる） |
| Impulse：Rotation | 1m ずれたときに一緒に回す角度（度）。遠くの景色も揺れて見える |
| Test Impulse | 再生中に押すと 0.3m の衝撃を送る |

```cpp
CameraSystem::AddImpulse(0.4f);                     // 強さは最初に振れる幅（m）。向きは画面の面の中でランダム
CameraSystem::AddImpulse(0.4f, {1.0f, 0.0f, 0.0f}); // ワールドの +X へ。長さは1でなくてよい
```

有効な Camera Shake 全部が受ける（切り替えの途中でも、両方のカメラが揺れる）。

## カメラごとのポストエフェクト（Camera Volume）

**Add Component → Camera → Camera Volume**（`CameraVolumeComponent`）。チェックを入れた項目だけ、このカメラの値を使う。外した項目は全体の設定（Settings → PostEffect・Depth Of Field Component）のまま。カメラの切り替えと一緒に補間する。

| 項目 | 中身 |
|---|---|
| Weight | 全部の項目の効き具合（0：全体の設定のまま 〜 1：このカメラの値） |
| Focus | ピントの合わせ方。`Global`（全体の設定）／`Distance`（Focus Distance）／`LookAtTarget`（Camera Look At が見ている点）／`FollowTarget`（このカメラの Camera Follow・Camera Orbit の Target。両方あれば有効な Camera Orbit の方） |
| Depth Of Field | Focus Range・Far Transition・Near Transition・Max Blur |
| Bloom | Threshold・Knee・Intensity |
| Lens | Distortion・Aberration・Radial Blur・Vignette |
| ToneMap | Exposure |
| Color Adjust | Saturation |

- Bloom の Mode・ToneMap の Mode は途中の値が無いので持たない（全体の設定のまま）。
- 全体の設定で切ってある効果も、Bloom の Intensity・Lens の項目・Max Blur を上書きしたカメラでは掛かる。
- Game ビューと製品版に掛かる。Scene ビューには掛からない（別のカメラから見るので、ピントが合わない）。
- 空間に置く Volume は無い。

## マウスで地面を指す（`ScreenPointToRay`）

画面の位置から、カメラの奥へ向かうレイを作る（Unity の `Camera.ScreenPointToRay`）。地面（y=0）や壁との交点を取るのに使う。透視でも正射影でも動く。

```cpp
#include <HTTEngine/Camera/Camera.h>
#include <HTTEngine/Collision/Collision.h>

const Ray ray = CameraSystem::GetMainCamera()->ScreenPointToRay(InputManager::GetMousePositionInGame()); // 始点は近クリップ面、向きは長さ1
float t = 0.0f;
if (Collision::RayPlane(ray, {{0.0f, 1.0f, 0.0f}, 0.0f}, &t)) {
	const Vector3 ground = Collision::RayPoint(ray, t); // マウスが指している地面の位置
}
```

- 引数は `GetMousePositionInGame()` の値をそのまま渡す（真ん中0、右と上が+、端が±1）。
- 物（Collider）を指したいときは、このレイを `CollisionSystem::Raycast` に渡す（14章）。
- 逆に、ワールドの点が画面のどこに写るかは `Camera::WorldToScreenPoint(world, &ndc)`（同じ ±1 の座標。カメラの後ろなら false）。
- `GetViewProjectionMatrix()` は値で返る。

```cpp
// ✗ だめな例
class GameScene : public IScene {
	std::shared_ptr<Camera> GetCamera() override { return camera_; } // ✗ 上書きすると Camera Component が無視される（前の書き方）
};
void GameScene::Update() {
	ParticleManager::SetCamera(GetCamera()); // ✗ 要らない（エンジンが毎フレーム GetCamera から取る）
}
```

Sceneビューのカメラ（デバッグカメラ）は別物で、ゲームのカメラには影響しない。Scene ビューには、Camera Component の位置にアイコンが出る（クリックで選べる。View → Camera Icons で消せる）。Game ビューには出ない。

## 線に沿って動くカメラ（Camera Path）

空の Entity の子に点の Entity を上から順に並べると線になる。カメラに **Camera Path** を付けて、Path へ親の Entity をドラッグする。

| 項目 | 中身 |
|---|---|
| Path | 線の Entity。子を順に通るなめらかな線（`PathCurve`）になる。子が 2 つ以上要る |
| Position | 線の上の位置（0〜1。長さに対する割合）。Timeline のキーで動かす |
| Rotation | `Keep`（Transform の向き。Camera Look At があれば注視点を見る）／`AlongPath`（線の進む向きを見る） |

写す位置だけを置き換えるので、Transform は変わらない。線は Scene ビューに水色で出る（View → Camera Paths）。

## Timeline でカメラを動かす

Timeline のカメラのトラック（`TimelineCameraTrack`）は、通常のカメラ（Priority で選ぶカメラ）の代わりに写すカメラを毎フレーム頼む。頼まなくなれば通常のカメラへ戻る。

- 演出に使うカメラは、通常のカメラより **Priority を小さくして**置く（無効にはしない）。
- カメラの指し方は2つ。再生する Entity の子孫なら `Rig Cam A`、シーンのほかの場所なら `/Cameras/Cam Wide`（`/` で始まる、シーンの一番上からのパス）。Hierarchy の Camera を Timeline ウィンドウの列へドラッグすると、その時間のクリップになる。
- クリップを並べるとカット、重ねると補間。重なった長さが補間の秒。
- 最初の補間はクリップの Blend In。終わった後は Exit で選ぶ（`Cut`：すぐ戻す／`Ease`：Exit Time の秒で戻す。この秒は Timeline の長さに入る／`Hold`：次のクリップか `TimelineSystem::Stop` までこのカメラ）。
- `TimelineSystem::Stop` で途中で止めても、クリップの Exit の設定で戻る。
- Timeline の Time Mode を Real にすると、ヒットストップの間も演出が進む。写しているカメラは Camera の Time Mode に関係なく Timeline の時間で動く。
- 位置・FOV・ポストエフェクト・ノイズの強さは値のトラック（`TimelineValueTrack`）で動かす（Component は Transform・Camera・Camera Volume・Camera Shake・Camera Path）。衝撃は `TimelineCameraShakeTrack`。
- 停止中に再生位置をつまむと、切り替え・補間・キー・揺れが Game ビューに出る。
- コードから頼むときは `CameraSystem::RequestOverride`。使い方は [Tasks/CameraTimeline.md](../Tasks/CameraTimeline.md)。
