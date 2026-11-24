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
| Blend Time | 別のアニメに切り替えるとき、前のアニメの形から混ぜていく秒（既定 0 ＝すぐ切り替える）。停止中（Editing）は混ぜない。ゲームから `Play`・`Restart` に秒を渡したときは、そちらを使う |
| Preview Time | 停止中に見せる時刻（秒）。つまむとポーズが変わる（停止中も Scene・Game ビューに出る） |

- **アニメは番号ではなく名前で持つ**ので、ファイルの中でアニメを足す・消す・並べ替えても、同じアニメが流れる。名前がファイルに無いときは、ログに警告が出る（同じファイル・同じ名前は1回だけ）。
- アニメはノードを**名前**で動かす。モデルの中に同じ名前のノードが2つ以上あるとき（Blender でメッシュのオブジェクトと骨に同じ名前を付けた、など）は、**骨の方**だけを動かし、読み込みのときにログに警告が出る。スキンのあるメッシュを持つノードは骨にしない。警告が出たら、モデルを作ったソフトで名前を分けておくと迷わない。
- 骨は View → **Skeletons** で Scene ビューに出せる。
- スキンのあるメッシュは、描く前に Compute Shader でまとめて曲げる（Scene ビューと Game ビューは同じ曲げた頂点を描く）。

## ゲームから流す

```cpp
// Script の中で（GameComponent.h だけで使える）
AnimationSystem::Play(gameObject.handle, "Run", true);           // 名前で流す（毎フレーム呼んでよい）
AnimationSystem::Play(gameObject.handle, "Attack", false,
                      AnimationSystem::CalcSpeedToFit(gameObject.handle, "Attack", 0.5f)); // ちょうど 0.5 秒で流しきる速さ
AnimationSystem::Play(gameObject.handle, "Run", true, 1.0f, 0.2f); // 前のアニメの形から 0.2 秒で混ぜる
if (!AnimationSystem::IsPlaying(gameObject.handle)) { … }        // ループしないアニメが最後まで行った
```

| 関数 | すること |
|---|---|
| `Play(entity, clip, loop, speed, blendTime)` | Animator の Clip・Loop・Speed をこの値に書き換えて流す。**違う名前か、止まっていれば最初から**（前のアニメの形から `blendTime` 秒で混ぜる。省くと Animator の Blend Time）。同じ名前が流れていれば、速さと loop だけ変える（最初には戻らない） |
| `Restart(entity, blendTime)` | 今のアニメを最初から流し直す（今見えている形から `blendTime` 秒で混ぜる。省くと Animator の Blend Time） |
| `Stop(entity)` | 止めて最初の時刻に戻す（混ぜるのもやめる） |
| `IsPlaying(entity)` | 流れているか（ループしないアニメが最後まで行ったら false） |
| `IsBlending(entity)` | 前のアニメの形から混ぜている途中か |
| `GetClipDuration(entity, clip)` | そのアニメの長さ（秒） |
| `CalcSpeedToFit(entity, clip, seconds)` | そのアニメを、ちょうど seconds 秒で流しきる速さ |
| `GetPlayer(entity)` | 細かい操作（時刻を決める・止める）用の `AnimationPlayer`。ポインタはそのフレームの中だけ使う |

- どれも、呼んだフレームの `AnimationSystem::Update`（Script・SYSTEM・攻撃表の AI の後）から効く。流し始めたフレームは時刻を進めないので、そのフレームはアニメの 0 秒の形が見える。
- アニメが動かしたノードの位置は、このフレームのワールド行列・描画に入る。
- 混ぜている間、前のアニメは流れ続ける（歩き → 走り で、前のアニメが止まって見えない）。混ぜている途中にまた切り替えたときは、そのとき見えている形で止めて、そこから混ぜる。一時停止・ヒットストップの間は、混ぜるのも止まる。
- 流し始めたフレームは混ぜる割合も 0（前のアニメの形）。`Blend Time` 秒たつと、新しいアニメの形だけになる。

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
