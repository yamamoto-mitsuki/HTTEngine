#pragma once
#include <string>
#include <vector>

#include "HTTEngine/ECS/GameObject.h"
#include "HTTEngine/ECS/Script.h"

//=============================================================================
// カメラを番号で選ぶ（Sandbox のテスト用）
//=============================================================================

/// <summary>
/// Cameras に入れたカメラから、写すカメラを選ぶ（Sandbox のテスト用。Camera Orbit の Heading の違いを見比べる）
/// <para>1〜9 キー：その番号のカメラ / Tab・RB：次のカメラ / LB：前のカメラ</para>
/// <para>選んだカメラの Priority を Active Priority に、ほかを 0 にする（切り替えは選んだカメラの Camera の Blend で補間する）</para>
/// <para>Label に Text の付いた Entity を入れると、カメラの一覧と、選んだカメラの Camera Orbit の Heading・Yaw などを出す</para>
/// </summary>
class OrbitCameraSwitcher : public Script {
public:
	void Start() override;
	void Update() override;
	void Fields(ComponentUI& ui) override;

private:
	// 選んだカメラの Priority を上げて、Label の文字を書き直す
	void Select(int index);
	// Label に出す文字（カメラの一覧と、選んだカメラの Camera Orbit の値）
	std::string MakeLabel() const;

	// ===== Inspector で変える値 =====
	std::vector<GameObject> cameras_; // 選べるカメラ（Camera Component を付けた Entity。上から 1・2・3…）
	GameObject label_;                // 一覧を出す Text の Entity（空なら出さない）
	int activePriority_ = 10;         // 選んだカメラの Priority（ほかのカメラは 0）

	// ===== 実行中だけの値 =====
	int selected_ = -1; // 今選んでいるカメラの番号（0 から。-1 はまだ選んでいない）
};


//=============================================================================
// Camera Orbit の向きを入力で回す（Sandbox のテスト用）
//=============================================================================

/// <summary>
/// 同じ Entity の Camera Orbit の Yaw・Pitch を、キーとスティックで書き換える（Sandbox のテスト用）
/// <para>Q・E か右スティックの左右：Yaw / R・F か右スティックの上下：Pitch</para>
/// <para>Heading が World のカメラに付けると、スティックで回す三人称のカメラになる（ほかの Heading では、決まった向きからのずれを変える）</para>
/// </summary>
class OrbitYawInput : public Script {
public:
	void Update() override;
	void Fields(ComponentUI& ui) override;

private:
	float yawSpeed_ = 120.0f;  // 横に回す速さ（度/秒）
	float pitchSpeed_ = 60.0f; // 縦に回す速さ（度/秒）
	float minPitch_ = -30.0f;  // Pitch の下限（度。見上げる）
	float maxPitch_ = 70.0f;   // Pitch の上限（度。見下ろす）
};
