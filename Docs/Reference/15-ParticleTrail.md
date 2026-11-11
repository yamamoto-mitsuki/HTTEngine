> HTTEngine リファレンスの1章。ほかの章は [索引](../Reference.md) から探す。

# 15. パーティクル・軌跡

## パーティクル（Particle System）

小さな板をたくさん出して、動かして消す Component（Unity の Particle System の小さい版）。Hierarchy の **＋ → Effects → Particle System**、または Inspector の **Add Component → Effects → Particle System** で付ける。**Component は「どう出すか」の設定だけ**で、粒は `ParticleSystem` が持つ（Trail と同じ。粒は保存されず、Play / Stop で消える）。停止中も Inspector の **▶ Preview** で流して調整できる（選ぶ物を変えると止まる）。

| 見出し | 項目 | 中身 |
|---|---|---|
| Playback | Play On Start・Looping・Duration・Stop Action | Play で流し始める（外すと `ParticleSystem::Play` まで出さない）／Duration 秒ごとにくり返す（Burst もまた）／1回の長さ／出し終わって（Looping でなく Duration が過ぎた・`Stop` を呼んだ）粒が全部消えたらすること（`None`：何もしない・`Destroy`：この Entity を子ごと消す） |
| Emission | Rate Over Time・Burst Count・Max Particles | 1秒に出す数／1回の始めにまとめて出す数／同時に出ている数の上限（4096 まで。いっぱいの間は出さない） |
| Shape | Shape・Radius・Cone Angle・Box Size・From Surface・Show Gizmo | Point（全部の向き）・Sphere・Circle（**XY の面**。横から見る輪）・Cone（+Y へ開く）・Box。From Surface で表面（縁）から。向きは真ん中から外へ。Entity の向きで回る。Show Gizmo で形を Scene ビューに桃色の線で出す（Cone は縁から出た粒の開く向きも。View → Gizmos → Particle Shapes で全部まとめて消せる） |
| Start (Min, Max) | Lifetime・Speed・Size・Color A・Color B・Intensity | 左と右の間でランダム（Size は m で板の1辺）。色は A と B の間。Intensity は全部の粒の明るさの倍率（1 より大きいとブルームで光る） |
| Over Lifetime | Gravity・Drag・End Size Rate・End Color・Ease・Converge・Converge Ease | 加速（m/s²）／1秒あたりの減速の割合／最後の大きさ（初めに掛ける。0 で縮んで消える）／最後の色（初めに掛ける。A 0 でフェードアウト）／大きさと色の変わり方／中心（Entity の位置）へ集まり、寿命の終わりにちょうど着く（Speed・Gravity・Drag は使わない。From Surface と合わせると輪・球の表面から吸い込まれる）／集まり方（InQuad：だんだん速く） |
| Rotation | Start Rotation・Angular Velocity・Random Spin Direction・Align To Velocity・Stretch | 画面の上で板を回す（度。**正で左回り**＝Transform の Rotation Z と同じ）。初めの角度・回る速さ（度/秒）は左と右の間でランダム／半分の粒を逆に／絵の右（+X）を進む向きへ／絵の横だけ伸ばす（Align と合わせて線） |
| Renderer | Texture・Blend・Space・Use Fog・Use Depth Of Field | 無ければ**丸いぼかし**（白。色は Color で）／Add（光る）・Normal（奥から順には並べない）／World（出た所に残る）・Local（Entity と一緒に動く）／霧を混ぜるか（初めは ✓）／被写界深度でぼかすか（初めは外す。ぼけ方は粒の後ろにある物の距離で決まるので、奥の層の葉・湯気に） |

```cpp
#include "HTTEngine/Systems/Effects/ParticleSystem.h" // GameComponent.h を include していれば要らない

// 子の「Burst」（Play On Start・Looping を外した、使ったら飛び散る物）を最初から
GameObject burst = gameObject.FindChild("Burst");
if (burst) {
	ParticleSystem::Play(burst.handle);       // 最初から（Burst も出る）。出している途中で呼んでも最初から
}
GameObject sparkle = gameObject.FindChild("Sparkle");
ParticleSystem::Stop(sparkle.handle);         // 出すのをやめる（出ている粒は寿命まで残る）。無い（無効な Handle）なら何もしない
ParticleSystem::Stop(sparkle.handle, true);   // 出ている粒もすぐ消す
ParticleSystem::Emit(burst.handle, 10);       // 今すぐ 10 個（Play していなくても、Component の値で出る）
if (!ParticleSystem::IsAlive(burst.handle)) { … } // 出している途中でも、粒が残ってもいない（全部消えた）
```

