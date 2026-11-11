#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "HTTEngine/Core/EnumTips.h"

// 前方宣言
class ComponentUI;


/// <summary>
/// 2つの物がぶつかったとき、2つの値をどう混ぜるか（Unity の PhysicMaterialCombine と同じ）
/// <para>2つの物で違うときは、下にある方（番号の大きい方）を使う（どちらかが Maximum なら Maximum）</para>
/// </summary>
enum class PhysicMaterialCombine {
	Average,  // 2つの平均
	Minimum,  // 小さい方
	Multiply, // 掛け算
	Maximum,  // 大きい方
};

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(PhysicMaterialCombine) {
	using enum PhysicMaterialCombine;
	return MakeEnumTips<PhysicMaterialCombine>({
	    {Average, "2つの平均"},
	    {Minimum, "小さい方"},
	    {Multiply, "掛け算"},
	    {Maximum, "大きい方"},
	});
}

// 摩擦の既定（Physic Material を選んでいない Collider・新しく作った .physmat・ファイルに書いていないとき）。Unity の既定の Physic Material と同じ
// 0 にすると、押された箱・転がった物が床の上でいつまでも滑り続ける
constexpr float kDefaultFriction = 0.6f;

/// <summary>
/// 物の表面の性質（Unity の Physic Material）。Collider に付ける（ファイル .physmat を選ぶ）
/// <para>Collider が選んでいなければ、既定の値（摩擦 kDefaultFriction・跳ね返らない）</para>
/// </summary>
struct PhysicMaterial {
	float friction = kDefaultFriction; // 摩擦（滑っているときに止める強さ。0：止まらない。0.5 くらいで普通、1 でよく止まる）
	float bounciness = 0.0f; // 跳ね返り（0：ぶつかった向きの速さが0になる 1：同じ速さで跳ね返る）
	PhysicMaterialCombine frictionCombine = PhysicMaterialCombine::Average; // 相手と摩擦をどう混ぜるか
	PhysicMaterialCombine bounceCombine = PhysicMaterialCombine::Average;   // 相手と跳ね返りをどう混ぜるか

	bool operator==(const PhysicMaterial&) const = default;
};


/// <summary>
/// Physic Material のファイル（.physmat）を読む・持つ・保存する
/// <para>同じファイルを選んだ Collider はみんな同じ番号になるので、ファイル1つを直すと、使っている全部に効く</para>
/// <para>中身は Inspector で直す（Project ウィンドウでファイルを選ぶ → AssetInspector）。つまむ・打つのが終わったらファイルに書く</para>
/// </summary>
class PhysicMaterialManager {
public:
	static constexpr std::string_view kFileExtension = ".physmat"; // Physic Material のファイルの拡張子

	static void Initialize();
	static void Release();

	/// <summary>
	/// ファイルを読む。同じファイルなら、前に読んだ物の番号を返す
	/// </summary>
	/// <returns>番号（1から）。読めなければ 0</returns>
	static uint32_t Load(const std::filesystem::path& filePath);

	// 番号からパスを引く（知らない番号なら空）
	static const std::string& GetPath(uint32_t handle);

	// 中身（0・知らない番号なら、既定の物＝摩擦 kDefaultFriction・跳ね返り 0）
	static const PhysicMaterial& Get(uint32_t handle);

	// 中身を書き換える（Inspector から。ファイルにはまだ書かない）
	static void Set(uint32_t handle, const PhysicMaterial& material);

	// ファイルに書く（Inspector で、つまむ・打つのが終わったとき）
	static bool Save(uint32_t handle);

	/// <summary>
	/// 新しいファイルを、既定の中身で作る（Project ウィンドウの Create → Physic Material）
	/// <para>名前を CreateFile にしないのは、windows.h に同じ名前のマクロ（CreateFileW に置き換わる）があるため</para>
	/// </summary>
	/// <returns>作れたか（同じ名前のファイルがあれば作らない）</returns>
	static bool CreateNewFile(const std::filesystem::path& filePath);

	// ぶつかった2つの値を、混ぜ方に合わせて1つにする（押し戻すときに PhysicsSystem が使う）
	static PhysicMaterial Combine(const PhysicMaterial& a, const PhysicMaterial& b);

	// 見せ方（Inspector とファイルの両方がこの並びを使う。Component の Describe〜 と同じ書き方）
	static void Describe(ComponentUI& ui, PhysicMaterial& value);
};