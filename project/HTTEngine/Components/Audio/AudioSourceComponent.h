#pragma once
#include <cstdint>

#include "HTTEngine/Sound/AudioBus.h"

// 音を鳴らすためのデータ。Entityに付けるComponent（カテゴリ：Audio）
// 鳴っている音の番号（SoundManager の再生ハンドル）は持たない。AudioSystem が持つ
// （ここに入れると、コピー・Undo で2つの AudioSource が同じ音を指してしまう）

/// <summary>
/// 音源
/// </summary>
struct AudioSourceComponent {
	uint32_t soundHandle = 0;    // SoundManager::Load の番号（0は未選択）。保存はパスで
	float volume = 1.0f;         // 0=無音、1=元の大きさ、2=2倍
	float pitch = 1.0f;          // 0.5=1オクターブ下、1=元の高さ、2=1オクターブ上
	bool loop = false;           // 最後まで鳴ったら頭から鳴らし直す（BGM）
	bool playOnStart = true;     // シーンが始まったとき（Play を押したとき）に鳴らす
	AudioBus bus = AudioBus::SE; // 音の種類。種類ごとの音量（Parameters の Audio / Volume）が掛かる

	// --- 3Dの音（聞く人＝ゲームのカメラ）---
	bool spatial = false;      // true: 離れると小さくなり、左右に振れる（位置は Transform）
	float minDistance = 1.0f;  // ここまで近いと元の大きさ
	float maxDistance = 20.0f; // ここより遠いと聞こえない（その間はまっすぐ小さくなる）
};