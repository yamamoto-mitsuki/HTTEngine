#pragma once
#include "HTTEngine/ECS/GameObject.h"
#include "HTTEngine/ECS/Script.h"
#include "HTTEngine/Math/Vector3.h"

//=============================================================================
// トリガーでカメラを切り替える（Sandbox のテスト用）
//=============================================================================

/// <summary>
/// RT（キーボードなら Enter）を押した瞬間に、Other Camera の Priority を上げ下げして、カメラを切り替える（Sandbox のテスト用）
/// <para>切り替えは Other Camera の Camera の Blend で補間する。Camera Look At の Screen Position の向き・Camera Volume の Saturation も同じ速さで変わるのを見る</para>
/// <para>LT・RT を押した瞬間・離した瞬間を、そのときの量と一緒に Console に出す（しきい値は既定で押す 0.5・離す 0.4）</para>
/// </summary>
class TriggerCameraSwitcher : public Script {
public:
	void Start() override;
	void Update() override;
	void Fields(ComponentUI& ui) override;

private:
	// ===== Inspector で変える値 =====
	GameObject otherCamera_;    // 切り替える先のカメラ（Camera Component を付けた Entity）
	int switchedPriority_ = 10; // 切り替えている間の Other Camera の Priority（今のカメラより大きくする）

	// ===== 実行中だけの値 =====
	int basePriority_ = 0;    // Start のときの Other Camera の Priority（戻すときの値）
	bool isSwitched_ = false; // Other Camera へ切り替えているか
};


//=============================================================================
// 左右に往復する（Sandbox のテスト用）
//=============================================================================

/// <summary>
/// Entity をワールドの X の向きに往復させる（Sandbox のテスト用。Camera Look At の Smooth Time で遅れて追うのを見る）
/// </summary>
class SideSwing : public Script {
public:
	void Start() override;
	void Update() override;
	void Fields(ComponentUI& ui) override;

private:
	// ===== Inspector で変える値 =====
	float width_ = 3.0f;  // 真ん中から左右へ動く幅（m）
	float period_ = 4.0f; // 1往復の秒

	// ===== 実行中だけの値 =====
	Vector3 center_ = {0.0f, 0.0f, 0.0f}; // 往復の真ん中（Start のときの位置）
	float time_ = 0.0f;                   // 動き始めてからの秒
};
