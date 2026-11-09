#pragma once
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

/// <summary>
/// Settings の Project ページに置く「番号 → 名前」の一覧（32 個まで）。レイヤー・攻撃の種類に使う
/// <para>Component には番号（またはビットの並び）を書く。名前を付け直しても、選んだ物は変わらない（Unity のレイヤーと同じ）</para>
/// <para>名前は Settings で付ける（Parameters/&lt;グループ名&gt;/&lt;グループ名&gt;.json に保存）。先頭の決まった名前（Default など）は Settings に出さず、変えられない</para>
/// </summary>
class NameList {
public:
	static constexpr int kCount = 32; // 名前の数（uint32_t のビットの数。ビットの並びで「どれとどれ」を持てるように）

	/// <param name="groupName">Settings のページの名前（保存先のフォルダの名前にもなる）</param>
	/// <param name="itemPrefix">Settings の行の名前の頭（"Layer" なら "Layer 3"）</param>
	/// <param name="fixedNames">先頭から順に、決まった名前（Settings に出さない・変えられない）</param>
	NameList(std::string groupName, std::string itemPrefix, std::initializer_list<std::string_view> fixedNames);

	// Settings に名前の行を登録する（起動のときに1回。保存した名前があれば、その名前になる）
	void Register() const;

	// 名前（範囲の外・名前の無い番号は空）
	std::string GetName(int index) const;
	// 名前から番号（無ければ -1。空の名前も -1）
	int Find(std::string_view name) const;
	// Inspector に出す文字（"3: Enemy"。名前が無ければ "3: (Layer 3)"）
	std::string GetLabel(int index) const;
	// 名前のビットを立てた並び（知らない名前は警告を出して飛ばす）
	uint32_t MakeMask(std::initializer_list<std::string_view> names) const;
	// 名前の付いている番号の一覧（小さい順）。Inspector の選択肢に使う
	std::vector<int> GetNamedIndices() const;

	const std::string& GetGroupName() const { return groupName_; }

private:
	// Settings の行の名前（"Layer 3"）
	std::string ItemName(int index) const;

	std::string groupName_;               // Settings のページの名前
	std::string itemPrefix_;              // 行の名前の頭
	std::vector<std::string> fixedNames_; // 先頭の決まった名前
};
