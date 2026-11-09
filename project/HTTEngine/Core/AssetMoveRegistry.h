#pragma once
#include <string>
#include <vector>
#include <utility>
#include <filesystem>
#include <string_view>
#include <unordered_map>


/// <summary>
/// 名前を変えた・動かしたファイルかフォルダ1つ（パスは ToPathKey の形。区切りは /）
/// <para>from がファイルならそのファイルだけ、フォルダなら中の物全部が「動いた物」になる</para>
/// <para>Windows のパスは大文字・小文字を区別しないので、比べるときも区別しない（"resources/" と "Resources/" は同じファイル）</para>
/// </summary>
struct MovedPath {
	std::string from; // 前のパス
	std::string to;   // 新しいパス

	// path が動いた物なら、新しいパスに書き換えて true を返す（違えばそのままで false）
	// 区切りが \ の物・「./」が付いた物も、ToPathKey でそろえてから比べる
	bool Apply(std::string& path) const;
	// パスの型で持っている物（エディタの窓が選んでいるファイルなど）。書き換えたら true
	bool Apply(std::filesystem::path& path) const;

	// キーがパスの表の、動いた物のキーを付け替える（中身はそのまま。番号も変わらない）
	template<class Value> void ApplyToKeys(std::unordered_map<std::string, Value>& map) const;
};


/// <summary>
/// ファイル・フォルダの名前を変えた（動かした）ことを、パスで覚えている所へ知らせる表
/// <para>パスで覚えている所（各 Manager・エディタの窓）は、Initialize で AddListener する。知らせを受けたら、覚えているパスを MovedPath::Apply で付け替える</para>
/// <para>中に別のアセットのパスを書くファイルの種類（シーン・Prefab など）は AddReferenceFileType で足す。エディタの AssetMover が、名前を変えたときにその中のパスを書き換える</para>
/// <para>知らせるのは AssetMover（エディタで名前を変えたとき）。Release でも登録は呼べる（知らせが来ないだけ）</para>
/// </summary>
class AssetMoveRegistry {
public:
	using Listener = void (*)(const MovedPath& moved); // 知らせを受ける関数

	// 知らせを受ける関数を足す（同じ関数は1回だけ）
	static void AddListener(Listener listener);

	// 中に別のアセットのパスを書くファイルの種類を足す（名前の後ろの部分。".prefab"・".scene.json"。同じ物は1回だけ）
	static void AddReferenceFileType(std::string_view suffix);

	// 足した関数を全部呼ぶ
	static void Notify(const MovedPath& moved);

	// 中に別のアセットのパスを書く種類のファイルか（名前の後ろで見る。大文字・小文字は区別しない）
	static bool IsReferenceFile(const std::filesystem::path& path);
};


//=============================================================================
// テンプレートの実装
//=============================================================================
// ===== キーがパスの表 =====
template<class Value> void MovedPath::ApplyToKeys(std::unordered_map<std::string, Value>& map) const {
	// --- 付け替えるキーを先に集める（回している途中で表に足し引きすると、回し方が壊れる）---
	std::vector<std::string> keys;
	for (const auto& [key, value] : map) {
		std::string moved = key;
		if (Apply(moved)) {
			keys.push_back(key);
		}
	}
	// --- 1つずつ外して、キーを書き換えて入れ直す ---
	// extract：中身を作り直さずに表から外す（外した物はキーを書き換えられる。テクスチャの GPU の資源なども、そのまま動く）
	for (const std::string& key : keys) {
		auto node = map.extract(key);
		Apply(node.key());
		auto result = map.insert(std::move(node));
		if (!result.inserted) {
			// 新しいパスの物がもう表にある（同じファイルを別の名前で読んでいた）：前のキーのまま戻す（外した物を消すと、番号が指す中身が無くなる）
			result.node.key() = key;
			map.insert(std::move(result.node));
		}
	}
}
