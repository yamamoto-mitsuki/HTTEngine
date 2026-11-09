#pragma once
#include <vector>

#include "HTTEngine/Core/FixedList.h"
#include "HTTEngine/ECS/Script.h"

/// <summary>
/// 距離の段階1つ（一覧の要素の見本）。memcpy で写せる型なので、データの Component の FixedList にも入れられる
/// </summary>
struct DistanceStage {
	float maxDistance = 10.0f; // この段階になる、相手までの距離の上限（m）
	float damageScale = 1.0f;  // この段階のダメージの倍率
};

/// <summary>
/// 出す場所の組1つ（一覧の中に一覧がある見本）
/// </summary>
struct SpawnWave {
	float delay = 1.0f;          // 前の組から何秒後に出すか
	std::vector<Vector3> points; // 出す場所（ワールドの位置）
};

/// <summary>
/// Inspector の一覧（ui.ListField）を確かめる Script（Sandbox のテスト用）
/// <para>Stages：要素が struct の一覧。見出しに距離と倍率を出す</para>
/// <para>Waves：要素の中に、さらに一覧がある</para>
/// <para>Targets：Entity への参照の一覧（Hierarchy からドラッグで入れる。Prefab に入れても、作った物の中の相手を指す）</para>
/// <para>再生中は、Target Distance の距離がどの段階か、と倍率をログに出す（Stages を上から見て、最初に距離が入る段階）</para>
/// </summary>
class ListFieldSample : public Script {
public:
	void Update() override;
	void Fields(ComponentUI& ui) override;

	// 距離がどの段階か（Stages を上から見て、最初に maxDistance 以下になる段階。どれにも入らなければ -1）
	int FindStage(float distance) const;

private:
	std::vector<DistanceStage> stages_ = {{10.0f, 1.0f}, {20.0f, 1.5f}, {40.0f, 2.0f}}; // 距離の段階（近い順に並べる）
	std::vector<SpawnWave> waves_;                                                     // 出す場所の組
	std::vector<GameObject> targets_;                                                  // 相手の一覧
	float targetDistance_ = 15.0f;                                                     // 段階を調べる距離（m。再生中に変えると、ログの段階が変わる）
	int lastStage_ = -2;                                                               // 前にログに出した段階（実行中だけ。変わったときだけ出す）
};


/// <summary>
/// データの Component の一覧（FixedList）を確かめる見本（Sandbox のテスト用）
/// </summary>
struct ListFieldSampleData {
	FixedList<DistanceStage, 4> stages = {{10.0f, 1.0f}, {20.0f, 1.5f}}; // 距離の段階（4つまで）
};
