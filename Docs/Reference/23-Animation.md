> HTTEngine リファレンスの1章。ほかの章は [索引](../Reference.md) から探す。

# 23. アニメーション（Animator）

**Add Component → Animation → Animator**（`AnimatorComponent`）。同じ Entity の **Model Renderer のモデル**を、ファイルに入っているアニメーションで動かす（Model Renderer が無ければ何もしない）。骨のあるモデル（スキン）も、骨の無いノードのアニメも同じ。

| 項目 | 中身 |
|---|---|
| Enabled | オフなら動かさない（モデルに入っている形で描く） |
| Animation | アニメーションが入っているファイル（`resources` の下の `.gltf` / `.glb` / `.fbx`）。モデルと同じファイルでよい |
| Clip | 流すアニメーションの**名前**（Blender ならアクションの名前。FBX の `Armature|Idle` の形も入る。127 バイトまで）。ファイルの中の名前から選ぶ。空なら流さない |
| Speed | 1 で等速、2 で倍速、負で逆再生 |
| Loop | ループするか |
| Play On Start | Play を押したとき（シーンが始まったとき）に流す。オフならゲームから `AnimationSystem::Play`・`Restart` |
| Preview Time | 停止中に見せる時刻（秒）。つまむとポーズが変わる（停止中も Scene・Game ビューに出る） |

- **アニメは番号ではなく名前で持つ**ので、ファイルの中でアニメを足す・消す・並べ替えても、同じアニメが流れる。名前がファイルに無いときは、ログに警告が出る（同じファイル・同じ名前は1回だけ）。
- 骨は View → **Skeletons** で Scene ビューに出せる。
- スキンのあるメッシュは、描く前に Compute Shader でまとめて曲げる（Scene ビューと Game ビューは同じ曲げた頂点を描く）。

## ゲームから流す

```cpp
// Script の中で（GameComponent.h だけで使える）
AnimationSystem::Play(gameObject.handle, "Run", true);           // 名前で流す（毎フレーム呼んでよい）
AnimationSystem::Play(gameObject.handle, "Attack", false,
                      AnimationSystem::CalcSpeedToFit(gameObject.handle, "Attack", 0.5f)); // ちょうど 0.5 秒で流しきる速さ
if (!AnimationSystem::IsPlaying(gameObject.handle)) { … }        // ループしないアニメが最後まで行った
```

| 関数 | すること |
|---|---|
| `Play(entity, clip, loop, speed)` | Animator の Clip・Loop・Speed をこの値に書き換えて流す。**違う名前か、止まっていれば最初から**。同じ名前が流れていれば、速さと loop だけ変える（最初には戻らない） |
| `Restart(entity)` | 今のアニメを最初から流し直す |
| `Stop(entity)` | 止めて最初の時刻に戻す |
| `IsPlaying(entity)` | 流れているか（ループしないアニメが最後まで行ったら false） |
| `GetClipDuration(entity, clip)` | そのアニメの長さ（秒） |
| `CalcSpeedToFit(entity, clip, seconds)` | そのアニメを、ちょうど seconds 秒で流しきる速さ |
| `GetPlayer(entity)` | 細かい操作（時刻を決める・止める）用の `AnimationPlayer`。ポインタはそのフレームの中だけ使う |

- どれも、呼んだフレームの `AnimationSystem::Update`（Script・SYSTEM・攻撃表の AI の後）から効く。流し始めたフレームは時刻を進めないので、そのフレームはアニメの 0 秒の形が見える。
- アニメが動かしたノードの位置は、このフレームのワールド行列・描画に入る。

```cpp
// ✗ だめな例
AnimationSystem::Play(entity, "Attack", false); // ✗ 毎フレーム呼んでも最初には戻らないので「もう1回攻撃」にならない
                                                //   → 押した瞬間に Restart（か、違う名前を挟む）
if (InputManager::IsKeyTriggered(DIK_J)) {
	AnimationSystem::Play(entity, "Attack", false);
	AnimationSystem::Restart(entity); // ○ 同じアニメが流れていても最初から
}
```

- 時間に沿って値・イベントを動かすのは Timeline（[21章](21-CurveTimeline.md)。同じ Animation カテゴリの Timeline Component）。
