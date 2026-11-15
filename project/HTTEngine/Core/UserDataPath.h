#pragma once
#include <filesystem>

/// <summary>
/// プレイヤーのデータの場所（プレイヤーが変えたキーの割り当てなど、遊ぶ人ごとに残す物の保存先）
/// <para>ゲームのフォルダ（起動したフォルダ＝ Parameters/ を読むのと同じ所）の UserData/。エディタも製品版も同じ</para>
/// <para>git には入れない（ゲームのリポジトリの .gitignore に UserData/ を足す。チームの既定の割り当ては Parameters/ の方）</para>
/// <para>書けない所（Program Files など）にゲームを置くと保存できない（書く側がログに出す）。保存先を変えるときは、ここだけ直す</para>
/// </summary>
namespace UserDataPath {
// 保存先のフォルダ（作りはしない。書く側が create_directories する）
inline const std::filesystem::path& GetDirectory() {
	static const std::filesystem::path directory = "UserData";
	return directory;
}
} // namespace UserDataPath
