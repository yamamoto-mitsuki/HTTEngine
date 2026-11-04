#pragma once
#include "MyEngine/ECS/Script.h"

/// <summary>
/// Hitbox の窓をくり返し開ける（Sandbox のテスト用。窓を開けるのは呼ぶ側、の見本）
/// <para>Off（Rest Seconds）→ Warning（Warning Seconds）→ Active（Active Seconds）→ Off … をくり返す。Warn に Active の秒も渡して、Hitbox に進めてもらう</para>
/// <para>同じ Entity に Hitbox（と Damage Source）を付ける</para>
/// </summary>
class HitboxCycler : public Script {
public:
	void Start() override;
	void Update() override;
	void Fields(ComponentUI& ui) override;

private:
	float restSeconds_ = 1.5f;    // 窓を閉じている秒
	float warningSeconds_ = 1.2f; // 予告の秒
	float activeSeconds_ = 0.3f;  // 当たる秒
	float startDelay_ = 0.0f;     // 最初の予告までの秒（いくつも置いたとき、ずらす）
	float timer_ = 0.0f;          // 次の予告までの秒（実行中だけ）
};
