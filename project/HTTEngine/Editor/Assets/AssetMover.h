#pragma once
#include <filesystem>


/// <summary>
/// エディタでファイル・フォルダの名前を変える（動かす）係（Unreal の「参照の付け替え（Fix Up）」と同じ考え）
/// <para>1. ディスクの名前を変える（1つ前の .bak も一緒に）</para>
/// <para>2. AssetMoveRegistry に知らせる。読み込み済みの Manager・エディタの窓が、覚えているパスを付け替える（番号はそのままなので、開いているシーンの欄も外れない。保存は新しいパスへ）</para>
/// <para>3. resources/ の中の、パスを書く種類のファイル（シーン・Prefab・.material など）の中の前のパスを、新しいパスに書き換える（開いていないシーンの欄も外れない）</para>
/// <para>4. Source/ のコードに前のパスが書いてあれば、警告を出す（コードは書き換えない。文字を組み立てているかもしれないので）</para>
/// <para>エクスプローラー・git で名前を変えたときは知らせが来ないので、欄が外れる（名前はエディタの Project ウィンドウで変える）</para>
/// </summary>
class AssetMover {
public:
	/// <summary>
	/// ファイル・フォルダの名前を変える（動かす）
	/// </summary>
	/// <returns>変えられたか（同じ名前がもうある・ディスクで変えられないなら false で、何も変えない）</returns>
	static bool Move(const std::filesystem::path& from, const std::filesystem::path& to);
};
