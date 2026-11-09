#include "HTTEngine/Sound/SoundManager.h"

#include <algorithm>
#include <format>
#include <optional>
#include <vector>

#include <xaudio2fx.h>

#include <externals/magic_enum/magic_enum.hpp>

#include "HTTEngine/Core/AssetMoveRegistry.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/String/ConvertString.h"

#pragma comment(lib, "xaudio2.lib")

// staticメンバの定義
std::unique_ptr<SoundManager> SoundManager::instance_ = nullptr;

namespace {
constexpr uint32_t kBusChannels = 2; // 種類ごとの通り道は2ch（左右）。左右の振り分けは、いつもこの2chに対して書けばよい

// 音の出口（左右の2ch）への割り振りを書く。gain は全体に掛ける大きさ、pan は -1=左 〜 1=右
// 中央（pan=0）は左右とも gain のまま＝何もしていない音と同じ大きさ。右へ振るほど左だけ小さくする
void ApplyOutputMatrix(IXAudio2SourceVoice* voice, float gain, float pan) {
	const float left = gain * std::clamp(1.0f - pan, 0.0f, 1.0f);
	const float right = gain * std::clamp(1.0f + pan, 0.0f, 1.0f);
	XAUDIO2_VOICE_DETAILS details;
	voice->GetVoiceDetails(&details);
	const uint32_t inputChannels = details.InputChannels;
	// matrix[入力ch数 × 出口ch + 入力ch] が「入力chから出口chへ送る大きさ」
	std::vector<float> matrix(inputChannels * kBusChannels, 0.0f);
	if (inputChannels == 1) {
		// モノラル：1つの音を左右の両方へ
		matrix[0] = left;
		matrix[1] = right;
	} else {
		// ステレオ以上：偶数chを左、奇数chを右へ
		for (uint32_t channel = 0; channel < inputChannels; ++channel) {
			const uint32_t output = channel % 2;
			matrix[inputChannels * output + channel] = (output == 0) ? left : right;
		}
	}
	// 送り先は種類ごとの通り道1つだけなので、nullptr（＝その1つ）でよい
	voice->SetOutputMatrix(nullptr, inputChannels, kBusChannels, matrix.data());
}

// フィルタを掛ける。frequency は 0.0f〜1.0f（XAUDIO2_MAX_FILTER_FREQUENCY に対する割合）
void ApplyFilter(IXAudio2SourceVoice* voice, XAUDIO2_FILTER_TYPE type, float frequency) {
	XAUDIO2_FILTER_PARAMETERS filter{};
	filter.Type = type;
	filter.Frequency = frequency * XAUDIO2_MAX_FILTER_FREQUENCY;
	filter.OneOverQ = 1.0f;
	const HRESULT hr = voice->SetFilterParameters(&filter);
	if (FAILED(hr)) {
		LogManager::Error(std::format("フィルタの設定に失敗 hr=0x{:08X}", static_cast<uint32_t>(hr)));
	}
}

// リバーブの設定。mix は 0.0f〜1.0f（0%〜100% に変換する）
XAUDIO2FX_REVERB_PARAMETERS MakeReverbParameters(float mix) {
	XAUDIO2FX_REVERB_PARAMETERS params{};
	params.ReflectionsDelay = 50;
	params.ReverbDelay = 60;
	params.RearDelay = 10;
	params.PositionLeft = 6;
	params.PositionRight = 6;
	params.PositionMatrixLeft = 0;
	params.PositionMatrixRight = 0;
	params.EarlyDiffusion = 15;
	params.LateDiffusion = 15;
	params.LowEQGain = 8;
	params.LowEQCutoff = 4;
	params.HighEQGain = 12;
	params.HighEQCutoff = 16;
	params.RoomFilterFreq = 5000.0f;
	params.RoomFilterMain = -1000.0f;
	params.RoomFilterHF = -100.0f;
	params.ReflectionsGain = 0.0f;
	params.ReverbGain = 0.0f;
	params.DecayTime = 5.0f;
	params.Density = 60.0f;
	params.RoomSize = 80.0f;
	params.WetDryMix = mix * 100.0f;
	return params;
}
} // namespace


//=============================================================================
// 内部の共通処理
//=============================================================================
// ===== 実体 =====
SoundManager& SoundManager::Instance() {
	HTT_ASSERT_MSG(instance_, "Initialized()を先に呼んでください");
	return *instance_;
}

