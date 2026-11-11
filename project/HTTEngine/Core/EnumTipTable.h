#pragma once
#include <array>

#include <externals/magic_enum/magic_enum.hpp>

#include "HTTEngine/Core/EnumTips.h"

// enum の選択肢の説明（EnumTips.h）を、選択肢を出す側が読むための道具
// 書く側（enum の宣言の横）は EnumTips.h だけを include する（magic_enum を読まずに済むように、ファイルを分けている）


// E の選択肢の説明（EnumTips 関数）を書いてあるか
// 関数は、引数の型から探す決まり（ADL）で見つける。だから E の宣言の横に書いてあれば、どこにも登録しなくてよい
template<class E>
concept HasEnumTips = requires(E value) { EnumTips(value); };

/// <summary>
/// E の選択肢の説明を、選択肢の番号（magic_enum::enum_names の並び＝宣言の順）の並びにする。説明の無い選択肢は nullptr
/// <para>Inspector の enum の項目（ComponentUI）と EditorWidgets::EnumCombo が使う</para>
/// <para>constexpr なので、static constexpr の変数で受ければ、作るのはコンパイルのときだけ（毎フレーム作らない）</para>
/// </summary>
template<class E> constexpr std::array<const char*, magic_enum::enum_count<E>()> MakeEnumTipTable() {
	std::array<const char*, magic_enum::enum_count<E>()> texts{}; // 初めは全部 nullptr
	if constexpr (HasEnumTips<E>) {
		for (const EnumTip<E>& tip : EnumTips(E{})) {
			// magic_enum が知らない値（範囲の外の数など）は飛ばす
			if (const auto index = magic_enum::enum_index(tip.value)) {
				texts[*index] = tip.text;
			}
		}
	}
	return texts;
}
