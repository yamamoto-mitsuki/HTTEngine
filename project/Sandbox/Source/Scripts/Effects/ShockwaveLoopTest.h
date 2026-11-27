#pragma once
#include <HTTEngine/ECS/Script.h>

//=============================================================================
// 広がる輪の確かめ（Primitive Renderer の Tube を、衝撃波のように広げては消す、をくり返す）
//=============================================================================
// Scale の X・Z を、ほぼ 0 から Max Radius の直径まで広げる（はじめ速く、だんだん遅く）
// 終わりに向けて Intensity を下げて消し、Interval 秒休んで、また始める
// Y 軸で回しながら広げると、テクスチャのギザギザが流れて見える


class ShockwaveLoopTest : public Script {
public:
	void Start() override;
	void Update() override;
	void Fields(ComponentUI& ui) override;

private:
	// ===== Inspector で変える値 =====
	float lifeTime_ = 1.0f;   // 広がりきるまでの秒
	float maxRadius_ = 6.0f;  // 広がりきった半径（m）
	float interval_ = 0.5f;   // 消えてから次が出るまでの秒
	float spinSpeed_ = 60.0f; // Y 軸で回る速さ（度/秒）
	float fadeStart_ = 0.5f;  // 進み具合（0〜1）がここを過ぎたら、薄くしはじめる

	// ===== 実行中のみ使用 =====
	float elapsedTime_ = 0.0f;   // 1 回ぶん（広がる＋休む）の中の、今の時間
	float baseIntensity_ = 1.0f; // はじめの明るさ（Primitive Renderer の Intensity）
};
