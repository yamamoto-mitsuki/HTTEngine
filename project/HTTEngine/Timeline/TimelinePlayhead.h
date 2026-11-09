#pragma once
#include <vector>

/// <summary>
/// 再生の状態
/// </summary>
enum class TimelinePlayState {
	Stopped,  // 止まっている（時間は 0）
	Playing,  // 再生中
	Paused,   // 一時停止中（Play で続きから）
	Finished, // 最後まで再生した（Loop でないとき。最後の値のまま）
};

/// <summary>
/// このフレームに通った時間の幅。イベントはこの幅に入った物を呼ぶ
/// </summary>
struct TimelineRange {
	float start = 0.0f;         // 始まり
	float end = 0.0f;           // 終わり（含む）
	bool includesStart = false; // 始まりちょうどを含むか（再生の最初とループの頭だけ true）
};

/// <summary>
/// 再生している時間の位置を進める係（Timeline の中身も Entity も知らない。時間の計算だけ）
/// <para>Advance が返す幅を全部つなぐと、同じ時間を2回通らず、通っていない時間も無い</para>
/// </summary>
class TimelinePlayhead {
public:
	static constexpr int kMaxLoopsPerFrame = 16; // 1フレームに数える周の上限（とても短い Timeline でも、ここで止まる）

	// 再生する。Stopped・Finished なら最初から、Paused なら続きから
	void Play();
	// 一時停止（再生中のときだけ）
	void Pause();
	// 止めて時間を 0 に戻す
	void Stop();
	// time へ移動する（duration の中に収める）。この時間を通ったことにはしない
	// Playing は再生を続ける。それ以外は Paused になる（Play で続きから）
	void Seek(float time, float duration);

	/// <summary>
	/// 時間を進めて、通った幅を out に入れる（out は最初に空にする）
	/// </summary>
	/// <param name="deltaTime">進める秒（0 以下・NaN なら進めない）</param>
	/// <param name="duration">Timeline の長さ（秒）</param>
	/// <param name="isLoop">最後まで行ったら最初へ戻るか</param>
	void Advance(float deltaTime, float duration, bool isLoop, std::vector<TimelineRange>& out);

	TimelinePlayState GetState() const { return state_; }
	float GetTime() const { return time_; }
	int GetLoopCount() const { return loopCount_; } // 最初へ戻った回数

private:
	TimelinePlayState state_ = TimelinePlayState::Stopped;
	float time_ = 0.0f;         // 今の時間（秒）
	bool includesStart_ = true; // 次に進めるとき、今の時間ちょうどのイベントも呼ぶか
	int loopCount_ = 0;         // 最初へ戻った回数
};