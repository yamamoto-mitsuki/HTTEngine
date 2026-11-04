#include "Source/Scripts/HitboxCycler.h"

SCRIPT(HitboxCycler, "Test");

//=============================================================================
// 毎フレーム
//=============================================================================
void HitboxCycler::Start() { timer_ = startDelay_; }

void HitboxCycler::Update() {
	const HitboxComponent* hitbox = GetComponent<HitboxComponent>();
	if (hitbox == nullptr || hitbox->phase != HitboxPhase::Off) {
		return; // 窓が開いている間は、Hitbox が時間で進める
	}
	timer_ -= Time::GetDeltaTime();
	if (timer_ > 0.0f) {
		return;
	}
	timer_ = restSeconds_;
	HitSystem::Warn(gameObject.handle, warningSeconds_, activeSeconds_); // 予告 → 時間が来たら Active → 時間が来たら Off
}


//=============================================================================
// Inspector と保存
//=============================================================================
void HitboxCycler::Fields(ComponentUI& ui) {
	ui.Field("Rest Seconds###restSeconds", restSeconds_, AtLeast(0.0f).Tip("窓を閉じている秒"));
	ui.Field("Warning Seconds###warningSeconds", warningSeconds_, AtLeast(0.0f).Tip("予告の秒（Just Lead Time 秒前から、ジャスト判定の範囲が有効）"));
	ui.Field("Active Seconds###activeSeconds", activeSeconds_, AtLeast(0.0f).Tip("当たる秒"));
	ui.Field("Start Delay###startDelay", startDelay_, AtLeast(0.0f).Tip("最初の予告までの秒（いくつも置いたとき、ずらす）"));
}