// ===== 再生ハンドルから voice を探す =====
IXAudio2SourceVoice* SoundManager::FindVoice(uint32_t playHandle) const {
	if (!playHandle) {
		LogManager::Warning("無効なハンドル(0)のため終了");
		return nullptr;
	}
	const auto it = voices_.find(playHandle);
	if (it == voices_.end()) {
		LogManager::Warning(std::format("playHandle={} はvoices_に存在しない", playHandle));
		return nullptr;
	}
	if (!it->second) {
		LogManager::Warning(std::format("playHandle={} のvoiceがnull", playHandle));
		return nullptr;
	}
	return it->second;
}

// ===== 全部の voice を破棄 =====
void SoundManager::DestroyAllVoices() {
	for (auto& [playHandle, voice] : voices_) {
		if (voice) {
			voice->Stop();
			voice->DestroyVoice();
		}
	}
	voices_.clear();
	fadeInfos_.clear();
}


//=============================================================================
// 公開関数（NVIパターン）
//=============================================================================
// ===== 初期化・解放 =====
void SoundManager::Initialize() {
	HTT_ASSERT_MSG(instance_ == nullptr, "Initialize()を2回呼んでいます");
	instance_ = std::make_unique<SoundManager>(ConstructorKey());
	instance_->InitInternal();
	AssetMoveRegistry::AddListener(&OnAssetMoved);
}

// ===== 名前を変えた・動かしたファイル =====
void SoundManager::OnAssetMoved(const MovedPath& moved) {
	if (instance_ != nullptr) {
		moved.ApplyToKeys(instance_->pathID_);
	}
}

void SoundManager::Release() {
	HTT_ASSERT_MSG(instance_ != nullptr, "Initialize()より先にRelease()が呼ばれています");
	instance_->ReleaseInternal();
	instance_.reset();
}

// ===== 読み込み・再生 =====
uint32_t SoundManager::Load(const std::filesystem::path& filePath) { return Instance().LoadInternal(filePath); }

uint32_t SoundManager::Play(uint32_t soundHandle, bool loop, float volume, float pitch, AudioBus bus) { return Instance().PlayInternal(soundHandle, loop, volume, pitch, bus); }
uint32_t SoundManager::Play(uint32_t soundHandle, const SoundConfig& config) { return Play(soundHandle, config.loop, config.volume, config.pitch, config.bus); }

// ===== 音量・ピッチ・パン =====
void SoundManager::SetVolume(uint32_t playHandle, float volume) { Instance().SetVolumeInternal(playHandle, volume); }
void SoundManager::SetVolume(uint32_t playHandle, const SoundConfig& config) { SetVolume(playHandle, config.volume); }

void SoundManager::SetPitch(uint32_t playHandle, float pitch) { Instance().SetPitchInternal(playHandle, pitch); }
void SoundManager::SetPitch(uint32_t playHandle, const SoundConfig& config) { SetPitch(playHandle, config.pitch); }

void SoundManager::SetPan(uint32_t playHandle, float pan) { Instance().SetPanInternal(playHandle, pan); }
void SoundManager::SetPan(uint32_t playHandle, const SoundConfig& config) { SetPan(playHandle, config.pan); }

// ===== エフェクト =====
void SoundManager::SetReverb(uint32_t playHandle, float mix) { Instance().SetReverbInternal(playHandle, mix); }
void SoundManager::SetReverb(uint32_t playHandle, const SoundConfig& config) { SetReverb(playHandle, config.reverbMix); }

void SoundManager::SetLowPassFilter(uint32_t playHandle, float cutoff) { Instance().SetLowPassFilterInternal(playHandle, cutoff); }
void SoundManager::SetLowPassFilter(uint32_t playHandle, const SoundConfig& config) { SetLowPassFilter(playHandle, config.lpfCutoff); }

void SoundManager::SetHighPassFilter(uint32_t playHandle, float cutoff) { Instance().SetHighPassFilterInternal(playHandle, cutoff); }
void SoundManager::SetHighPassFilter(uint32_t playHandle, const SoundConfig& config) { SetHighPassFilter(playHandle, config.hpfCutoff); }

