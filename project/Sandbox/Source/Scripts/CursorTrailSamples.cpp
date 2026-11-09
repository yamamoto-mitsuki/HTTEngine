#include "Source/Scripts/CursorTrailSamples.h"

#include <algorithm>

#include "MyEngine/Components/Effects/TrailComponent.h"
#include "MyEngine/Components/Rendering3D/PrimitiveRendererComponent.h"
#include "MyEngine/Graphics/Renderer/GameScreen.h"
#include "MyEngine/Input/InputManager.h"

SCRIPT(AimCursorSample, "Test");

namespace {
constexpr float kBulletScale = 0.3f;       // 弾の見た目の球の大きさ
constexpr float kTrailWidthRate = 0.6f;    // 弾の Trail の太さ（球の大きさに掛ける）
constexpr float kTrailIntensity = 3.0f;    // 弾の Trail の明るさの倍率（1 より大きいとブルームで光る）
constexpr float kTrailVertexDistance = 0.05f; // 弾の Trail の点の間（m）
} // namespace


//=============================================================================
// AimCursorSample
//=============================================================================
// ===== 初期化 =====
void AimCursorSample::Start() {
	InputManager::SetCursorVisible(false);        // ゲーム画面の上では、この照準がカーソルの代わり
	InputManager::SetCursorConfined(confineOnStart_);  // 閉じ込めると、照準はマウスの動いた量で動く（カーソルがゲーム画面の外へ出ない）
}

// ===== 更新 =====
void AimCursorSample::Update() {
	// --- C キー：OS のカーソルを出す・隠すを切り替える ---
	if (InputManager::IsKeyTriggered(DIK_C)) {
		InputManager::SetCursorVisible(!InputManager::IsCursorVisible());
	}
	// --- L キー：OS のカーソルを閉じ込める・放すを切り替える ---
	if (InputManager::IsKeyTriggered(DIK_L)) {
		InputManager::SetCursorConfined(!InputManager::IsCursorConfined());
	}

	const long deltaX = InputManager::GetMouseDeltaX();
	const long deltaY = InputManager::GetMouseDeltaY();
	if (InputManager::IsCursorConfineActive()) {
		// --- 閉じ込めている：マウスの動いた量（数えた数）→ ピクセルの量（感度を掛ける）→ 画面の位置の量 ---
		// マウスは下が＋、画面の位置は上が＋なので、Y を返す
		const Vector2 pixelMove = {static_cast<float>(deltaX) * pixelsPerCount_, -static_cast<float>(deltaY) * pixelsPerCount_};
		const Vector2 move = GameScreen::PixelToNdc(pixelMove);
		screenPosition_ = {screenPosition_.x + move.x, screenPosition_.y + move.y};
	} else if ((deltaX != 0 || deltaY != 0) && InputManager::IsMouseInGame()) {
		// --- 閉じ込めていない：ゲーム画面の中で動いたら、その位置（画面の位置。端が ±1）---
		screenPosition_ = InputManager::GetMousePositionInGame();
	}

	// --- 右スティック：ピクセルで動かした量を、画面の位置の量に直して足す（真ん中が 0 なので、量にも同じ関数が使える）---
	const Vector2 stick = {InputManager::GetRightStickX(), InputManager::GetRightStickY()};
	const Vector2 move = GameScreen::PixelToNdc(stick * (padSpeed_ * Time::GetRealDeltaTime()));
	screenPosition_ = {std::clamp(screenPosition_.x + move.x, -1.0f, 1.0f), std::clamp(screenPosition_.y + move.y, -1.0f, 1.0f)};

	// --- 画面の位置 → UI のピクセル（Anchor が Center の Translation）---
	const Vector2 pixel = GameScreen::NdcToPixel(screenPosition_);
	transform.translation.x = pixel.x;
	transform.translation.y = pixel.y;
}

// ===== Inspector に出す・保存する値 =====
void AimCursorSample::Fields(ComponentUI& ui) {
	ui.Field("Pad Speed###padSpeed", padSpeed_, AtLeast(0.0f).Tip("右スティックで照準を動かす速さ（ピクセル／秒。倒しきったとき）"));
	ui.Field("Confine On Start###confineOnStart", confineOnStart_, Tip("Start で OS のカーソルをゲーム画面に閉じ込める（照準はマウスの動いた量で動く。L キーで切り替え・エディタは Shift+F1 で放す）"));
	ui.Field("Pixels Per Count###pixelsPerCount", pixelsPerCount_, AtLeast(0.0f).Tip("閉じ込めている間、マウスが1つ数えたら照準を何ピクセル動かすか（感度。1 で Windows の矢印とおよそ同じ）"));
}


