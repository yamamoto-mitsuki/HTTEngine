#pragma once
#include <memory>
#include <array>
#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>

#include <wrl.h>
#include <xaudio2.h>

#include <externals/magic_enum/magic_enum.hpp>

#include "MyEngine/Math/Easing.h"
#include "MyEngine/Sound/AudioBus.h"
#include "MyEngine/Sound/SoundDecoder.h"


/// <summary>
/// 音声の登録、呼び出し、調整するクラス
/// </summary>
class SoundManager {
public:
	// ===== Passkey Idiom =====
	class ConstructorKey {
	private:
		ConstructorKey() = default;
		friend class SoundManager;
	};
	explicit SoundManager(ConstructorKey) {} // 鍵を受け取るコンストラクタ

	/// <summary>
	/// 再生・エフェクトのパラメータをまとめた設定構造体
	/// </summary>
	struct SoundConfig {
		// --- Play ---
		float volume = 1.0f; // 音量。0.0f=無音、1.0f=原音量
		float pitch = 1.0f;  // ピッチ。0.5f=1オクターブ下、1.0f=原音程、2.0f=1オクターブ上
		bool loop = false;   // trueでループ再生
		AudioBus bus = AudioBus::SE; // 音の種類（どの音量つまみを通すか）

		// --- SetPan ---
		float pan = 0.0f; // -1.0f=完全に左、0.0f=中央、1.0f=完全に右

		// --- SetLowPassFilter ---
		float lpfCutoff = 1.0f; // 0.0f=最大カット、1.0f=フィルタなし

		// --- SetHighPassFilter ---
		float hpfCutoff = 0.0f; // 0.0f=フィルタなし、1.0f=最大カット

		// --- SetReverb ---
		float reverbMix = 0.0f; // 0.0f=なし、1.0f=最大

		// --- FadeOut / FadeIn ---
		float fadeDuration = 1.0f;     // フェードにかける時間（秒）
		float fadeTargetVolume = 0.0f; // フェード完了時の音量
		Ease::Type fadeEaseType = Ease::Type::Linear; // フェードのイージング（Ease::Type:: で呼ぶ）
	};

public:
	/// <summary>
	/// 初期化。Engine::Initialize()内で1度だけ呼ぶ
	/// </summary>
	static void Initialize();

	/// <summary>
	/// 終了処理。Engine::Finalize()内で呼ぶ。
	/// 再生中の全ボイスと読み込み済みの全音声データを解放する
	/// </summary>
	static void Release();

	/// <summary>
	/// 音声ファイルを読み込む。wav/mp3両対応。同じパスを渡すとキャッシュから返す
	/// </summary>
	/// <param name="filePath">ファイルパス（例: "resources/bgm.mp3"）。UTF-8 の文字から作るときは ToPath を通す</param>
	/// <returns>読み込んだ音声の素材ハンドル</returns>
	static uint32_t Load(const std::filesystem::path& filePath);

	/// <summary>
	/// 音声を再生する。戻り値の再生ハンドルでStop等の操作が可能
	/// </summary>
	/// <param name="soundHandle">Load()が返した素材ハンドル</param>
	/// <param name="loop">trueでループ再生</param>
	/// <param name="volume">音量 0.0f=無音 1.0f=原音量</param>
	/// <param name="pitch">ピッチ 0.5f=1オクターブ下 1.0f=原音程 2.0f=1オクターブ上</param>
	/// <param name="bus">音の種類。BGM なら AudioBus::BGM を渡す（種類ごとの音量が掛かる）</param>
	/// <returns>再生ハンドル。Stop/Pause等の操作に使う</returns>
	static uint32_t Play(uint32_t soundHandle, bool loop, float volume = 1.0f, float pitch = 1.0f, AudioBus bus = AudioBus::SE);

	/// <summary>
	/// 音声を再生する（SoundConfig版）
	/// </summary>
	static uint32_t Play(uint32_t soundHandle, const SoundConfig& config);

	/// <summary>
	/// 音声を停止してSourceVoiceを破棄する
	/// </summary>
	static void Stop(uint32_t playHandle);

	/// <summary>
	/// 音声を一時停止する。Resume()で再開できる
	/// </summary>
	static void Pause(uint32_t playHandle);

	/// <summary>
	/// 一時停止中の音声を再開する
	/// </summary>
	static void Resume(uint32_t playHandle);

	/// <summary>
	/// 再生中の音量を変更する
	/// </summary>
	static void SetVolume(uint32_t playHandle, float volume);

	/// <summary>
	/// 再生中の音量を変更する（SoundConfig版）。config.volumeを使用する
	/// </summary>
	static void SetVolume(uint32_t playHandle, const SoundConfig& config);

