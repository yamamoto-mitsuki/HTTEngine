#pragma once
#include "HTTEngine/ECS/Script.h"

/// <summary>
/// Character Controller をキーボードで動かす（Sandbox のテスト用）
/// <para>W・A・S・D：ワールドの +Z・−X・−Z・+X へ歩く / Space：跳ぶ / Left Shift：ダッシュ（Dash Speed で Dash Time 秒、歩いていた向きへ）</para>
/// <para>同じ Entity に Collider（Capsule）と Character Controller を付ける。当たりの知らせ（OnCollisionEnter・Exit）を Console に出す</para>
/// </summary>
class CharacterKeyboardMover : public Script {
public:
	void Update() override;
	void OnCollisionEnter(const CollisionInfo& collision) override;
	void OnCollisionExit(const CollisionInfo& collision) override;
	void Fields(ComponentUI& ui) override;

private:
	float moveSpeed_ = 6.0f;                     // 歩く速さ（m/s）
	float jumpSpeed_ = 6.0f;                     // 跳んだ瞬間の上向きの速さ（m/s）
	float dashSpeed_ = 18.0f;                    // ダッシュの速さ（m/s）
	float dashTime_ = 0.18f;                     // ダッシュの長さ（秒）
	float dashRemaining_ = 0.0f;                 // ダッシュの残り時間（秒。0 ならダッシュしていない）
	Vector3 facing_ = {0.0f, 0.0f, 1.0f};        // 最後に歩いた向き（ダッシュの向き。長さ1）
};
