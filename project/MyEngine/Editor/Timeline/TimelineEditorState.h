#pragma once
#include <span>
#include <string>
#include <vector>
#include <cstdint>

#include "MyEngine/Core/Handle.h"
#include "MyEngine/ECS/Entity.h"
#include "MyEngine/Editor/Widgets/CurveEditor.h"
#include "MyEngine/Timeline/TimelineManager.h"
#include "MyEngine/Timeline/TimelinePlayhead.h"
#include "MyEngine/Timeline/TimelineTimeView.h"

// Timeline ウィンドウの部品（TimelineWindow・TimelineRows・TimelineDetails）が一緒に使う物。ほかからはインクルードしない

constexpr float kTimelineListWidth = 230.0f;      // 左のトラックの一覧の幅（ピクセル）
constexpr float kTimelineRowHeight = 30.0f;       // トラック1行の高さ
constexpr float kTimelineScrollbarHeight = 12.0f; // 横のスクロールバーの高さ

/// <summary>
/// 選んでいる物（複数選択は作っていない）
/// </summary>
struct TimelineSelection {
	int track = -1;       // トラックの番号（無ければ -1）
	int clip = -1;        // クリップの番号（無ければ -1）
	bool hasKey = false;  // キーの印を選んでいる
	float keyTime = 0.0f; // 選んでいるキーの時間（クリップの始まりからの秒）
	int channel = 0;      // カーブの絵に出す成分
};

/// <summary>
/// 列の上でつまんでいる物
/// </summary>
enum class TimelineDrag {
	None,
	Clip,    // クリップを動かす
	ClipEnd, // クリップの右端で長さを変える
	Key,     // キーの印を動かす
};

/// <summary>
/// Timeline ウィンドウの状態（1つのファイルだけ開ける）
/// </summary>
struct TimelineEditorState {
	// ===== 開いている物 =====
	uint32_t handle = 0;  // 開いているファイル（0 なら開いていない）
	EntityId ownerId = 0; // 再生する Entity。Play・Stop で作り直されると Handle は変わるが、番号は変わらない
	Handle<Entity> owner; // ownerId から毎フレーム引き直す（居なければ無効。プレビューできない）

	// ===== 見え方 =====
	TimelineTimeView view;  // 見ている時間の範囲
	float laneWidth = 1.0f; // 右の時間の列の幅（ピクセル）
	float snapStep = 0.1f;  // 目盛りの間隔（秒。0 ならスナップしない）
	bool needsFit = true;   // 次に描くとき、全体が見える範囲にする

	// ===== 再生位置 =====
	bool isGameRunning = false; // エディタの Play 中か（ウィンドウのボタンは TimelineSystem を動かす）
	TimelinePlayhead playhead;  // 停止中のプレビューの再生位置
	float time = 0.0f;          // 再生位置（秒）
	float duration = 0.0f;      // 長さ（秒）
	bool isPreview = false;     // 停止中のプレビューをしている
	bool isLoop = false;        // 停止中のプレビューをループする（ファイルにも Component にも書かない）

	// ===== 選択・操作 =====
	TimelineSelection selection;            // 選んでいる物
	CurveEditor curveEditor;                // 下のカーブの絵
	TimelineDrag drag = TimelineDrag::None; // つまんでいる物
	float dragOffset = 0.0f;                // つまんだ物とマウスの時間の差（秒）
	float dragKeyTime = 0.0f;               // つまんだキーの、操作の前の時間（クリップの始まりからの秒）
	std::vector<Curve> dragCurves;          // つまんだクリップの、操作の前のカーブ（毎フレームここから作り直す。途中で追い越してもハンドルが縮まない）

	// ===== Undo =====
	std::string committedText; // 最後に Undo へ積んだときの中身（JSON の文字）
	uint32_t seenRevision = 0; // 知っている中身の版（Undo で版が増えたら、選択を確かめ直す）
	bool hasEdit = false;      // 直して、まだ Undo に積んでいない

	// ===== 調べる =====
	Timeline* GetTimeline() const { return TimelineManager::GetForEdit(handle); }
	// 選んでいるトラック（無ければ nullptr）
	TimelineTrack* GetSelectedTrack() const;
	// 選んでいるクリップのカーブ（無ければ空）
	std::span<Curve> GetSelectedCurves() const;
	// 時間を目盛りへそろえる（Ctrl を押している間はそろえない）
	float Snap(float value) const;

	// ===== 変える =====
	// 中身を直した後に呼ぶ（操作が終わったら TimelineWindow が保存して Undo に積む）
	void MarkEdited();
	// 再生位置を動かす。停止中はプレビューを始める。Play 中は TimelineSystem::Seek
	void SeekTo(float value);
	void SelectTrack(int track);
	void SelectClip(int track, int clip);
	void SelectKey(int track, int clip, float keyTime);
	// 中身が変わった後に、無くなった物を選んだままにしない
	void ClampSelection();
};

// ===== 部品（どれも今のカーソルの位置から描く）=====
// 左上の Add Track と、時間の目盛り（ここをつまむと再生位置が動く）
void DrawTimelineRuler(TimelineEditorState& state);
// トラックの一覧と時間の列
void DrawTimelineRows(TimelineEditorState& state, float height);
// 横のスクロールバー
void DrawTimelineScrollbar(TimelineEditorState& state);
// 下の詳細（トラックの結び付け・クリップ・キー・カーブ）
void DrawTimelineDetails(TimelineEditorState& state, float height);

// ===== 部品の間で使う =====
// 新しいクリップのカーブに、対象の今の値で平らなキーを入れる
void FillNewClipCurves(const TimelineEditorState& state, TimelineTrack& track, size_t clip);
// 結び付けの問題（無ければ空。トラックの行に出す）
std::string FindTimelineBindingProblem(const TimelineEditorState& state, const TimelineTrack& track);
// トラックの対象を entity にする（再生する Entity の子孫でなければ、/ で始まるシーンのパスで指す）
void SetTimelineTarget(TimelineEditorState& state, TimelineTrack& track, Handle<Entity> entity);