	/// <summary>
	/// 再生中のピッチを変更する
	/// </summary>
	static void SetPitch(uint32_t playHandle, float pitch);

	/// <summary>
	/// 再生中のピッチを変更する（SoundConfig版）。config.pitchを使用する
	/// </summary>
	static void SetPitch(uint32_t playHandle, const SoundConfig& config);

	/// <summary>
	/// 左右のパンニング（音の定位）を設定する。-1.0f=左 0.0f=中央 1.0f=右
	/// <para>中央では左右とも元の大きさ。右へ振るほど左が小さくなる（逆も同じ）</para>
	/// </summary>
	static void SetPan(uint32_t playHandle, float pan);

	/// <summary>
	/// 左右のパンニングを設定する（SoundConfig版）。config.panを使用する
	/// </summary>
	static void SetPan(uint32_t playHandle, const SoundConfig& config);

	/// <summary>
	/// 3Dの音用。距離で決めた大きさ（0〜1）と左右（-1〜1）を掛ける。SetVolume の音量とは別に掛かる
	/// <para>毎フレーム呼んでよい（ログを出さない）。SetPan と同じ所を書き換えるので、後から呼んだ方が効く</para>
	/// </summary>
	static void SetSpatial(uint32_t playHandle, float gain, float pan);

	/// <summary>
	/// リバーブ（残響）エフェクトをかける。0.0f=なし 1.0f=最大
	/// </summary>
	static void SetReverb(uint32_t playHandle, float mix);

	/// <summary>
	/// リバーブをかける（SoundConfig版）。config.reverbMixを使用する
	/// </summary>
	static void SetReverb(uint32_t playHandle, const SoundConfig& config);

	/// <summary>
	/// ローパスフィルタをかける。高音域をカットして篭った音になる。
	/// 壁の向こうの音や水中の表現に使う。0.0f=最大カット 1.0f=フィルタなし
	/// </summary>
	static void SetLowPassFilter(uint32_t playHandle, float cutoff);

	/// <summary>
	/// ローパスフィルタをかける（SoundConfig版）。config.lpfCutoffを使用する
	/// </summary>
	static void SetLowPassFilter(uint32_t playHandle, const SoundConfig& config);

	/// <summary>
	/// ハイパスフィルタをかける。低音域をカットして細い音になる。
	/// 電話越しの声やラジオ音声の表現に使う。0.0f=フィルタなし 1.0f=最大カット
	/// </summary>
	static void SetHighPassFilter(uint32_t playHandle, float cutoff);

	/// <summary>
	/// ハイパスフィルタをかける（SoundConfig版）。config.hpfCutoffを使用する
	/// </summary>
	static void SetHighPassFilter(uint32_t playHandle, const SoundConfig& config);

	/// <summary>
	/// 音量を徐々に下げる。targetVolume=0.0fで完全に無音になる
	/// </summary>
	static void FadeOut(uint32_t playHandle, float duration, float targetVolume, Ease::Type easeType);

	/// <summary>
	/// 音量を徐々に下げる（SoundConfig版）。
	/// config.fadeDuration / fadeTargetVolume / fadeEaseTypeを使用する
	/// </summary>
	static void FadeOut(uint32_t playHandle, const SoundConfig& config);

	/// <summary>
	/// 音量を徐々に上げる。音量0から始めてtargetVolumeまで上げる
	/// </summary>
	static void FadeIn(uint32_t playHandle, float duration, float targetVolume, Ease::Type easeType);

	/// <summary>
	/// 音量を徐々に上げる（SoundConfig版）。
	/// config.fadeDuration / fadeTargetVolume / fadeEaseTypeを使用する
	/// </summary>
	static void FadeIn(uint32_t playHandle, const SoundConfig& config);

	/// <summary>
	/// リバーブエフェクトを解除する
	/// </summary>
	static void RemoveReverb(uint32_t playHandle);

	/// <summary>
	/// 再生終了した音声のSourceVoiceを破棄する。毎フレーム呼ぶこと
	/// </summary>
	/// <param name="deltaTime">前フレームからの経過時間</param>
	static void CleanupSourceVoices(float deltaTime);

	// ===== まとめて扱う（AudioSystem と、エディタの Play / Pause / Stop から使う）=====

	/// <summary>
	/// 鳴っている音を全部止めて破棄する（Play / Stop / Restart でシーンを作り直すとき）
	/// <para>AudioSource の音も、Play() で直接鳴らした音も止まる</para>
	/// </summary>
	static void StopAll();

	/// <summary>
	/// 全部の音を一時停止する / 再開する（エディタの Pause）
	/// <para>一時停止の間に Play() した音は、再開するまで鳴らない</para>
	/// </summary>
	static void SetPaused(bool paused);

