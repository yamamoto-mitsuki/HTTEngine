> HTTEngine リファレンスの1章。ほかの章は [索引](../Reference.md) から探す。

# 24. キャラを歩かせる（Character Controller）

**Add Component → Physics → Character Controller**（`CharacterControllerComponent`）。プレイヤー・敵のような「歩くキャラ」を、坂・段・壁に沿って動かす（Unity の CharacterController・Unreal の CharacterMovementComponent の小さい版）。

- **形は同じ Entity の Collider**（Sphere か Capsule）。Collider が無い・無効・Box・Polygon なら動かさない。形を2か所に持たない。
- Rigidbody と違い、**跳ね返らず・押されず・坂で滑らない**。立てる坂の上では止まり、小さな段は上り、下り坂・段を下りるときは地面に吸い付く。
- ほかの Rigidbody からは「動く壁」に見える。**Rigidbody と一緒に付けない**（付けたら Rigidbody が動かし、こちらは何もしない）。
- 動かすのは再生中だけ。Script・SYSTEM の後、物理の前（[1フレームの順番](01-Overview.md#1フレームの順番) の 5）。動かす前に、当たりの形を今の位置で集め直す。

| 項目 | 中身 |
|---|---|
| Slope Limit | 立てる坂の角度（度。初めは 45）。これより緩い面は地面（止まっても滑らない）。急な面は壁と同じく、沿って滑る |
| Step Offset | そのまま上れる段の高さ（m。初めは 0.3）。下り坂・段を下りるときに地面へ吸い付く距離にも使う |
| Use Gravity・Gravity Scale | 空中で重力で落ちるか・その倍率（重力の向き・強さは Settings の Physics → World → Gravity。Rigidbody と同じ値） |
| Push Rigidbodies・Push Mass | ぶつかった Rigidbody（Is Kinematic でない物）を押すか・押すときのキャラの重さ（kg）。押す速さ ＝ キャラの速さ × Push Mass ÷ (Push Mass ＋ 相手の重さ) |
| Freeze Position X・Y・Z | その軸には動かさない（横から見るゲームなら Z を止める） |

## 動かし方：`velocity` の X・Z を書く

```cpp
void PlayerController::Update() {
	CharacterControllerComponent* controller = GetComponent<CharacterControllerComponent>();
	if (controller == nullptr) {
		return;
	}
	const Vector2 move = InputManager::GetMoveAxis();
	controller->velocity.x = move.x * moveSpeed_; // X・Z は毎フレーム書く
	controller->velocity.z = move.y * moveSpeed_;
	if (controller->IsGrounded() && InputManager::IsKeyTriggered(DIK_SPACE)) {
		controller->velocity.y = jumpSpeed_;        // Y は跳ぶときだけ書く（あとは重力に任せる）
	}
}
```

- `velocity` はワールド座標の速さ（m/秒）。Y は空中で重力が足され、地面にいる間は 0。壁に当たると壁へ向かう分が消える。
- 回転はしない。向きは自分で Transform を回す（`Rotate`・`SetEulerAngles`。[7章](07-Transform.md)）。

## 実行中に読める値（保存しない）

| 値 | 中身 |
|---|---|
| `IsGrounded()` | 地面に乗っているか（前のフレームの終わりに） |
| `movementMode` | `Walking`（地面）／`Falling`（空中。跳んだ・段から落ちた・立てない急な坂の上） |
| `groundNormal`・`groundPoint`・`groundEntity` | 乗っている地面の向き・触れている所・地面の Entity（Walking の間だけ意味がある） |
| `hitSides`・`hitAbove`・`hitBelow` | このフレームに横（壁）・上（天井）・下（床）に当たったか |

- Rigidbody か Character Controller かを気にせずに「速さ・地面にいるか」を聞くなら `MotionQuery::GetVelocity(entity)`・`MotionQuery::IsGrounded(entity)`（Camera Follow の Ground Snap Y・Trigger Activator の AnyMover もこれを使う）。
- 当たりは Script の `OnCollision〜` に来る（Rigidbody と同じ。[14章](14-MathCollision.md)）。
- Scene ビューに上れる段の高さ・地面の向きが出る（Collider のギズモと一緒に出す・消す。View → Colliders）。

```cpp
// ✗ だめな例
transform.translation += move * moveSpeed_ * Time::GetDeltaTime(); // ✗ Transform を直接動かすと、坂・段・壁に沿わない（壁にめり込んでから押し戻される）
controller->velocity.y = 0.0f;                                      // ✗ 毎フレーム Y を消すと落ちない（Y は跳ぶときだけ書く）
```
