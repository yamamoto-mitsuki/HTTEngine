#include "Source/Scripts/WorldFollowSamples.h"

#include <cmath>

#include "MyEngine/Components/UI/UIWorldFollowComponent.h"

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
