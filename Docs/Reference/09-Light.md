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
| `DirectionalLightComponent` | `color` / `intensity`。**有効な物のうち最初の1つだけ**が使われる（2つ以上あると警告） |
| `PointLightComponent` | `color` / `intensity` / `radius` / `decay`。64 個まで（超えた分は使われず、警告が出る） |
| `SpotLightComponent` | `color` / `intensity` / `range` / `decay` / `outerAngle` / `innerAngle`（度、89まで）。32 個まで |

- `color` は sRGB の `Vector3`（0〜1）。どれも `gizmo`（エディタのアイコン・範囲の線を出すか）を持つ。ゲームの絵には関係しない。

```cpp
// ✗ だめな例
struct DirectionalLightComponent { Vector3 direction; }; // ✗ 向きはTransformから取る（二重管理になる）
```

Hierarchyの **＋ → Light**（Directional Light・Point Light・Spot Light）から作るのが早い（位置と向きの初期値もUnityと同じに入る）。