// ===== フェード =====
void SoundManager::FadeOut(uint32_t playHandle, float duration, float targetVolume, Ease::Type easeType) { Instance().FadeOutInternal(playHandle, duration, targetVolume, easeType); }
void SoundManager::FadeOut(uint32_t playHandle, const SoundConfig& config) { FadeOut(playHandle, config.fadeDuration, config.fadeTargetVolume, config.fadeEaseType); }

void SoundManager::FadeIn(uint32_t playHandle, float duration, float targetVolume, Ease::Type easeType) { Instance().FadeInInternal(playHandle, duration, targetVolume, easeType); }
void SoundManager::FadeIn(uint32_t playHandle, const SoundConfig& config) { FadeIn(playHandle, config.fadeDuration, config.fadeTargetVolume, config.fadeEaseType); }


//=============================================================================
// 再生の操作
//=============================================================================
// ===== 止めて破棄 =====
void SoundManager::Stop(uint32_t playHandle) {
	SoundManager& self = Instance();
	IXAudio2SourceVoice* voice = self.FindVoice(playHandle);
	if (!voice) {
		return;
	}
	voice->Stop();
	voice->DestroyVoice();
	self.voices_.erase(playHandle);
	LogManager::Log(std::format("playHandle={} を停止・破棄した", playHandle));
}

// ===== 一時停止 =====
void SoundManager::Pause(uint32_t playHandle) {
	IXAudio2SourceVoice* voice = Instance().FindVoice(playHandle);
	if (!voice) {
		return;
	}
	voice->Stop();
	LogManager::Log(std::format("playHandle={} を一時停止した", playHandle));
}

// ===== 一時停止を再開 =====
void SoundManager::Resume(uint32_t playHandle) {
	IXAudio2SourceVoice* voice = Instance().FindVoice(playHandle);
	if (!voice) {
		return;
	}
	voice->Start();
	LogManager::Log(std::format("playHandle={} を再開した", playHandle));
}

// ===== 再生終了した音を破棄 =====
void SoundManager::CleanupSourceVoices(float deltaTime) {
	// 毎フレーム処理のためログなし
	SoundManager& self = Instance();
	// 一時停止中はフェードも止める（進めると、再開したときに音量が飛ぶ）
	if (!self.paused_) {
		self.UpdateFadeInternal(deltaTime);
	}

	for (auto it = self.voices_.begin(); it != self.voices_.end();) {
		const uint32_t playHandle = it->first;
		IXAudio2SourceVoice* voice = it->second;
		// フェード中のボイスは掃除の対象から外す
		if (self.fadeInfos_.contains(playHandle)) {
			++it;
			continue;
		}
		XAUDIO2_VOICE_STATE state;
		voice->GetState(&state);
		// バッファが空（再生終了）になったら破棄
		if (state.BuffersQueued == 0) {
			voice->Stop();
			voice->DestroyVoice();
			it = self.voices_.erase(it);
		} else {
			++it;
		}
	}
}

// ===== 全部止める =====
void SoundManager::StopAll() {
	SoundManager& self = Instance();
	LogManager::Log(std::format("鳴っている音を全部止める 件数={}", self.voices_.size()));
	self.DestroyAllVoices();
}

// ===== 全部を一時停止 / 再開 =====
void SoundManager::SetPaused(bool paused) {
	SoundManager& self = Instance();
	if (self.paused_ == paused) {
		return; // 同じ状態なら何もしない
	}
	self.paused_ = paused;
	// XAudio2 の Stop は「今の位置で止める」だけで、Start でそこから続きが鳴る（頭に戻るのは DestroyVoice して作り直したとき）
	for (auto& [playHandle, voice] : self.voices_) {
		if (!voice) {
			continue;
		}
		if (paused) {
			voice->Stop();
		} else {
			voice->Start();
		}
	}
	LogManager::Log(std::format("{} 件数={}", paused ? "全部の音を一時停止した" : "全部の音を再開した", self.voices_.size()));
}

bool SoundManager::IsPaused() { return Instance().paused_; }

// ===== まだ鳴っているか =====
bool SoundManager::IsPlaying(uint32_t playHandle) {
	// 毎フレーム呼ばれるのでログなし
	// 鳴り終わった音は CleanupSourceVoices が一覧から消すので、一覧にあればまだある
	return playHandle != 0 && Instance().voices_.contains(playHandle);
}