- 1つの Entity に Particle System は1つ。キラキラと飛び散るの2つが要る物は、子を2つ作る（`FindChild` で探す）。
- Prefab から作った同じフレームに `Play` してもよい（付くのは次のフレームの頭だが、覚えている）。`Emit` は Component が付いてから（付く前に呼ぶと何もしない）。
- 飛び散って終わる物は、Prefab にして Looping を外し **Stop Action を Destroy** にすれば、出し終わったら Entity ごと消える（`IsAlive` を見て消すコードは要らない）。
- ゲームが動き出した最初のフレーム（製品版の起動・エディタの Play。Script の `Start`）に `Play`・`Emit` してもよい。
- 粒は `Time::GetDeltaTime` で動く（ポーズ・ヒットストップで止まる）。Use Depth Of Field を外した粒は被写界深度の後に描く（ぼけない）。

## 前からのパーティクル（ParticleManager・ParticleEmitter）

コードでグループを作って粒を1個ずつ登録する、前からの仕組み（古いゲームが使っているので残してある）。**新しく作る物は Particle System を使う。**

```cpp
// グループを作る（Initializeで1回）
ParticleGroupSetting setting;
setting.textureHandle = TextureManager::Load("resources/particle.png");
groupHandle_ = ParticleManager::CreateGroup(setting);

// 出す
Particle p;
p.transform.translation = {0.0f, 1.0f, 0.0f};
p.velocity = RandomEngine::GetInsideUnitSphere();
p.color = {1.0f, 1.0f, 1.0f, 1.0f}; // 色は初期値が無いので必ず入れる
p.lifeTime = 1.0f;
ParticleManager::Register(groupHandle_, p);
```

1グループ最大4096個。カメラはエンジンが毎フレーム渡す。**シーンを入れ替える（Play / Stop・`LoadScene`）と、グループは全部消える**（`ParticleManager::ClearGroups`。エミッターはシーンのメンバに置く）。

## 軌跡（Trail）

通った跡・描いた線を、帯にして描く Component（Unity の Trail Renderer）。Inspector の **Add Component → Effects → Trail** で付ける。**Component は「どう描くか」の設定だけ**で、点の列は `TrailSystem` が持つ（Component に `std::vector` は入れられないので。AudioSource と鳴っている音の関係と同じ）。点は保存されず、停止中は持たない。

| 使い方 | 設定 | 例 |
|---|---|---|
| **動いた跡** | Emitting に印（動くたびに自動で点が増え、Life Time 秒で消える） | 剣の残像・高速移動の跡 |
| **描いた線** | Emitting を外し、Life Time 0（消えない）。点は `TrailSystem::AddPoint` で足す | マウスで描く線・地面に残るインク |

| 項目 | 中身 |
|---|---|
| Min Vertex Distance | 動いた跡で、この距離動くたびに1点 |
| Life Time | 点が消えるまでの秒（0 で消えない） |
| After Owner / Fade Seconds | 持ち主（Entity かこの Component）が消えた後の尾の消え方（新しい点は足さない）。`Discard`：すぐ捨てる／`Shrink`（初期値）：点が寿命で消えて縮む（Life Time 0 なら捨てる）／`Fade`：形はそのままで Fade Seconds かけて薄くして消す |
| Max Points | 点の上限（初期値 256。超えたら古い方から消す） |
| Width / End Width Rate | 新しい方の端の太さ／古い方の端の太さ（Width に掛ける。1 でずっと同じ） |
| Alignment | View：カメラの方を向く／Ground：地面に寝かせる（帯の面が真上） |
| Color・Intensity / Texture / Blend | 色（光の当たり方は使わない）と明るさの倍率／U が古い方 0 → 新しい方 1、V が幅の向き／Add で光る線 |

```cpp
// 描いた線：線1本＝Entity 1つ。RequestAdd と同じフレームから AddPoint してよい
TrailComponent trail;
trail.emitting = false;
trail.lifeTime = 0.0f;
trail.alignment = TrailAlignment::Ground;
const Handle<Entity> line = EntityManager::Create("InkLine");
EntityManager::RequestAdd<TrailComponent>(line, trail);
TrailSystem::AddPoint(line, start);

// 点を読む（古い方 → 新しい方）・長さ・消す
for (const TrailPoint& point : TrailSystem::GetPoints(line)) { … } // point.position・point.age
const float length = TrailSystem::CalcLength(line);
EntityManager::Destroy(line); // 線ごと消す（Life Time 0 なので点もすぐ消える。Life Time がある尾は After Owner の消え方で残る）。点だけ消すなら TrailSystem::Clear(line)
```

- `GetPoints` の結果はその場で使う（`AddPoint` すると使えなくなる。最後の点が要るなら先に写す）。持ち主から離れて消えている途中の尾は、もう誰の物でもないので出ない。
- シーンを変える（Play / Stop・`LoadScene`）と、消えている途中の尾も全部捨てる（`TrailSystem::ClearAll`）。
- 線に触れているかは、点と点の間の線分ごとに `Collision::ClosestPointOnSegment` で見る（14章）。
