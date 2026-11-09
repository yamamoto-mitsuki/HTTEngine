#pragma once
#include <array>
#include <cstddef>

#include "HTTEngine/ECS/Script.h"
#include "HTTEngine/Systems/Animation/AnimationSystem.h"

/// <summary>
/// アニメを名前で順に流す（Sandbox のテスト用。AnimationSystem::Play・CalcSpeedToFit を名前で呼ぶ見本）
/// <para>Clip 1 → Clip 2 → Clip 3 → Clip 1 … を、どれも Seconds Per Clip 秒でちょうど1回流しきる（ループしない）</para>
/// <para>同じ Entity に Animator を付ける。Clip の欄は、その Animator のファイルの中の名前から選べる</para>
/// </summary>
class ClipCycler : public Script {
public:
	void Start() override;
	void Update() override;
	void Fields(ComponentUI& ui) override;

private:
	// 次の欄のアニメを流す（空の欄は飛ばす）
	void PlayNext();

	static constexpr size_t kClipCount = 3; // 順に流すアニメの数

	std::array<AnimationClipName, kClipCount> clips_ = {"Spin", "Bounce", "Grow"}; // 順に流すアニメの名前（空の欄は飛ばす）
	float secondsPerClip_ = 1.0f; // 1つのアニメを流しきる秒（長さが違うアニメも、この秒に合わせる）
	size_t current_ = 0;          // 今流しているのは clips_ の何番目か（実行中だけ）
	float timer_ = 0.0f;          // 次のアニメまでの秒（実行中だけ）
};
