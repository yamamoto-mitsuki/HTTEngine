#pragma once
#include <cstdint>

#include "HTTEngine/Core/EnumTips.h"

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

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(AudioBus) {
	using enum AudioBus;
	return MakeEnumTips<AudioBus>({
	    {BGM, "音楽（Parameters ウィンドウの Audio / Volume の BGM の音量が掛かる）"},
	    {SE, "効果音（Parameters ウィンドウの Audio / Volume の SE の音量が掛かる）"},
	});
}