//=============================================================================
// 音の情報・音量
//=============================================================================
// ===== 素材ハンドル → パス =====
const std::string& SoundManager::GetSoundPath(uint32_t soundHandle) {
	const SoundManager& self = Instance();
	static const std::string kEmpty; // 見つからないときに返す空文字（参照で返すので static にする）
	// 「パス → 番号」の表を逆から引く（読み込んだ音の数しか無いので、順に見れば足りる）
	const auto it = std::ranges::find_if(self.pathID_, [soundHandle](const auto& entry) { return entry.second == soundHandle; });
	return (it != self.pathID_.end()) ? it->first : kEmpty;
}

// ===== 3Dの音（距離の大きさと左右） =====
void SoundManager::SetSpatial(uint32_t playHandle, float gain, float pan) {
	// 毎フレーム呼ばれるのでログなし。鳴り終わった音には何もしない（FindVoice は警告を出すので使わない）
	const SoundManager& self = Instance();
	const auto it = self.voices_.find(playHandle);
	if (it == self.voices_.end() || it->second == nullptr) {
		return;
	}
	ApplyOutputMatrix(it->second, gain, pan);
}

// ===== 音の種類ごとの音量 =====
void SoundManager::SetBusVolume(AudioBus bus, float volume) {
	LogManager::Log(std::format("{} の音量を{:.3f}にした", magic_enum::enum_name(bus), volume));
	Instance().busVoices_[static_cast<size_t>(bus)]->SetVolume(volume);
}

float SoundManager::GetBusVolume(AudioBus bus) {
	float volume = 0.0f;
	Instance().busVoices_[static_cast<size_t>(bus)]->GetVolume(&volume);
	return volume;
}

void SoundManager::SetMasterVolume(float volume) {
	LogManager::Log(std::format("全体の音量を{:.3f}にした", volume));
	Instance().masterVoice_->SetVolume(volume);
}

float SoundManager::GetMasterVolume() {
	float volume = 0.0f;
	Instance().masterVoice_->GetVolume(&volume);
	return volume;
}


//=============================================================================
// 初期化・解放
//=============================================================================
// ===== 初期化 =====
void SoundManager::InitInternal() {
	SoundDecoder::Initialize();
	HRESULT hr;
	// XAudio2エンジン作成
	hr = XAudio2Create(&xAudio2_, 0, XAUDIO2_DEFAULT_PROCESSOR);
	HTT_ASSERT_MSG(SUCCEEDED(hr), "XAudio2Create失敗");
	// マスターボイス作成（最終的な音声出力先）
	hr = xAudio2_->CreateMasteringVoice(&masterVoice_);
	HTT_ASSERT_MSG(SUCCEEDED(hr), "CreateMasteringVoice失敗");
	// 音の種類ごとの通り道（左右2ch・スピーカーと同じサンプリングレート）。鳴らす音はどれか1つを通ってマスターへ行く
	XAUDIO2_VOICE_DETAILS masterDetails;
	masterVoice_->GetVoiceDetails(&masterDetails);
	for (IXAudio2SubmixVoice*& busVoice : busVoices_) {
		hr = xAudio2_->CreateSubmixVoice(&busVoice, kBusChannels, masterDetails.InputSampleRate);
		HTT_ASSERT_MSG(SUCCEEDED(hr), "CreateSubmixVoice失敗");
	}
	LogManager::Log("Initialized");
}

// ===== 解放 =====
void SoundManager::ReleaseInternal() {
	// 再生中の全ボイスを停止・破棄
	LogManager::Log(std::format("再生中のvoiceを破棄 件数={}", voices_.size()));
	DestroyAllVoices();
	// 読み込み済みの全波形データを解放（DecodedSound の vector が、clear で一緒に消える）
	LogManager::Log(std::format("波形データを解放 件数={}", soundMap_.size()));
	soundMap_.clear();
	pathID_.clear();
	// 種類ごとの通り道を破棄（流れ込む音を全部消した後、流れ出る先のマスターより先に）
	for (IXAudio2SubmixVoice*& busVoice : busVoices_) {
		if (busVoice) {
			busVoice->DestroyVoice();
			busVoice = nullptr;
		}
	}
	// マスターボイスを破棄
	if (masterVoice_) {
		masterVoice_->DestroyVoice();
		masterVoice_ = nullptr;
		LogManager::Log("MasteringVoice を破棄した");
	}
	xAudio2_.Reset();
	SoundDecoder::Finalize();
	LogManager::Log("Released");
}


