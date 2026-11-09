#pragma once
#include <cstdint>

// 音の種類。鳴らす音は必ずどちらかを通り、種類ごとに音量を決められる
// （Parameters ウィンドウの Audio / Volume、ゲーム空はAudioSystem::SetBusVolume）
// SoundManager.hは XAudio2 を読むので重い。Component からはこのヘッダだけを読む

/// <summary>
/// 音の種類（音量のつまみ）
/// </summary>
enum class AudioBus : uint8_t {
	BGM, // 音楽
	SE,  // 効果音
};