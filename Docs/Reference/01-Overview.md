> HTTEngine リファレンスの1章。ほかの章は [索引](../Reference.md) から探す。

# 1. 全体の形

```
main.cpp                  ゲームの入口。Engine::Initialize してループを回すだけ
  └ Engine               ウィンドウ・DirectX12・各Managerの初期化と、フレームの開始/終了
      └ WindowManager    ウィンドウごとの更新と描画。Editorのウィンドウもここ
          └ SceneManager 再生状態（Play / Pause / Stop）とシーンの作り直し
              └ IScene   シーン（ふつうはエンジンの DefaultScene。要るときだけゲームが書く。2章）
                  └ Entity + Component   置いてある物と、その中身のデータ
                       └ System          Componentを回して動かす処理
```

**エンジンは「データの置き場所」と「毎フレームの段取り」を持ち、ゲームは「データの中身」と「処理」を書く。**

## 1フレームの順番

全 System を呼ぶ順番は `Systems/EngineSystems.cpp` の1か所にまとまっている（細かい順番はそこを見る）。

| 順 | 何が起きるか | 書く場所 |
|---|---|---|
| 1 | `Engine::BeginFrame`（入力の更新、鳴り終わった音の片付け、時間の更新） | エンジン |
| 2 | `EngineSystems::FlushPending`（前フレームの予約を反映）：`EntityManager::FlushComponentChanges`（Component の追加・削除）→ `PrefabManager::Flush`（Prefab から作った Entity に Component・Script を付ける）→ `UIScreen::Flush` → エディタは `EditorHistory::Flush`（Undo・削除・貼り付けの予約を実行） | エンジン |
| 3 | `SceneManager::Update`：頭で作り直し（Play / Stop / Restart）・シーン切り替え（前のフレームの `LoadScene`）・保存 → `EntityManager::UpdateTransforms`（頭で作った・戻した Entity の `worldMatrix` も作る）→ **`IScene::Update`** → **Script の Awake・Start・当たり・Update** → **`GameComponentRegistry::UpdateAll`（`SYSTEM(...)` で書いた処理）** → **Script の LateUpdate** | **ゲーム** |
| 4 | `EngineSystems::Update` の頭：`AttackTableSystem` → `AnimationSystem` → `UIStateSystem` → `TimelineSystem`（Script・SYSTEM の後に、アニメ・Timeline の値を書く）→ `EntityManager::UpdateTransforms`（親→子の順に `worldMatrix` を計算） | エンジン |
| 5 | `WindZoneSystem::Update`（Wind Zone の範囲を集め直す。再生中は範囲の中の Rigidbody を押す・画面の風の線の値を書く）→ 再生中だけ `CharacterControllerSystem::Update` → `PhysicsSystem::Update` → `CameraSystem::UpdateFollow`（Camera Follow のカメラを、物理で動いた後の追う物へ近づける）。どれも、動かしたら `worldMatrix` を計算し直す | エンジン |
| 6 | `CameraSystem::Update`（有効な Camera Component の中で Priority が一番大きい物を選び、注視点・揺れ・切り替えの補間を入れた値をゲームのカメラへ写す）→ `UIWorldFollowSystem::Update`（UI をワールドの点に重ねる） | エンジン |
| 7 | パーティクル（`ParticleManager`・`ParticleSystem`）/ `LightSystem::Update`（ライトを集めてGPUへ）/ `FogSystem` / `DepthOfFieldSystem` | エンジン |
| 8 | `ScriptSystem::FlushDestroy`（Script の OnDestroy）→ `EntityManager::FlushDestroy`（`Destroy` の予約を反映。子も一緒に消える） | エンジン |
| 9 | `CollisionSystem::Update`（Collider の重なりを調べて「当たりの一覧」を作る。ゲームは次のフレームの 3 で `GetHits` を読む）→ `DamageSystem` → `HitSystem`（攻撃の当たり） | エンジン |
| 9.5 | `TriggerActivatorSystem::Update`（再生中だけ。9 の当たりで、範囲に入った・出たを見て Target を出す・消す・フェードを進める）→ `TimelineTriggerSystem::Update`（範囲に入った・出たで Timeline の再生を頼む） | エンジン |
| 9.6 | `UIScreen::Update`（再生中だけ。画面の出す・消す動きを進め、ゲームを止める・動かす。止めない時間で動く）→ `SceneTransition::Update`（シーンを切り替える幕を進める。閉じきったら `LoadScene` を頼む） | エンジン |
| 10 | `AudioSystem::Update`（種類ごとの音量を伝え、消えた・外した・無効の AudioSource の音を止め、初めて見つけた AudioSource を Play On Start で鳴らし、3Dの音の大きさ・左右を更新する。停止中は鳴らさない） | エンジン |
| 10.5 | `TrailSystem::Update`（持ち主の Entity・Trail が消えた尾を持ち主から離し〈Fade Out After Owner なら写した設定で消えきるまで描く。新しい点は足さない〉、点を古くして寿命の物を消し、Emitting の Trail に今の位置を足す。停止中は点を持たない。シーンを変えたら `TrailSystem::ClearAll` で全部捨てる）→ `ElasticSurfaceSystem::Update` | エンジン |
| 10.6 | `WindowManager::UpdateCursor`（OS のカーソルを隠す・閉じ込める。Script・SYSTEM の頼みがそろった後） | エンジン |
| 11 | `WindowManager::DrawAll` → **`IScene::Draw`**（`ModelRenderSystem::Draw` など）→ `EngineSystems::Draw`（パーティクル・軌跡の帯・球と箱・足場・デカール・Sprite・UI・幕・エディタのギズモ）。ここでは描画の要求を積むだけ | **ゲーム** |
| 12 | `Engine::EndFrame`：`PreRenderAll`（文字のアトラスを送る・スキンのあるメッシュを CS で曲げる・積んだ要求を描く・ポストエフェクト）→ `PostRenderAll`（ImGui・Present。GPUを待った後、返してもらったSRVスロットを使い回せるようにする） | エンジン |

