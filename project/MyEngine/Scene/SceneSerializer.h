#pragma once
#include <string>

#include "MyEngine/ECS/ComponentSerializer.h"

// 前方宣言
struct MovedPath;

// シーンファイルを読んだ結果
enum class SceneLoadResult {
	Loaded,   // 読めた
	NotFound, // ファイルが無い・中身が空（まだ一度も保存していないシーン）
	Failed,   // ファイルはあるが読めない（壊れている・新しい版のエンジンで保存された）
};


/// <summary>
/// 全Entity（名前・有効・親子・Component）をJSONにする / JSONから作る
/// <para>Componentの中身は ComponentSerializer に登録した型だけが書かれる（COMPONENT(...) の型とエンジンの型は全部入っている）</para>
/// <para>読み込みはフレームの境目（SceneManager::Update の頭）でだけ行う。今あるEntityは消さないので、先に DestroyAllNow を呼ぶ</para>
/// <para>シーンファイルには、Prefab から置いた物を参照（"prefab" と一番上の Transform）と、置いた所ごとの上書きだけで書く。Play の退避の文字列は中身も全部書く</para>
/// </summary>
class SceneSerializer {
public:
	// ファイルの形の版。値の意味を変えたら（回転をクオータニオンにする等）上げて、古い版を読み替える処理を足す
	// 2：Prefab から置いた物を参照で書く（"prefab"）。1 のファイルはそのまま読める（"prefab" が無いだけ）
	// 3：Prefab のファイルの中の入れ子の Prefab も参照で書く。2 のファイルはそのまま読める（入れ子が中身ごと書いてあるだけ）
	// 4：置いた所ごとの上書き（"overrides" など）を書く。3 のファイルはそのまま読める（上書きが無いだけ）
	// 5：Entity のレイヤー（"layer"。Default なら書かない）を書く。4 のファイルはそのまま読める（全部 Default）
	static constexpr int kVersion = 5;

	// ===== ファイル =====
	// フォルダが無ければ作る。前のファイルは「〇〇.bak」として1つだけ残す
	static bool SaveFile(const std::string& path);
	// 読む前に、書き換わった Prefab を読み直す（PrefabManager::ReloadChangedFiles）
	static SceneLoadResult LoadFile(const std::string& path);

	// ===== 文字列（Playを押した瞬間の退避に使う。Prefab の中身も全部書く＝シーンで直した所も Stop で戻る）=====
	static std::string SaveToText();
	static bool LoadFromText(const std::string& text);

	// 全Entityを今すぐ消す（予約ではない。同じEntityIdで作り直すため）。フレームの境目でだけ呼ぶ
	static void DestroyAllNow();

	// ===== Entity の一部分（Prefab が使う。中身の形はシーンファイルと同じ）=====
	// root と、その子孫を {"version", "entities"} にする（root が先頭・親が子より前。root の親は0として書く）
	// 番号は shift で XOR して書く（中の Entity への参照も。Prefab の番号のずれを戻すため。PrefabManager.h）。root の「置いた物の印」は書かない
	// 中に、別の Prefab から置いた物（入れ子）があれば、それは参照だけを書く（シーンファイルと同じ形。中身は書かない）
	static SceneJson SaveTree(Handle<Entity> root, EntityId shift = 0);
	// JSON を人が読める形でファイルに書く（フォルダが無ければ作る。前のファイルは「〇〇.bak」として1つだけ残す）。path は UTF-8
	static bool WriteFile(const std::string& path, const SceneJson& root);
	// {"version", "entities"} の版を確かめて、entities の配列を返す（読めない版・形なら、ログに出して nullptr）
	static const SceneJson* FindEntities(const SceneJson& root);

	// ===== 名前を変えた・動かしたアセット =====
	// JSON の中の文字の値で、動いた物のパスを全部新しいパスにする（項目の名前は見ない）。1つでも書き換えたら true
	// エディタの AssetMover（ファイルの中）と、JSON のまま覚えている所（Prefab の中身・攻撃部品の値）が使う
	static bool RemapPaths(SceneJson& json, const MovedPath& moved);
};