	// 一時停止中か
	static bool IsPaused();

	/// <summary>
	/// 再生ハンドルの音がまだあるか（鳴り終わった・止めた・0 なら false。一時停止中は true）
	/// </summary>
	static bool IsPlaying(uint32_t playHandle);

	/// <summary>
	/// 素材ハンドルから、読み込んだときのパスを引く（保存と Inspector の表示用。0・知らない番号なら空）
	/// </summary>
	static const std::string& GetSoundPath(uint32_t soundHandle);

	// ===== 音の種類ごとの音量（0=無音、1=元の大きさ）=====
	// 鳴っている音にもすぐ効く。ふつうは AudioSystem（Parameters の Audio / Volume）から変える
	static void SetBusVolume(AudioBus bus, float volume);
	static float GetBusVolume(AudioBus bus);
	// 全部の音に掛かる音量
	static void SetMasterVolume(float volume);
	static float GetMasterVolume();

private:
	// フェードの状態を管理する構造体
	struct FadeInfo {
		float startVolume;  // フェード開始時の音量
		float targetVolume; // フェード終了時の音量
		float timer;        // フェードの経過時間
		float duration;     // フェードにかける時間
		Ease::Type easeType;
	};

	// Initialize()で作り、Release()で消す（unique_ptr）
	// シングルトン
	friend struct std::default_delete<SoundManager>;
	~SoundManager() = default;
	SoundManager(const SoundManager&) = delete;
	SoundManager& operator=(const SoundManager&) = delete;
	static std::unique_ptr<SoundManager> instance_;

	// Initialize の後の実体（Initialize より先に呼ぶと assert）
	static SoundManager& Instance();

	/// <summary>
	/// 再生ハンドルから voice を探す
	/// </summary>
	/// <returns>見つかった voice。0・一覧に無い・null なら警告を出して nullptr</returns>
	IXAudio2SourceVoice* FindVoice(uint32_t playHandle) const;

	// 再生中の voice を全部止めて破棄し、フェードの状態も消す
	void DestroyAllVoices();

	// 内部処理用の関数
	void InitInternal();
	void ReleaseInternal();
	uint32_t LoadInternal(const std::filesystem::path& filename);
	// 名前を変えた・動かしたファイル：キーのパスを付け替える（番号はそのまま。AssetMoveRegistry から呼ばれる）
	static void OnAssetMoved(const struct MovedPath& moved);
	uint32_t PlayInternal(uint32_t soundHandle, bool loop, float volume, float pitch, AudioBus bus);
	void SetVolumeInternal(uint32_t playHandle, float volume);
	void SetPitchInternal(uint32_t playHandle, float pitch);
	void SetPanInternal(uint32_t playHandle, float pan);
	void SetReverbInternal(uint32_t playHandle, float mix);
	void SetLowPassFilterInternal(uint32_t playHandle, float cutoff);
	void SetHighPassFilterInternal(uint32_t playHandle, float cutoff);
	void FadeOutInternal(uint32_t playHandle, float duration, float targetVolume, Ease::Type easeType);
	void FadeInInternal(uint32_t playHandle, float duration, float targetVolume, Ease::Type easeType);
	void UpdateFadeInternal(float deltaTime);

	Microsoft::WRL::ComPtr<IXAudio2> xAudio2_;
	IXAudio2MasteringVoice* masterVoice_ = nullptr;
	// 音の種類ごとの通り道（SubmixVoice）。鳴らす音 → ここ → masterVoice_ の順に流れ、ここの音量が種類ごとの音量になる
	static constexpr size_t kBusCount = magic_enum::enum_count<AudioBus>(); // AudioBus の数（magic_enum が enum の名前を数える。種類を足しても直さなくてよい）
	std::array<IXAudio2SubmixVoice*, kBusCount> busVoices_ = {};

	// 音声データのパスから素材ハンドルを引く用
	std::unordered_map<std::string, uint32_t> pathID_;
	// 素材ハンドルから波形データを引く用
	std::unordered_map<uint32_t, SoundDecoder::DecodedSound> soundMap_;
	// 再生ハンドルから再生中のvoiceポインタを引く用
	std::unordered_map<uint32_t, IXAudio2SourceVoice*> voices_;
	// 再生ハンドルからフェード状態を引く用
	std::unordered_map<uint32_t, FadeInfo> fadeInfos_;

	// 再生ハンドルの発行カウンタ。0は無効なハンドルとするため1からスタートする
	uint32_t nextPlayHandle_ = 1;
	// SetPaused(true) の間は true（この間に Play した音は、Start せずに待たせる）
	bool paused_ = false;
};