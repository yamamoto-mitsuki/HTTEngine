#pragma once
#include <cstdint>
#include <string_view>

#include "MyEngine/Core/Handle.h"
#include "MyEngine/ECS/Entity.h"
#include "MyEngine/Math/Vector4.h"
#include "MyEngine/Time/EngineTime.h"
#include "MyEngine/Timeline/TimelineEvent.h"
#include "MyEngine/Timeline/TimelinePlayhead.h"

/// <summary>
/// 再生の層。同じ Entity の同じ層で再生できるのは1つだけ（もう一度頼むと置き換わる）
/// <para>値は並びの順（Component → Script → … → UIScreen）に書くので、同じ値を動かすと後の層が勝つ。同じ層の中では、後から再生を始めた方が勝つ</para>
/// </summary>
enum class TimelineLayer {
	Component,    // Timeline Component の再生（Play・Pause・Stop・Seek）
	Script,       // ゲームのコードから PlayLayer で再生した物
	Trigger,      // Timeline Trigger（範囲に入った・出た）
	UIState,      // UI State の Normal・Hover・Pressed・Selected・Disabled
	UIVisibility, // UI State の Show・Hide
	UIScreen,     // UI Screen の Show・Hide
};

/// <summary>
/// PlayLayer の再生の設定
/// </summary>
struct TimelinePlaySettings {
	uint32_t timeline = 0;              // 再生するファイル（AssetType::Timeline の番号）
	bool loop = false;                  // 最後まで行ったら最初へ戻る
	float speed = 1.0f;                 // 再生速度（0 以下は進まない）
	TimeMode timeMode = TimeMode::Game; // 使う時間（Real：ポーズ中も動く）
	bool jumpToEnd = false;             // 動かさずに、最初のフレームで最後の値を書く（最初の形をすぐ当てる）
};

/// <summary>
/// Entity の今の再生の様子（TimelineSystem::GetStatus）。読むたびに作る写しなので、値は古くなる
/// </summary>
struct TimelineStatus {
	bool isValid = false;                                 // 再生する物があり、ファイルが読めている
	uint32_t timeline = 0;                                // 再生しているファイル
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
/// <para>Timeline のファイルは全部の Entity で共有する。今の時間・対象の Entity はここが Entity と層ごとに持つ（最初に使うときに作り、エディタの Stop で消える）</para>
/// <para>順番：全部の Entity の値を層の順に書く → 全部のイベントを呼ぶ（イベントの中で Stop・Destroy をしても、回している途中の物が壊れない）</para>
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

	// ===== Timeline Component の再生を指示する（ゲーム・カメラ）。どれも Timeline Component が無い・ファイルが無い・エディタの停止中は false =====
	// 再生する。止まっていれば最初から、一時停止中なら続きから
	static bool Play(Handle<Entity> entity);
	// 一時停止する
	static bool Pause(Handle<Entity> entity);
	// 止める。時間を 0 に戻し、次の Update で 0 の値を書く
	static bool Stop(Handle<Entity> entity);
	// time 秒へ移動する。次の Update でその時間の値を書く。イベントは呼ばない
	static bool Seek(Handle<Entity> entity, float time);

	// ===== Component を使わずに再生する（UI State・UI Screen・ゲームのコード）=====
	/// <summary>
	/// owner の layer で、settings.timeline を最初から再生する（前の再生は置き換わる）。最後まで行ったら、最後の値のまま止まる（Loop でなければ）
	/// <para>トラックの対象は owner からのパス。値を書くのは時間が動いたフレームだけ（止まった後は、ほかの層・ゲームが書いた値を上書きしない）</para>
	/// </summary>
	/// <returns>layer が Component・ファイルが無い・owner が無いなら false</returns>
	static bool PlayLayer(Handle<Entity> owner, TimelineLayer layer, const TimelinePlaySettings& settings);
	// owner の layer の再生をやめる（値は今のまま。書くのをやめるだけ）
	static void StopLayer(Handle<Entity> owner, TimelineLayer layer);

	// ===== 外から読む =====
	static TimelineStatus GetStatus(Handle<Entity> entity, TimelineLayer layer = TimelineLayer::Component);

	// ===== 実行中に、値・対象を変える（owner の全部の層の再生に効く。owner が消える・エディタの停止で忘れる）=====
	// Tween の Parameter に渡す値。再生中に変えれば、次のフレームから効く（1つの値の項目は x を使う）
	static void SetParameter(Handle<Entity> owner, std::string_view name, const Vector4& value);
	// 1つの値（全部の成分に同じ値を入れる）
	static void SetParameter(Handle<Entity> owner, std::string_view name, float value);
	/// <summary>
	/// 対象が path のトラックを、target を動かすようにする（無効な Handle を渡すと元へ戻す）
	/// <para>path はトラックの Target に書いてある文字のまま。owner の Timeline のトラックだけに効く（Preset の中のトラックには効かない）</para>
	/// </summary>
	static void SetTargetOverride(Handle<Entity> owner, std::string_view path, Handle<Entity> target);
	// owner に渡した値・差し替えた対象を全部忘れる
	static void ClearOverrides(Handle<Entity> owner);
	// Tween の Add・Multiply の元の値を忘れる（次に動かしたときの値が、新しい元の値になる。ゲームで置き直した要素に使う）
	static void ResetBaseValues(Handle<Entity> entity);

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