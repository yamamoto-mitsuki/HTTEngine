#pragma once
#include <filesystem>
#include <optional>
#include <vector>

#include <Windows.h>
#include <mmreg.h>


/// <summary>
/// 音声ファイル（wav・mp3 など）を PCM の波形に読む係。Media Foundation はこのファイルの中だけで使う
/// </summary>
namespace SoundDecoder {

// 読んだ音声
struct DecodedSound {
	WAVEFORMATEX format{};  // 波形フォーマット
	std::vector<BYTE> pcm;  // 波形データ（PCM）
};

/// <summary>
/// Media Foundation を使えるようにする。Decode より先に1度だけ呼ぶ
/// </summary>
void Initialize();

/// <summary>
/// Initialize と対になる終了処理
/// </summary>
void Finalize();

/// <summary>
/// 音声ファイルを最後まで読む
/// </summary>
/// <returns>読んだ音声。開けないファイルなら std::nullopt（エラーのログは出す）</returns>
std::optional<DecodedSound> Decode(const std::filesystem::path& filePath);

} // namespace SoundDecoder