//=============================================================================
// 読み込み・再生
//=============================================================================
// ===== 読み込み =====
uint32_t SoundManager::LoadInternal(const std::filesystem::path& filePath) {
	const std::string filename = ToPathKey(filePath);
	LogManager::Log(std::format("ファイル読み込み要求 filename=\"{}\"", filename));
	// キャッシュチェック
	if (const auto cached = pathID_.find(filename); cached != pathID_.end()) {
		// 同じ音を何個ものAudioSourceが使うと毎回ここに来る。ふつうのことなので警告にはしない
		LogManager::Log(std::format("キャッシュ済み filename=\"{}\" -> soundHandle={}", filename, cached->second));
		return cached->second;
	}
	// 読めなければ止めずに 0（未選択）を返す。Inspector で読めないファイルを選んでも、エディタが落ちないように
	// コードで Load して 0 が返った音を Play すると、Play の assert で止まるので気づける
	std::optional<SoundDecoder::DecodedSound> decoded = SoundDecoder::Decode(filePath);
	if (!decoded) {
		return 0;
	}
	// 登録（読んだ vector をそのまま渡す。コピーも new も要らない）
	const size_t bufferSize = decoded->pcm.size();
	const uint32_t newID = static_cast<uint32_t>(soundMap_.size() + 1);
	pathID_[filename] = newID;
	soundMap_[newID] = std::move(*decoded);
	LogManager::Log(std::format("登録完了 filename=\"{}\" -> soundHandle={} bufferSize={}", filename, newID, bufferSize));
	return newID;
}

// ===== 再生 =====
uint32_t SoundManager::PlayInternal(uint32_t soundHandle, bool loop, float volume, float pitch, AudioBus bus) {
	LogManager::Log(std::format("soundHandle={} loop={} volume={:.3f} pitch={:.3f} bus={}", soundHandle, loop, volume, pitch, magic_enum::enum_name(bus)));
	const auto sound = soundMap_.find(soundHandle);
	if (soundHandle == 0 || sound == soundMap_.end()) {
		LogManager::Error(std::format("未登録のsoundHandle={} が指定された", soundHandle));
		HTT_ASSERT_MSG(false, "登録されていないハンドルが指定されました");
		return 0;
	}
	const SoundDecoder::DecodedSound& data = sound->second;
	HRESULT hr;
	// SourceVoice作成。送り先は音の種類の通り道（マスターへ直接は送らない）
	// XAUDIO2_VOICE_USEFILTER が無いと SetFilterParameters が失敗する（付けただけでは音は変わらない）
	XAUDIO2_SEND_DESCRIPTOR send = {0, busVoices_[static_cast<size_t>(bus)]};
	const XAUDIO2_VOICE_SENDS sends = {1, &send};
	IXAudio2SourceVoice* pSourceVoice = nullptr;
	hr = xAudio2_->CreateSourceVoice(&pSourceVoice, &data.format, XAUDIO2_VOICE_USEFILTER, XAUDIO2_DEFAULT_FREQ_RATIO, nullptr, &sends);
	if (FAILED(hr)) {
		LogManager::Error(std::format("SourceVoiceの作成に失敗 soundHandle={} hr=0x{:08X}", soundHandle, static_cast<uint32_t>(hr)));
		HTT_ASSERT_MSG(false, "SourceVoiceの作成に失敗しました");
		return 0;
	}
	pSourceVoice->SetVolume(volume);
	pSourceVoice->SetFrequencyRatio(pitch);
	// バッファをキューに積む
	XAUDIO2_BUFFER buffer = {};
	buffer.pAudioData = data.pcm.data();
	buffer.AudioBytes = static_cast<UINT32>(data.pcm.size());
	buffer.Flags = XAUDIO2_END_OF_STREAM;
	if (loop) {
		buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
	}
	hr = pSourceVoice->SubmitSourceBuffer(&buffer);
	if (FAILED(hr)) {
		LogManager::Error(std::format("SubmitSourceBuffer失敗 soundHandle={} hr=0x{:08X}", soundHandle, static_cast<uint32_t>(hr)));
		pSourceVoice->DestroyVoice();
		return 0;
	}
	// 一時停止中（SetPaused）は Start しない。バッファは積んであるので、再開したときに頭から鳴る
	if (!paused_) {
		hr = pSourceVoice->Start();
		if (FAILED(hr)) {
			LogManager::Error(std::format("Start失敗 soundHandle={} hr=0x{:08X}", soundHandle, static_cast<uint32_t>(hr)));
			pSourceVoice->DestroyVoice();
			return 0;
		}
	}
	// 再生ハンドルを発行して登録
	const uint32_t playHandle = nextPlayHandle_++;
	if (nextPlayHandle_ == 0) {
		nextPlayHandle_ = 1; // オーバーフロー対策
	}
	voices_[playHandle] = pSourceVoice;
	LogManager::Log(std::format("再生開始 soundHandle={} -> playHandle={}", soundHandle, playHandle));
	return playHandle;
}


