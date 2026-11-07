#pragma once
#include <random>

#include "MyEngine/ECS/Script.h"

/// <summary>
/// 円を描いて動く（Sandbox のテスト用。UI World Follow の相手・Camera Follow の相手にする）
/// </summary>
class CircleMover : public Script {
public:
	void Start() override;
	void Update() override;
	void Fields(ComponentUI& ui) override;

private:
	float radius_ = 3.0f;               // 円の半径（m）
	float degreesPerSecond_ = 90.0f;    // 1秒に回る角度（度）
	Vector3 center_ = {0.0f, 0.0f, 0.0f}; // 円の真ん中（実行中だけ。Start の位置）
	float angle_ = 0.0f;                // 今の角度（度。実行中だけ）
};

/// <summary>
/// UI World Follow の結果を読んで、端に寄せている間だけ矢印を出し、重ねる点の方へ向ける（Sandbox のテスト用）
/// <para>UI World Follow（Clamp To Edge）の Entity に付ける。Arrow に子の矢印（Vector Shape など）を入れる</para>
/// <para>結果は UI World Follow が Script の後（カメラの後）に書くので、Script が読むのは前のフレームの結果（向きが1フレーム遅れるが、見て分からない）</para>
/// </summary>
class EdgeArrow : public Script {
public:
	void Update() override;
	void Fields(ComponentUI& ui) override;

private:
	GameObject arrow_; // 端に寄せている間だけ出して回す物（子の矢印。上向きに作る）
};

/// <summary>
/// ダメージの数字のように、ワールドの点に出て、上へ浮いて消える（Sandbox のテスト用）
/// <para>UI World Follow（Target は none）の Entity に付ける。Interval 秒ごとに、Spawn Center のまわりの別の点に出し直す</para>
/// <para>浮くのは Transform の Translation の y（写した点からのずれ）。UI World Follow は Translation を書き換えないので、ぶつからない</para>
/// </summary>
class FloatUpText : public Script {
public:
	void Update() override;
	void Fields(ComponentUI& ui) override;

private:
	float interval_ = 1.0f;                     // 出し直すまでの秒
	float riseSpeed_ = 60.0f;                   // 上へ浮く速さ（ピクセル／秒）
	Vector3 spawnCenter_ = {0.0f, 1.0f, 0.0f};  // 出す点の真ん中（ワールドの位置）
	float spawnRadius_ = 2.0f;                  // 出す点を、真ん中からどれだけ散らすか（m）
	float timer_ = 0.0f;                        // 出してからの秒（実行中だけ）
	std::mt19937 random_{std::random_device{}()}; // 出す点を散らす乱数（実行中だけ）
};
