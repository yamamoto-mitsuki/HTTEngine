> HTTEngine リファレンスの1章。ほかの章は [索引](../Reference.md) から探す。

# 9. ライト

**ライトもEntityに付けるComponent。** 位置と向きは付けたEntityのTransformから取る（向き＝ローカルの +Z）。

```cpp
const Handle<Entity> sun = EntityManager::Create("Directional Light"); // CreateDefaultEntities の中で（エディタで置くなら Hierarchy の ＋ → Light）
TransformComponent* transform = EntityManager::Get<TransformComponent>(sun);
SetEulerAngles(*transform, {50.0f, -30.0f, 0.0f}); // 角度（度）で向ける（rotation はクォータニオン。7章）
EntityManager::RequestAdd<DirectionalLightComponent>(sun);
```

| 種類 | 主な項目 |
|---|---|
| `DirectionalLightComponent` | `color` / `intensity` / `shadow`（影。下の「影」）。**有効な物のうち最初の1つだけ**が使われる（2つ以上あると警告） |
| `PointLightComponent` | `color` / `intensity` / `radius` / `decay`。64 個まで（超えた分は使われず、警告が出る） |
| `SpotLightComponent` | `color` / `intensity` / `range` / `decay` / `outerAngle` / `innerAngle`（度、89まで）。32 個まで |

- `color` は sRGB の `Vector3`（0〜1）。どれも `gizmo`（エディタのアイコン・範囲の線を出すか）を持つ。ゲームの絵には関係しない。

```cpp
// ✗ だめな例
struct DirectionalLightComponent { Vector3 direction; }; // ✗ 向きはTransformから取る（二重管理になる）
```

Hierarchyの **＋ → Light**（Directional Light・Point Light・Spot Light）から作るのが早い（位置と向きの初期値もUnityと同じに入る）。

## 影（平行光源）

**平行光源の `shadow` で影を出す。** 物の方は Model Renderer・Primitive の `Cast Shadows`（`castsShadows`）が入っている不透明な物が影を落とす。どちらも既定で入っている（前のシーンのファイルも、開くと影が出る）。

| 項目（`DirectionalLightComponent::shadow`） | 意味 |
|---|---|
| `castsShadows` | 影を出す |
| `mapSize` | ライトから見た深度の絵の大きさ（`Size1024`・`Size2048`・`Size4096`） |
| `distance` | カメラからこの距離（m）まで影を出す（最後の 1 割で薄くなって消える）。短いほど影が細かい |
| `strength` | 影の濃さ（0〜1） |
| `bias` | 深度を比べる前に引く距離（m）。自分の影が縞になるなら上げる、影が物から浮くなら下げる |

```cpp
// 影を落とさない物（足元の目印・光る板など）
ModelRendererComponent* render = EntityManager::Get<ModelRendererComponent>(marker);
render->castsShadows = false;

// 影の濃さを変える
DirectionalLightComponent* sunLight = EntityManager::Get<DirectionalLightComponent>(sun);
sunLight->shadow.strength = 0.7f;
```

- 影を受けるのは、光を使う Shader（Lambert〜PBR）の不透明な物。半透明な物は影を落とさず、受けもしない。Unlit は光を使わないので変わらない。
- ポイントライト・スポットライトの影はまだ無い。
- Lambert・Half Lambert は環境光が無いので、`strength` が 1 だと影が真っ黒になる。
- 影の深度は、ビューのカメラに合わせて描き直す（エディタの Scene ビューと Game ビューで別々）。

```cpp
// ✗ だめな例
sunLight->intensity = 0.0f; // ✗ 影だけ消したいのに、光も消える（shadow.castsShadows を外す）
```

## 環境光（空の光）

**Add Component → Lighting → Environment Light**（`EnvironmentLightComponent`）。シーンに1つ（どの Entity でもよい。2つ以上あれば先に見つかった物）。
空の画像（IBL）から来る光と、空の絵の明るさを決める。**無ければ全部 1**（空の画像の明るさのまま）。

| 項目 | 意味 |
|---|---|
| `intensity` | 空から来る、ぼんやりした光の強さ（PBR の物の全部の面に当たる）。下げると、平行光源の当たる面と影の差が出る |
| `reflectionIntensity` | 空の映り込みの強さ（金属・つるつるした物） |
| `skyIntensity` | 空の絵の明るさ（Camera の Background が Skybox のとき）。物を照らす光は変わらない |

```cpp
EnvironmentLightComponent* environment = EntityManager::Get<EnvironmentLightComponent>(sun);
environment->intensity = 0.4f; // 空の光を弱めて、太陽の光を目立たせる
```

- 効くのは PBR の物だけ（ほかの Shader は環境光を持たない）。
- 空を回しても（`Skybox::SetRotationY`）、空の光の向きはまだ回らない。

```cpp
// ✗ だめな例
sunLight->intensity = 0.3f; // ✗ 全体がまぶしいのに太陽を弱める（空の光が勝って、もっとのっぺりする。environment->intensity を下げる）
```
