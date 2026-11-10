#pragma once
#include <HTTEngine/ECS/Script.h>

//=============================================================================
// プレイヤーの操作（歩く・跳ぶ・進む向きを向く）
//=============================================================================
// 同じ Entity に Collider（Capsule）と Character Controller を付けて使う


class PlayerController : public Script {
public:
	void Start() override;
	void Update() override;
	void Fields(ComponentUI& ui) override;

private:
	// ===== Inspectorで変える値 =====
	float moveSpeed_ = 4.0f;       // 移動速度（m/秒）
	float jumpSpeed_ = 6.0f;       // 跳ぶ瞬間の上向きの速さ（m/秒）
	float turnSmoothTime_ = 0.08f; // 向きが追いつくまでの時間（秒）

	// ===== 実行中のみ使用 =====
	float facingYaw_ = 0.0f; // 今の向き（度。0 で奥、90 で右）
};