//=============================================================================
// 音の調整
//=============================================================================
// ===== 音量調整 =====
void SoundManager::SetVolumeInternal(uint32_t playHandle, float volume) {
	IXAudio2SourceVoice* voice = FindVoice(playHandle);
	if (!voice) {
		return;
	}
	voice->SetVolume(volume);
	LogManager::Log(std::format("playHandle={} の音量を{:.3f}に変更した", playHandle, volume));
}

// ===== ピッチ調整 =====
void SoundManager::SetPitchInternal(uint32_t playHandle, float pitch) {
	IXAudio2SourceVoice* voice = FindVoice(playHandle);
	if (!voice) {
		return;
	}
	voice->SetFrequencyRatio(pitch);
	LogManager::Log(std::format("playHandle={} のピッチを{:.3f}に変更した", playHandle, pitch));
}

// ===== パン調整 =====
void SoundManager::SetPanInternal(uint32_t playHandle, float pan) {
	IXAudio2SourceVoice* voice = FindVoice(playHandle);
	if (!voice) {
		return;
	}
	// 左右への割り振り（3Dの音の SetSpatial と同じ関数。大きさは変えない）
	ApplyOutputMatrix(voice, 1.0f, pan);
	LogManager::Log(std::format("playHandle={} パンを{:.3f}に設定した", playHandle, pan));
}


//=============================================================================
// エフェクト
//=============================================================================
// ===== リバーブ調整 =====
void SoundManager::SetReverbInternal(uint32_t playHandle, float mix) {
	IXAudio2SourceVoice* voice = FindVoice(playHandle);
	if (!voice) {
		return;
	}
	XAUDIO2_VOICE_DETAILS details;
	voice->GetVoiceDetails(&details);
	// リバーブエフェクトを作成してエフェクトチェーンに設定する
	Microsoft::WRL::ComPtr<IUnknown> reverbEffect; // SetEffectChain の中で XAudio2 も持つので、こちらの分は関数を抜けると自動で Release
	HRESULT hr = XAudio2CreateReverb(&reverbEffect);
	if (FAILED(hr)) {
		LogManager::Log(std::format("XAudio2CreateReverb失敗 hr=0x{:08X}", static_cast<uint32_t>(hr)));
		LogManager::Flush();
		return;
	}
	XAUDIO2_EFFECT_DESCRIPTOR descriptor{};
	descriptor.InitialState = TRUE;
	descriptor.OutputChannels = details.InputChannels;
	descriptor.pEffect = reverbEffect.Get();
	XAUDIO2_EFFECT_CHAIN chain{};
	chain.EffectCount = 1;
	chain.pEffectDescriptors = &descriptor;
	hr = voice->SetEffectChain(&chain);
	if (FAILED(hr)) {
		LogManager::Log(std::format("SetEffectChain失敗 hr=0x{:08X}", static_cast<uint32_t>(hr)));
		return;
	}
	// リバーブパラメータを設定する
	const XAUDIO2FX_REVERB_PARAMETERS params = MakeReverbParameters(mix);
	voice->SetEffectParameters(0, &params, sizeof(params));
	LogManager::Log(std::format("playHandle={} リバーブを適用した WetDryMix={:.1f}%", playHandle, params.WetDryMix));
}

