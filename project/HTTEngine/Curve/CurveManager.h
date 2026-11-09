#pragma once
#include <set>
#include <string>
#include <string_view>
#include <cstdint>
#include <filesystem>

#include "HTTEngine/Curve/Curve.h"
#include "HTTEngine/ECS/ComponentSerializer.h"

/// <summary>
/// カーブのファイル（.curve）を読む・持つ・保存する（AttackTableManager と同じ形）
/// <para>同じファイルを選んだ物はみんな同じカーブを見るので、ファイル1つを直すと、使っている全部に効く</para>
/// <para>中身は Inspector で直す（Project ウィンドウでファイルを選ぶ → CurveAssetEditor）</para>
/// <para>ほかのファイルの中にカーブを入れたいときは、ToJson・FromJson をそのまま使う</para>
/// </summary>
class CurveManager {
public:
	static constexpr std::string_view kFileExtension = ".curve"; // カーブのファイルの拡張子

	static void Initialize();
	static void Release();

	/// <summary>
	/// ファイルを読む。同じファイルなら、前に読んだ物の番号を返す
	/// </summary>
	/// <returns>番号（1から）。読めなければ 0</returns>
	static uint32_t Load(const std::filesystem::path& filePath);

	// 番号からパスを引く（知らない番号なら空）
	static const std::string& GetPath(uint32_t handle);

	// 中身（0・知らない番号なら nullptr）。ポインタは使い捨て
	static const Curve* Get(uint32_t handle);

	// 中身を書き換える（Inspector から。ファイルにはまだ書かない）
	static void Set(uint32_t handle, const Curve& curve);

	// ファイルに書く（前のファイルは .bak として1つ残る）
	static bool Save(uint32_t handle);

	// 新しいファイルを、既定の中身で作る（Project ウィンドウの Create → Curve）。同じ名前のファイルがあれば作らない
	static bool CreateNewFile(const std::filesystem::path& filePath);

	// ===== JSON（ファイルの形は { "keys": [{...}, ...] }。1つの中身は DescribeKey の項目）=====
	static SceneJson ToJson(const Curve& curve);
	// 読めない項目は warnings に書いて、既定の値のままにする。並びや範囲の外の値は Curve が直す
	static Curve FromJson(const SceneJson& json, std::set<std::string>& warnings);

	// ===== 見せ方（Inspector とファイルの両方がこの並びを使う）=====
	// isInUsed・isOutUsed が false のハンドルは灰色にする（保存には関係しない）
	static void DescribeKey(ComponentUI& ui, CurveKey& value, bool isInUsed = true, bool isOutUsed = true);
};