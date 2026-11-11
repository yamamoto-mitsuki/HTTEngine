#pragma once
#include <array>
#include <cstddef>

// enum の選択肢ごとの説明（Inspector で選択肢の一覧を開き、1つにマウスを乗せると出る）
// Unreal の UMETA(ToolTip = "...") と同じく、説明は「項目」ではなく「enum の型」に付ける
// （同じ enum を何か所で使っても、説明は1か所に書くだけで済む。項目そのものの説明は今までどおり ui.Field の Tip に書く）
//
//   書き方：enum の宣言のすぐ下に、同じ名前空間で EnumTips という関数を1つ書く（constexpr で、引数はその enum の型だけ）
//
//   enum class ParticleShape { Point, Sphere };
//   constexpr auto EnumTips(ParticleShape) {
//       using enum ParticleShape; // 中では ParticleShape:: を付けずに書ける（C++20）
//       return MakeEnumTips<ParticleShape>({
//           {Point, "Entity の位置から、全部の向きへ"},
//           {Sphere, "球の中から、外へ"},
//       });
//   }
//
//   ComponentUI は、引数の型から関数を探す決まり（ADL：引数の型が宣言された名前空間も探す）でこの関数を見つける。
//   だから登録は要らない。書かなければ、選択肢の説明は出ないだけ
//   書かなかった選択肢は説明なし。並びは宣言の順でなくてよい


/// <summary>
/// enum の選択肢1つの説明
/// </summary>
template<class E> struct EnumTip {
	E value;          // どの選択肢か
	const char* text; // 説明（Inspector で選択肢にマウスを乗せると出る）
};

// EnumTip の並びを作る（std::to_array と同じ。型を EnumTip<E> と書かずに、enum の型だけで書けるようにする）
// tips の数 N は、{...} の数からコンパイラが決める
template<class E, size_t N> constexpr std::array<EnumTip<E>, N> MakeEnumTips(const EnumTip<E> (&tips)[N]) {
	std::array<EnumTip<E>, N> result{};
	for (size_t i = 0; i < N; ++i) {
		result[i] = tips[i];
	}
	return result;
}
