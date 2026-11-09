#include "HTTEngine/Sound/SoundDecoder.h"

#include <format>
#include <memory>

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl.h>

#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/String/ConvertString.h"

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace {
constexpr DWORD kAudioStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM); // 読む音声の番号（最初の音声）

// CoTaskMemFree で返す決まりのメモリを、unique_ptr に返してもらうための「消し方」
// （unique_ptr はふつう delete で消すので、消し方だけ差し替える）
struct CoTaskMemDeleter {
	void operator()(void* p) const { CoTaskMemFree(p); }
};

/// <summary>
/// 読み出す形式を PCM にして、その波形フォーマットを返す
/// </summary>
WAVEFORMATEX SetupPcmFormat(IMFSourceReader* reader) {
	// 出力フォーマットをPCMに設定
	Microsoft::WRL::ComPtr<IMFMediaType> type;
	HRESULT hr = MFCreateMediaType(&type);
	HTT_ASSERT(SUCCEEDED(hr));
	type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
	hr = reader->SetCurrentMediaType(kAudioStream, nullptr, type.Get());
	HTT_ASSERT(SUCCEEDED(hr));
	LogManager::Log("メディアタイプをPCMに設定完了");
	// 波形フォーマット取得
	Microsoft::WRL::ComPtr<IMFMediaType> outputType;
	hr = reader->GetCurrentMediaType(kAudioStream, &outputType);
	HTT_ASSERT(SUCCEEDED(hr));
	WAVEFORMATEX* rawWaveFormat = nullptr;
	UINT32 waveFormatSize = 0;
	hr = MFCreateWaveFormatExFromMFMediaType(outputType.Get(), &rawWaveFormat, &waveFormatSize);
	HTT_ASSERT(SUCCEEDED(hr));
	// 受け取ったらすぐ unique_ptr に渡す（CoTaskMemFree を書き忘れても、関数を抜けると返される）
	const std::unique_ptr<WAVEFORMATEX, CoTaskMemDeleter> waveFormat(rawWaveFormat);
	LogManager::Log(std::format("波形フォーマット取得完了 channels={} sampleRate={} bitsPerSample={}",
	                waveFormat->nChannels, waveFormat->nSamplesPerSec, waveFormat->wBitsPerSample));
	return *waveFormat;
}

/// <summary>
/// PCMデータを最後まで読み出す
/// </summary>
std::vector<BYTE> ReadAllSamples(IMFSourceReader* reader) {
	std::vector<BYTE> audioData;
	while (true) {
		// ループの中で作った ComPtr は、1周終わるたびに Release される
		Microsoft::WRL::ComPtr<IMFSample> sample;
		DWORD dwFlags = 0;
		HRESULT hr = reader->ReadSample(kAudioStream, 0, nullptr, &dwFlags, nullptr, &sample);
		HTT_ASSERT(SUCCEEDED(hr));
		if (dwFlags & MF_SOURCE_READERF_ENDOFSTREAM) {
			break;
		}
		if (!sample) {
			continue;
		}
		Microsoft::WRL::ComPtr<IMFMediaBuffer> mediaBuffer;
		hr = sample->ConvertToContiguousBuffer(&mediaBuffer);
		HTT_ASSERT(SUCCEEDED(hr));
		BYTE* pAudioBytes = nullptr;
		DWORD cbBuffer = 0;
		hr = mediaBuffer->Lock(&pAudioBytes, nullptr, &cbBuffer);
		HTT_ASSERT(SUCCEEDED(hr));
		audioData.insert(audioData.end(), pAudioBytes, pAudioBytes + cbBuffer);
		mediaBuffer->Unlock(); // Lock と対になる物なので、これは残す（Release ではない）
	}
	LogManager::Log(std::format("PCMデータ読み出し完了 totalBytes={}", audioData.size()));
	return audioData;
}
} // namespace


//=============================================================================
// 初期化・終了
//=============================================================================
// ===== 初期化 =====
void SoundDecoder::Initialize() {
	// Media Foundation初期化（MP3等のデコードに使う）
	const HRESULT hr = MFStartup(MF_VERSION);
	HTT_ASSERT_MSG(SUCCEEDED(hr), "MFStartup失敗");
}

// ===== 終了 =====
void SoundDecoder::Finalize() {
	MFShutdown();
}

//=============================================================================
// 読み込み
//=============================================================================
// ===== 音声ファイルを読む =====
std::optional<SoundDecoder::DecodedSound> SoundDecoder::Decode(const std::filesystem::path& filePath) {
	const std::string filename = ToPathKey(filePath);
	// SourceReaderでファイルを開く
	// Media Foundation の物は COM なので ComPtr で持つ。関数を抜けると（途中の return でも）自動で Release される
	Microsoft::WRL::ComPtr<IMFSourceReader> reader;
	const HRESULT hr = MFCreateSourceReaderFromURL(filePath.c_str(), nullptr, &reader);
	if (FAILED(hr)) {
		LogManager::Error(std::format("ファイルの読み込みに失敗した filename=\"{}\" hr=0x{:08X}", filename, static_cast<uint32_t>(hr)));
		return std::nullopt;
	}
	LogManager::Log(std::format("SourceReader 作成完了 filename=\"{}\"", filename));

	DecodedSound sound;
	sound.format = SetupPcmFormat(reader.Get());
	sound.pcm = ReadAllSamples(reader.Get());
	return sound;
}
