#pragma once
#include "MyEngine/Core/Handle.h"
#include "MyEngine/ECS/Entity.h"
#include "MyEngine/Timeline/TimelineEvent.h"
#include "MyEngine/Timeline/TimelinePlayhead.h"

/// <summary>
/// Entity の今の再生の様子（TimelineSystem::GetStatus）。読むたびに作る写しなので、値は古くなる
/// </summary>
struct TimelineStatus {
	bool isValid = false;                                 // Timeline Component があり、ファイルが読めている
	TimelinePlayState state = TimelinePlayState::Stopped; // 再生の状態
	float time = 0.0f;                                    // 今の時間（秒）
	float duration = 0.0f;                                // 長さ（秒）
	int loopCount = 0;                                    // 最初へ戻った回数

	bool IsPlaying() const { return state == TimelinePlayState::Playing; }
	// 終わりまでの割合（0〜1）
	float GetProgress() const { return (duration > 0.0f) ? time / duration : 0.0f; }
};

/// <summary>
/// Timeline を再生する係
/// <para>Timeline のファイルは全部の Entity で共有する。今の時間・対象の Entity はここが Entity ごとに持つ（最初に使うときに作り、エディタの Stop で消える）</para>
/// <para>順番：全部の Entity の値を書く → 全部のイベントを呼ぶ（イベントの中で Stop・Destroy をしても、回している途中の物が壊れない）</para>
/// </summary>
class TimelineSystem {
public:
	// Timeline Component と、エンジンのトラックの種類を登録する。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// 全部の Timeline を1フレーム進める。Script の後、ワールド行列の計算の前に呼ぶ（動かした位置を、そのフレームの描画に使うため）
	/// <para>CameraSystem::Update より前に呼ぶ（カメラのトラックの頼みを、そのフレームのカメラに使うため）</para>
	/// </summary>
	/// <param name="isGameRunning">再生中か（エディタの停止中は何も動かさない）</param>
	static void Update(bool isGameRunning);

	// ===== 外から指示する（ゲーム・カメラ）。どれも Timeline Component が無い・ファイルが無い・エディタの停止中は false =====
	// 再生する。止まっていれば最初から、一時停止中なら続きから
	static bool Play(Handle<Entity> entity);
	// 一時停止する
	static bool Pause(Handle<Entity> entity);
	// 止める。時間を 0 に戻し、次の Update で 0 の値を書く
	static bool Stop(Handle<Entity> entity);
	// time 秒へ移動する。次の Update でその時間の値を書く。イベントは呼ばない
	static bool Seek(Handle<Entity> entity, float time);

	// ===== 外から読む =====
	static TimelineStatus GetStatus(Handle<Entity> entity);

	// ===== プレビュー（エディタの停止中に、再生せずに値を見る）=====
	/// <summary>
	/// 次の Update で、time 秒の値を仮に書く。見るのは1フレームだけなので、見続ける間は毎フレーム呼ぶ（呼ばなくなれば、それだけで元の値のまま）
	/// <para>値を書くのは Update から描画の要求を積み終わる FinishPreview までの間だけ。フレームの頭の保存・Undo と、後の ImGui のウィンドウは元の値を見る</para>
	/// </summary>
	/// <param name="owner">再生する Entity（トラックの対象はここからのパス）</param>
	/// <param name="timeline">見る Timeline の番号</param>
	/// <param name="time">見る時間（秒）</param>
	static void RequestPreview(Handle<Entity> owner, uint32_t timeline, float time);
	// 仮に書いた値を元へ戻す。描画の要求を全部積んだ後に、1フレームに1回呼ぶ
	static void FinishPreview();

	// ===== イベントを受け取る =====
	// Script は OnTimelineEvent を書く。Script 以外で受け取りたいときに、ここへ関数を登録する（登録した関数は Release まで呼ばれる。同じ関数は1回だけ）
	using EventListener = void (*)(const TimelineEvent& event);
	static void AddEventListener(EventListener listener);

private:
	// Entity が消える直前（EntityManager から呼ばれる）
	static void OnEntityDestroyed(Handle<Entity> entity);
};