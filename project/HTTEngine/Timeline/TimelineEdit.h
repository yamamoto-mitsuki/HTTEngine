#pragma once
#include <span>
#include <vector>
#include <string_view>

#include "HTTEngine/Curve/Curve.h"
#include "HTTEngine/Timeline/TimelineManager.h"


/// <summary>
/// Timeline の中身を直す操作（ImGui を知らない。Timeline ウィンドウと自己テストから使う）
/// <para>キーは成分ごとのカーブに入っているが、ウィンドウでは同じ時間のキーを1つの印に見せる。ここの関数は、その1つの印を全部の成分へまとめて行う</para>
/// </summary>
class TimelineEdit {
public:
	static constexpr float kSameKeyTime = 0.0001f;      // この差より近いキーは、同じ時間とみなす（秒）
	static constexpr float kDefaultClipDuration = 1.0f; // 新しいクリップの長さ（秒）
	static constexpr float kMinClipDuration = 0.01f;    // クリップの右端をつまんで縮められる下限（秒）

	// ===== トラック =====
	// 足す。足した番号を返す（知らない種類なら -1）
	static int AddTrack(Timeline& timeline, std::string_view typeName);
	static void RemoveTrack(Timeline& timeline, int index);
	// index のトラックを newIndex へ動かす。動かした先の番号を返す
	static int MoveTrack(Timeline& timeline, int index, int newIndex);

	// ===== クリップ =====
	// start 秒に足すクリップの長さ（次のクリップの手前まで。ぶつからなければ kDefaultClipDuration）
	static float CalcNewClipDuration(const TimelineTrack& track, float start);

	// ===== キー =====
	// 全部のカーブのキーの時間を、重ならないように小さい順に並べる
	static std::vector<float> CollectKeyTimes(std::span<const Curve> curves);
	// キーのあるカーブ全部へ、形を変えずにキーを足す
	static void AddKeys(std::span<Curve> curves, float time);
	// その時間のキーを全部消す
	static void RemoveKeys(std::span<Curve> curves, float time);
	// from の時間のキーを全部 to の時間へ動かす
	static void MoveKeys(std::span<Curve> curves, float from, float to);
	// カーブを、0 秒と duration 秒に value のキーがある平らな線にする
	static void FillFlat(Curve& curve, float value, float duration);
};