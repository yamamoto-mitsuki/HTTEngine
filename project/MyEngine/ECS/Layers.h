#pragma once
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

// 前方宣言
class NameList;

/// <summary>
/// どのレイヤーか、のビットの並び（Unity の LayerMask と同じ）。1つの uint32_t に、32 個のレイヤーのどれを含むかを持つ
/// <para>Component に持たせて「当てる相手のレイヤー」などに使う。保存は数（名前を付け直しても、選んだ物は変わらない）</para>
/// </summary>
struct LayerMask {
	uint32_t bits = 0; // 番号 i のレイヤーを含むなら、i 番目のビットが 1

	// そのレイヤーを含むか
	constexpr bool Contains(int layer) const { return layer >= 0 && layer < 32 && ((bits >> layer) & 1u) != 0; }

	// 何も含まない・全部含む・1つだけ含む
	static constexpr LayerMask Nothing() { return {0u}; }
	static constexpr LayerMask Everything() { return {0xFFFFFFFFu}; }
	static constexpr LayerMask Of(int layer) { return {(layer >= 0 && layer < 32) ? (1u << layer) : 0u}; }

	bool operator==(const LayerMask&) const = default;
};


/// <summary>
/// Entity のレイヤー（Unity の Layer と同じ。Entity に1つ。0〜31 の番号で、名前は Settings の Layers で付ける）
/// <para>0 は Default（名前は変えられない）。攻撃の当たり（Hitbox の Targets）などが、相手の Entity のレイヤーで絞る</para>
/// </summary>
class Layers {
public:
	static constexpr int kDefault = 0; // 何も選ばなかったときのレイヤー

	// Settings に名前の行を登録する（EngineSystems::Initialize の初めに1回）
	static void Initialize();

	// 名前の一覧（Inspector の選択肢）
	static const NameList& GetNameList();

	// 番号 → 名前（名前が無ければ空）。Unity の LayerMask.LayerToName
	static std::string LayerToName(int layer);
	// 名前 → 番号（無ければ -1）。Unity の LayerMask.NameToLayer
	static int NameToLayer(std::string_view name);
	// 名前を並べてビットの並びを作る（Layers::GetMask({"Player", "Enemy"})）。Unity の LayerMask.GetMask
	static LayerMask GetMask(std::initializer_list<std::string_view> names);
};
