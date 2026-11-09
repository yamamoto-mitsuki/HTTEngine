#pragma once
#include <memory>

/// <summary>
/// 動くのに使う時間の種類（Component の Time Mode の項目）
/// </summary>
enum class TimeMode {
	Game, // ゲームの時間（スローで遅くなり、ヒットストップ・ポーズで止まる）
	Real, // 止まらない時間（スロー・ヒットストップ・ポーズの間も同じ速さで動く。エディタの一時停止では止まる）
};


// 時間計測クラス
// 経過時間には「一時停止（エンジン）」「ゲームのスロー（ゲーム）」「ヒットストップ（ゲーム）」の3つが掛かる
// 一時停止とゲームのスローを別々に持つのは、SceneManager が毎フレーム一時停止の値を書き直しても、ゲームのスローが消えないようにするため
class Time {
public:
	// ===== Passkey Idiom =====
	class ConstructorKey {
	private:
		ConstructorKey() = default;
		friend class Time;
	};
	explicit Time(ConstructorKey) {} // 鍵を受け取るコンストラクタ

	// 1フレームの経過時間の上限（秒）。重い処理やブレークポイントで止めて再開したとき、巨大な deltaTime で物が飛んでいかないように
	// Engine::BeginFrame がこの値で止める。PhysicsSystem の「1フレームを何歩に分けるか」の上限も、この値から決める
	static constexpr float kMaxDeltaTime = 0.1f;

	// 初期化・解放
	static void Initialize() { instance_ = std::make_unique<Time>(ConstructorKey()); }
	static void Release() {
		instance_.reset();
	}

	// ===== 経過時間 =====
	// 前フレームからの秒（一時停止・ゲームのスロー・ヒットストップを掛けた物）。ゲームの動きは全部これで
	static float GetDeltaTime() { return instance_->deltaTime_ * GetTimeScale(); }
	// 種類を選んで読む
	static float GetDeltaTime(TimeMode mode) { return (mode == TimeMode::Real) ? GetRealDeltaTime() : GetDeltaTime(); }
	// 何も掛けていない秒（UIの演出など、スロー・一時停止で止めたくない物に）
	static float GetUnscaleDeltaTime() { return instance_->deltaTime_; }
	// 止めない時間の秒（スロー・ヒットストップ・ポーズは掛からない。エディタの一時停止のときだけ0）。カメラのように、ゲームの時間が止まっても動かしたい物に
	static float GetRealDeltaTime() { return instance_->deltaTime_ * instance_->pauseScale_; }
	// 今掛かっている倍率（一時停止 × ゲームのスロー。ヒットストップ中・ポーズ中は0）
	static float GetTimeScale() {
		if (instance_->hitStopRemaining_ > 0.0f || instance_->isGamePaused_) {
			return 0.0f;
		}
		return instance_->pauseScale_ * instance_->gameTimeScale_;
	}

	// ===== ゲームから使う =====
	// ゲームのスロー（1で普通、0.1で10分の1の速さ）。Play / Stop / Restart・シーンの切り替えで1に戻る
	static void SetGameTimeScale(float scale) { instance_->gameTimeScale_ = (scale < 0.0f) ? 0.0f : scale; }
	static float GetGameTimeScale() { return instance_->gameTimeScale_; }
	// ヒットストップ（seconds 秒だけ時間を止める）。止まっている間にもう一度呼んだら、残りの長い方
	// 秒は何も掛けていない時間で数える（スロー中でも同じ長さだけ止まる）。一時停止中は減らない
	static void HitStop(float seconds) {
		if (seconds > instance_->hitStopRemaining_) {
			instance_->hitStopRemaining_ = seconds;
		}
	}
	static bool IsHitStopping() { return instance_->hitStopRemaining_ > 0.0f; }
	// ポーズ（ゲームのポーズ画面を開いている間）。止めている間は時間が0になり、Script の Update も呼ばれない
	// ポーズ画面（G9d の Screen）・幕（SceneTransition）は止めない時間（GetUnscaleDeltaTime）で動く。Play / Stop / Restart・シーンの切り替えで外れる
	static void SetGamePaused(bool isPaused) { instance_->isGamePaused_ = isPaused; }
	static bool IsGamePaused() { return instance_->isGamePaused_; }

	// ===== エンジンから使う =====
	// フレームの頭に1回（Engine::BeginFrame）。ヒットストップの残りもここで減らす（一時停止・ポーズ中は減らさない）
	static void SetDeltaTime(float time) {
		instance_->deltaTime_ = time;
		if (instance_->pauseScale_ > 0.0f && !instance_->isGamePaused_ && instance_->hitStopRemaining_ > 0.0f) {
			instance_->hitStopRemaining_ -= time;
			if (instance_->hitStopRemaining_ < 0.0f) {
				instance_->hitStopRemaining_ = 0.0f;
			}
		}
	}
	// 一時停止（SceneManager が毎フレーム書く。一時停止中0、それ以外1）
	static void SetPauseScale(float scale) { instance_->pauseScale_ = scale; }
	// ゲームのスローとヒットストップ・ポーズを戻す（SceneManager がシーンを作り直すときに呼ぶ）
	static void ResetGameTime() {
		instance_->gameTimeScale_ = 1.0f;
		instance_->hitStopRemaining_ = 0.0f;
		instance_->isGamePaused_ = false;
	}

private:
	// シングルトン
	friend struct std::default_delete<Time>;
	~Time() = default;
	Time(const Time&) = delete;
	Time& operator=(const Time&) = delete;
	static inline std::unique_ptr<Time> instance_ = nullptr;
	float deltaTime_ = 0.0f;        // 前フレームからの秒（何も掛けていない）
	float pauseScale_ = 1.0f;       // 一時停止（エンジンが書く）
	float gameTimeScale_ = 1.0f;    // ゲームのスロー（ゲームが書く）
	float hitStopRemaining_ = 0.0f; // ヒットストップの残りの秒
	bool isGamePaused_ = false;     // ポーズ中か（ゲームのポーズ画面が書く）
};