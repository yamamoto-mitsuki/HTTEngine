#pragma once
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

// 前方宣言
class NameList;

/// <summary>
/// 攻撃の種類（0〜31 の番号。名前は Settings の Damage Types で付ける。Unreal の DamageType の役目）
/// <para>DamageSource で1つ選び、Health の Immune Types（効かない種類）・受ける側の OnBeforeHitTaken（ガードできない種類など）で使う</para>
/// <para>0 は Default（名前は変えられない）</para>
/// </summary>
class DamageTypes {
public:
	static constexpr int kDefault = 0; // 何も選ばなかったときの種類

	// Settings に名前の行を登録する（EngineSystems::Initialize の初めに1回）
	static void Initialize();

	// 名前の一覧（Inspector の選択肢）
	static const NameList& GetNameList();

	// 番号 → 名前（名前が無ければ空）
	static std::string TypeToName(int type);
	// 名前 → 番号（無ければ -1）
	static int NameToType(std::string_view name);
	// 名前を並べてビットの並びを作る（Immune Types に書く値）
	static uint32_t GetMask(std::initializer_list<std::string_view> names);
};
