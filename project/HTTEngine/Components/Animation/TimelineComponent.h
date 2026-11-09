#pragma once
#include <cstdint>
#include <type_traits>

#include "HTTEngine/Time/EngineTime.h"

// Timeline を再生する。Entityに付けるComponent（カテゴリ：Animation）
// 中身はファイル（.timeline）に書く。ここは「どのファイルをどう再生するか」だけを持つ
// 今の時間・再生の状態は TimelineSystem が持つ（数が変わる物はバイト列で写す Component に入れられず、同じファイルを何体でも再生できるように）

/// <summary>
/// Timeline の再生の設定
/// </summary>
struct TimelineComponent {
	uint32_t timeline = 0;              // 再生するファイル（AssetType::Timeline の番号）
	bool playOnStart = true;            // 再生が始まったら、すぐ再生する（false なら TimelineSystem::Play を呼ぶまで止まっている）
	bool loop = false;                  // 最後まで行ったら最初へ戻る
	float speed = 1.0f;                 // 再生速度（1 で普通。0 の間は進まない。負の値は 0 と同じ）
	TimeMode timeMode = TimeMode::Game; // 使う時間
};

static_assert(std::is_trivially_copyable_v<TimelineComponent>);