// ===== リバーブを解除 =====
void SoundManager::RemoveReverb(uint32_t playHandle) {
	IXAudio2SourceVoice* voice = Instance().FindVoice(playHandle);
	if (!voice) {
		return;
	}
	voice->SetEffectChain(nullptr);
	LogManager::Log(std::format("playHandle={} リバーブを解除した", playHandle));
}

// ===== ローパスフィルタをかける =====
void SoundManager::SetLowPassFilterInternal(uint32_t playHandle, float cutoff) {
	IXAudio2SourceVoice* voice = FindVoice(playHandle);
	if (!voice) {
		return;
	}
	ApplyFilter(voice, LowPassFilter, cutoff);
	LogManager::Log(std::format("playHandle={} ローパスフィルタをcutoff={:.3f}で設定した", playHandle, cutoff));
}

// ===== ハイパスフィルタをかける =====
void SoundManager::SetHighPassFilterInternal(uint32_t playHandle, float cutoff) {
	IXAudio2SourceVoice* voice = FindVoice(playHandle);
	if (!voice) {
		return;
	}
	ApplyFilter(voice, HighPassFilter, cutoff); // ハイパスは 0.0f で全部通り、1.0f で全部消える
	LogManager::Log(std::format("playHandle={} ハイパスフィルタをcutoff={:.3f}で設定した", playHandle, cutoff));
}


//=============================================================================
// フェード
//=============================================================================
// ===== フェードアウト =====
void SoundManager::FadeOutInternal(uint32_t playHandle, float duration, float targetVolume, Ease::Type easeType) {
	IXAudio2SourceVoice* voice = FindVoice(playHandle);
	if (!voice) {
		return;
	}
	// 現在の音量からフェード開始する
	float currentVolume = 0.0f;
	voice->GetVolume(&currentVolume);
	fadeInfos_[playHandle] = {.startVolume = currentVolume, .targetVolume = targetVolume, .timer = 0.0f, .duration = duration, .easeType = easeType};
	LogManager::Log(std::format("playHandle={} フェードアウト開始 startVolume={:.3f} -> targetVolume={:.3f} duration={:.3f}s", playHandle, currentVolume, targetVolume, duration));
}

// ===== フェードイン =====
void SoundManager::FadeInInternal(uint32_t playHandle, float duration, float targetVolume, Ease::Type easeType) {
	IXAudio2SourceVoice* voice = FindVoice(playHandle);
	if (!voice) {
		return;
	}
	// 音量0から開始してtargetVolumeまでフェードインする
	voice->SetVolume(0.0f);
	fadeInfos_[playHandle] = {.startVolume = 0.0f, .targetVolume = targetVolume, .timer = 0.0f, .duration = duration, .easeType = easeType};
	LogManager::Log(std::format("playHandle={} フェードイン開始 0.0 -> targetVolume={:.3f} duration={:.3f}s", playHandle, targetVolume, duration));
}

// ===== フェードの更新 =====
void SoundManager::UpdateFadeInternal(float deltaTime) {
	// 毎フレーム処理のためログなし
	for (auto it = fadeInfos_.begin(); it != fadeInfos_.end();) {
		const uint32_t playHandle = it->first;
		FadeInfo& fadeInfo = it->second;
		// voiceが既に破棄されていたらフェード情報も削除する
		const auto voiceIt = voices_.find(playHandle);
		if (voiceIt == voices_.end()) {
			it = fadeInfos_.erase(it);
			continue;
		}
		IXAudio2SourceVoice* voice = voiceIt->second;
		// 経過時間を進めてイージングを適用した音量を計算する
		fadeInfo.timer += deltaTime;
		const float progress = std::min(fadeInfo.timer / fadeInfo.duration, 1.0f);
		const float easedProgress = Ease::Apply(progress, fadeInfo.easeType);
		const float currentVolume = fadeInfo.startVolume + (fadeInfo.targetVolume - fadeInfo.startVolume) * easedProgress;
		voice->SetVolume(currentVolume);
		if (progress >= 1.0f) {
			// フェード完了。targetVolumeが0以下なら破棄する（Stop だけだとバッファが残り、CleanupSourceVoices が破棄しない）
			if (fadeInfo.targetVolume <= 0.0f) {
				voice->Stop();
				voice->DestroyVoice();
				voices_.erase(voiceIt);
			}
			it = fadeInfos_.erase(it);
		} else {
			++it;
		}
	}
}
