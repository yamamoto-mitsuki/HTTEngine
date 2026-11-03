#pragma once
#include <cstdint>
#include <type_traits>

#include "MyEngine/String/FixedString.h"

// Prefab から置いた物の一番上に付く印（Unity の Prefab インスタンス）。Add Component には出さない
// PrefabManager::Instantiate（Project の Place in Scene・Script の Instantiate）と、Hierarchy の Save As Prefab で付く
// この印がある Entity は、シーンファイルに「どの Prefab を、どこに置いたか」だけを書く（中身は Prefab のファイルから作り直す）
// → Player 担当が Player.prefab を直しても、シーンファイルは変わらない（チームで同じシーンを触ってもぶつかりにくい）
// シーンに書くのは、一番上の名前・有効・親・Transform と、置いた所ごとに変えた所（上書き）と、あとから足した子だけ
// （上書きは Inspector に出る。Apply で Prefab に書く・Revert で捨てる）

/// <summary>
/// Prefab から置いた物の印
/// </summary>
struct PrefabInstanceComponent {
	FixedString<256> path; // 元の Prefab のファイル（UTF-8。"resources/prefabs/Player.prefab"）。ファイルに書くのはこれだけ
	uint32_t prefab = 0;   // PrefabManager の番号（書かない。読んだときに path から引く。ファイルが無ければ 0）
};

static_assert(std::is_trivially_copyable_v<PrefabInstanceComponent>);
