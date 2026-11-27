#include "ShockwaveLoopTest.h"

#include <algorithm>
#include <cmath>

#include <HTTEngine/Components/Rendering3D/PrimitiveRendererComponent.h>
#include <HTTEngine/Math/MathUtility.h>

SCRIPT(ShockwaveLoopTest, "Effects"); // クラス名が保存の名前なので後から変えない

namespace {
constexpr float kStartRadius = 0.05f;  // はじめの半径（m。大きさ 0 だと形が潰れるので少しだけ大きく）
constexpr float kMinFadeRange = 0.01f; // 薄くする区間の長さの下限（0 で割らないように）
} // namespace


//=============================================================================
// 毎フレーム
//=============================================================================
// ===== 始まり =====
void ShockwaveLoopTest::Start() {
	if (const PrimitiveRendererComponent* renderer = GetComponent<PrimitiveRendererComponent>()) {
		baseIntensity_ = renderer->material.intensity;
	}
}

// ===== 毎フレーム =====
void ShockwaveLoopTest::Update() {
	PrimitiveRendererComponent* renderer = GetComponent<PrimitiveRendererComponent>();
	if (renderer == nullptr) {
		return;
	}

	// --- 時間を進める（広がる＋休む で 1 回。終わったら頭に戻る）---
	const float cycle = lifeTime_ + interval_;
	elapsedTime_ = std::fmod(elapsedTime_ + Time::GetDeltaTime(), cycle); // std::fmod：小数の割り算の余り
	const bool isExpanding = elapsedTime_ < lifeTime_;
	renderer->enabled = isExpanding; // 休んでいる間は描かない
	if (!isExpanding) {
		return;
	}

	// --- 広げる：はじめ速く、だんだん遅く（1 −（1 − t）²）---
	const float progress = elapsedTime_ / lifeTime_;
	const float eased = 1.0f - (1.0f - progress) * (1.0f - progress);
	const float radius = MathUtility::Lerp(kStartRadius, maxRadius_, eased);
	transform.scale.x = radius * 2.0f; // 筒は Scale 1 で直径 1
	transform.scale.z = radius * 2.0f;

	// --- 回す（ギザギザが流れて見える。rotation はクォータニオンなので、角度は Rotate で足す）---
	Rotate(transform, {0.0f, 1.0f, 0.0f}, spinSpeed_ * Time::GetDeltaTime());

	// --- 終わりに向けて薄くする（加算なので、暗くする ＝ 消えていく）---
	const float fade = std::clamp((1.0f - progress) / (std::max)(1.0f - fadeStart_, kMinFadeRange), 0.0f, 1.0f);
	renderer->material.intensity = baseIntensity_ * fade;
}


//=============================================================================
// Inspector に出す・保存する値
//=============================================================================
void ShockwaveLoopTest::Fields(ComponentUI& ui) {
	ui.Field("Life Time", lifeTime_, AtLeast(0.05f).Tip("広がりきるまでの秒"));
	ui.Field("Max Radius", maxRadius_, AtLeast(kStartRadius).Tip("広がりきった半径（m）"));
	ui.Field("Interval", interval_, AtLeast(0.0f).Tip("消えてから次が出るまでの秒"));
	ui.Field("Spin Speed", spinSpeed_, Tip("Y 軸で回る速さ（度/秒）"));
	ui.Field("Fade Start", fadeStart_, Range(0.0f, 1.0f).Tip("進み具合（0〜1）がここを過ぎたら、薄くしはじめる"));
}
