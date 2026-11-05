#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "MyEngine/Graphics/Material/Material.h"

// 前方宣言
class ComponentUI;


/// <summary>
/// マテリアルのファイル（.material）を読む・持つ・保存する（PhysicMaterialManager と同じ形）
/// <para>同じファイルを選んだ物はみんな同じ番号になるので、ファイル1つを直すと、使っている全部に効く</para>
/// <para>中身は Inspector で直す（Project ウィンドウでファイルを選ぶ → MaterialAssetEditor）。欄は Shader の #PROPERTIES から作る</para>
/// </summary>
class MaterialManager {
public:
	static constexpr std::string_view kFileExtension = ".material"; // マテリアルのファイルの拡張子

	static void Initialize();
	static void Release();

	/// <summary>
	/// ファイルを読む。同じファイルなら、前に読んだ物の番号を返す
	/// <para>Shader の #PROPERTIES に無い値は、警告を出して捨てる（次に保存したときにファイルから消える）</para>
	/// </summary>
	/// <returns>番号（1から）。読めなければ 0</returns>
	static uint32_t Load(const std::filesystem::path& filePath);

	// 番号からパスを引く（知らない番号なら空）
	static const std::string& GetPath(uint32_t handle);

	// 中身（0・知らない番号なら nullptr）。ポインタは使い捨て（Load で増えると動く）
	static const Material* Get(uint32_t handle);

	// 中身を書き換える（Inspector から。ファイルにはまだ書かない）
	static void Set(uint32_t handle, const Material& material);

	// ファイルに書く（前のファイルは .bak として1つ残る）
	static bool Save(uint32_t handle);

	/// <summary>
	/// 新しいファイルを作る（Project ウィンドウの Create → Material）。Shader は未選択
	/// <para>名前を CreateFile にしないのは、windows.h に同じ名前のマクロ（CreateFileW に置き換わる）があるため</para>
	/// </summary>
	/// <returns>作れたか（同じ名前のファイルがあれば作らない）</returns>
	static bool CreateNewFile(const std::filesystem::path& filePath);

	/// <summary>
	/// 描くときの形にする（Shader の番号・描くときの状態・gMaterial に詰めた中身）
	/// <para>Curve を付けた値は、progress（Curve の横軸。描き手が渡す 0〜1）で始まりと終わりの間を補間する</para>
	/// </summary>
	/// <param name="progress">Curve の横軸（0〜1。何を渡すかは描き手が決める）</param>
	/// <returns>描けるか（知らない番号・Shader が未選択なら false）</returns>
	static bool Resolve(uint32_t handle, float progress, ResolvedMaterial& out);

	/// <summary>
	/// 見せ方（Inspector とファイルの両方がこの並びを使う。Component の Describe〜 と同じ書き方）
	/// <para>Shader を変えたら、値を新しい Shader の #PROPERTIES に合わせ直す（名前と種類が同じ値は残す）</para>
	/// </summary>
	static void Describe(ComponentUI& ui, Material& material);

	/// <summary>
	/// 値を Shader の #PROPERTIES に合わせ直す（名前と種類が同じ値は残し、無い値は既定の値で足し、合わない値は捨てる）
	/// </summary>
	static void MatchValues(Material& material);
};