**覚えておくこと：**

- **追加・削除は必ず「予約 → 次のフレームの頭で反映」**。`ForEach` で回している最中に配列が動かないようにするため
- `worldMatrix` は 3 の頭の最後と 4・5 で計算されるので、**Script・SYSTEM の中で読むと、フレームの頭の時点の値**（シーンを読んだ最初のフレームでも正しい。そのフレームの中で動かした分は 4 まで入らない）
- 当たり（`CollisionSystem::GetHits`）も 9 で作られるので、**SYSTEM で読むのは前のフレームの判定結果**（動かした後の位置で調べた物）。消すと決めた Entity は 8 で消えているので、もう当たらない
- 停止中（Stop）は 3 の `IScene::Update` から後（Script・SYSTEM）が飛ぶ（3 の頭の作り直し・保存は動く）。Pause中は Script が呼ばれず、SYSTEM は `Time::GetDeltaTime()` が 0 で回る。音も止まる

## ファイルの置き場所

**名前の終わりで、置き場所が決まる**（理由は [ARCHITECTURE.md](../ARCHITECTURE.md) の「フォルダの決まり」）。

| 名前 | 置き場所 | 例 |
|---|---|---|
| `〜Component` | `HTTEngine/Components/カテゴリ/`（カテゴリは Add Component のカテゴリと同じ名前） | `Core/TransformComponent.h`・`Physics/ColliderComponent.h`・`Audio/AudioSourceComponent.h`・`Camera/CameraComponent.h` |
| `〜System` | `HTTEngine/Systems/カテゴリ/`（呼ぶ順番は直下の `EngineSystems.cpp`） | `ModelRenderSystem`・`LightSystem`・`CollisionSystem`・`AudioSystem`・`CameraSystem`・`TrailSystem` |
| `〜Editor`（Inspector の区画） | `HTTEngine/Editor/Inspector/` | `TransformEditor`・`LightEditor` |
| `〜Gizmo` | `HTTEngine/Editor/Gizmos/` | `LightGizmo` |
| `〜Window`（エディタのウィンドウ） | `HTTEngine/Editor/Windows/` | `HierarchyWindow`・`InspectorWindow`・`ProjectWindow`・`SettingsWindow` |
| エディタが出すアセットの見た目 | `HTTEngine/Editor/Assets/` | `AssetIcons`（アイコン）・`AssetThumbnails`（画像の小さい絵） |
| エディタのアイコンの画像 | エンジンの `Resources/Editor/Icons/`（ゲームの `HTTEngine/Resources/Editor/Icons/` へ写される） | `Folder.png`・`Scene.png` |
| Entity と Component の仕組み | `HTTEngine/ECS/` | `EntityManager`・`ComponentUI`・`GameComponent`（ゲームが読むのはこれ1つ） |
| `〜Manager` | その分野のフォルダ | `ModelManager`（`Graphics/Model/`）・`SoundManager`（`Sound/`） |
| ゲームの Component（データ＋見せ方＋処理の1ファイル） | ゲームの `project/Source/Components/`（約束。どこに置いても動く） | `Spin.cpp` |

- ゲームの Component は、**プロジェクトに入っていればどこに置いても登録される**（`COMPONENT(...)` が起動のときに登録する）。Inspector の見出しにマウスを乗せると、書いたファイルが出る。
- モデル・テクスチャ・音は、**`resources/` か `HTTEngine/Resources/` の下**（深さは自由）に置くと Inspector の一覧に出る。一覧の一番上に探す場所が出る。
- エンジンのヘッダは写さない。`HTTEngine.props` の include の道でエンジンのフォルダを直接読む（`"ModelManager.h"` のようにファイル名だけでも書ける）。ゲームの `HTTEngine/` へ写されるのは、実行に要る `Shader/Data/` と `Resources/` だけ。

## 名前の決まり

| 決まり | 例 | 理由 |
|---|---|---|
| **Sound＝音の素材**（ファイル・波形）、**Audio＝鳴らす仕組み** | `SoundManager`・`AssetType::Sound`（素材）／`AudioSourceComponent`・`AudioSystem`・`AudioBus`（鳴らす仕組み） | Unreal も同じ分け方（SoundWave と AudioComponent）。混ざっていても、決まりがあれば迷わない |
| **Calc＝呼ぶたびにその場で計算**、**Get・メンバ＝覚えてある値** | `EntityManager::CalcWorldMatrix`（親をたどって計算）／`TransformComponent::worldMatrix`（フレームに1回計算した値） | Calc だと「重いかもしれない」と分かるので、毎フレーム何百回も呼ばない |
| マウスの位置は **`InGame`＝ゲーム用、`InWindow`＝エディタ・エンジン用** | `GetMousePositionInGame`／`GetMousePositionInWindow` | エディタではウィンドウの中とゲーム画面の中がずれる |