//=============================================================================
// TrailFadeShooter
//=============================================================================
// ===== 見せ方 =====
COMPONENT(TrailFadeShooter, "Test") {
	ui.Field("Interval###interval", value.interval, AtLeast(0.01f).Tip("撃つ間隔（秒）"));
	ui.Field("Speed###speed", value.speed, AtLeast(0.0f).Tip("弾の速さ（m/s）"));
	ui.Field("Flight Time###flightTime", value.flightTime, AtLeast(0.0f).Tip("弾が飛ぶ秒（過ぎたら End のやり方でいなくなる）"));
	ui.Field("End###end", value.end, Tip("弾がいなくなる形"));
	ui.Field("After Owner###afterOwner", value.afterOwner, Tip("弾の Trail の After Owner（弾がいなくなった後の尾。Discard：すぐ消える／Shrink：縮む／Fade：形はそのままで薄くなる）"));
	ui.Field("Fade Seconds###fadeSeconds", value.fadeSeconds, AtLeast(0.0f).Tip("弾の Trail の Fade Seconds（Fade のとき、薄くしきるまでの秒）"));
	ui.Field("Trail Life Time###trailLifeTime", value.trailLifeTime, AtLeast(0.0f).Tip("弾の Trail の Life Time（点が消えるまでの秒）"));
	ui.Field("Blend###blend", value.blend, Tip("弾の Trail の Blend（Add・Normal のどちらでも薄くなる）"));
	ui.ColorField("Trail Color###trailColor", value.trailColor);
}

// ===== 撃つ =====
void TrailFadeShooterUpdate(Handle<Entity>, TrailFadeShooter& shooter, TransformComponent& transform, float deltaTime) {
	shooter.timer -= deltaTime;
	if (shooter.timer > 0.0f) {
		return;
	}
	shooter.timer += shooter.interval;

	// --- 弾：見た目の球・動き・光の尾（付くのは次のフレームの頭）---
	const GameObject bullet = GameObject::Create("Trail Bullet");
	bullet.transform.translation = GetWorldPosition(transform);
	bullet.transform.scale = {kBulletScale, kBulletScale, kBulletScale};

	PrimitiveRendererComponent look;
	look.shape = PrimitiveShape::Sphere;
	look.material.color = shooter.trailColor;
	bullet.AddComponent(look);

	TrailFadeBullet move;
	move.velocity = GetWorldForward(transform) * shooter.speed;
	move.flightTime = shooter.flightTime;
	move.timeLeft = shooter.flightTime;
	move.end = shooter.end;
	bullet.AddComponent(move);

	TrailComponent trail;
	trail.lifeTime = shooter.trailLifeTime;
	trail.afterOwner = shooter.afterOwner;
	trail.fadeSeconds = shooter.fadeSeconds;
	trail.minVertexDistance = kTrailVertexDistance;
	trail.width = kBulletScale * kTrailWidthRate;
	trail.color = shooter.trailColor;
	trail.intensity = kTrailIntensity;
	trail.blend = shooter.blend;
	bullet.AddComponent(trail);
}
SYSTEM(TrailFadeShooterUpdate);


//=============================================================================
// TrailFadeBullet
//=============================================================================
// ===== 見せ方 =====
COMPONENT(TrailFadeBullet, "Test") {
	ui.Field("Velocity###velocity", value.velocity, Tip("速さ（m/s。ワールド座標）"));
	ui.Field("Flight Time###flightTime", value.flightTime, AtLeast(0.0f).Tip("1回に飛ぶ秒（RemoveTrail は、Trail を外した後にもう1回飛ぶ）"));
	ui.Field("Time Left###timeLeft", value.timeLeft, AtLeast(0.0f).Tip("いなくなるまでの秒"));
	ui.Field("End###end", value.end, Tip("いなくなる形"));
}

// ===== 動かす・いなくなる =====
void TrailFadeBulletUpdate(Handle<Entity> entity, TrailFadeBullet& bullet, TransformComponent& transform, float deltaTime) {
	transform.translation += bullet.velocity * deltaTime;
	bullet.timeLeft -= deltaTime;
	if (bullet.timeLeft > 0.0f) {
		return;
	}
	// --- Trail だけ外す：球は残して、同じだけ飛んだら消す ---
	if (bullet.end == TrailFadeEnd::RemoveTrail && !bullet.isTrailRemoved) {
		EntityManager::RequestRemove<TrailComponent>(entity);
		bullet.isTrailRemoved = true;
		bullet.timeLeft = bullet.flightTime;
		return;
	}
	EntityManager::Destroy(entity);
}
SYSTEM(TrailFadeBulletUpdate);
