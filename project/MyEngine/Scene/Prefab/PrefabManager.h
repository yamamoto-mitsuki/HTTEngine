#pragma once
#include <memory>
#include <cstdint>
#include <set>
#include <string>
#include <filesystem>

#include "MyEngine/Core/Handle.h"
#include "MyEngine/ECS/ComponentSerializer.h"
#include "MyEngine/ECS/Entity.h"
#include "MyEngine/Math/Vector3.h"

// Prefab の置き場所と、名前の後ろに付く部分（"resources/prefabs/Box.prefab" の Prefab の名前は "Box"）
inline constexpr const char* kPrefabFolder = "resources/prefabs/";
inline constexpr const char* kPrefabFileExtension = ".prefab";
// 名前から Prefab のファイルの場所を作る（"Box" → "resources/prefabs/Box.prefab"）
inline std::string MakePrefabFilePath(const std::string& name) { return std::string(kPrefabFolder) + name + kPrefabFileExtension; }


/// <summary>
/// Prefab（Entity・その子・Component・Script をまとめてファイルに取っておき、同じ物を何個でも作れる物。Unity の Prefab）
/// <para>ファイルの中身はシーンファイルと同じ形（{"version", "entities"}）。並びは一番上の Entity が先頭で、親が子より前</para>
/// <para>作る：Instantiate。Entity と Transform はその場でできるので、すぐ位置を変えてよい。
/// ほかの Component・Script は次のフレームの頭（Flush）に付く（GameObject::AddComponent と同じ「予約」）</para>
/// <para>作った物の一番上には「置いた物の印」（PrefabInstanceComponent）が付き、シーンファイルには参照だけが書かれる。
/// Prefab を保存し直すと、次にシーンを開いたときから新しい中身になる（開いている今のシーンの物は変わらない）</para>
/// <para>番号のずれ：置いた物の番号は「ファイルの番号 XOR ずれ」。ずれは「置いた物の一番上の番号 XOR ファイルの一番上の番号」で、
/// 中の全員で同じ。一番上の番号さえシーンに書けば、中の子の番号も毎回同じになる（シーンのほかの Entity から、中の子を指せる）</para>
/// <para>上書き：置いた物の中身（子も含む全部の Component・名前・有効・親）を、置いた所ごとに変えてよい。シーンファイルには違う所だけが書かれ、
/// Prefab のほかの所を直すと、上書きしていない所は Prefab のとおりに変わる（Unity の Override。作るのは PrefabOverrides）</para>
/// <para>入れ子：Prefab の中に、別の Prefab から置いた物を入れてよい。Prefab のファイルにも参照だけが書かれ、作るときにその Prefab から作る
/// （内側の Prefab を直すと、外側の Prefab から作った物も変わる）。中の番号は、入れ子の一番上を基準に同じ XOR で決まる</para>
/// <para>分け方：中身を JSON のまま並べる・上書きを当てるのは PrefabRecords、JSON の違いは PrefabDiff、上書きを作るのは PrefabOverrides。
/// このクラスは、読み込み・保存・Entity を作る・持ち主を調べる</para>
/// </summary>
class PrefabManager {
public:
	static void Initialize();
	static void Release();

	// ===== 読み込み（アセット。InspectorのAssetField（AssetType::Prefab）からも呼ばれる）=====
	// 同じファイルは1回だけ読む（2回目からは同じ番号を返す）。読めなければ0
	static uint32_t Load(const std::filesystem::path& filePath);
	// 番号からパス（UTF-8。保存・Inspector の表示に使う）。知らない番号なら空
	static const std::string& GetPath(uint32_t handle);
	// 番号から中身（ファイルの "entities"。読めていない・知らない番号なら nullptr）
	// shared_ptr で渡すので、このあと Prefab が保存し直されても、受け取った中身はそのまま使える
	static std::shared_ptr<const SceneJson> GetEntities(uint32_t handle);
	// 読み込んだ後にファイルが書き換わった Prefab を読み直す（git pull で届いた Prefab を使うため）。シーンファイルを読む前に呼ぶ
	static void ReloadChangedFiles();

	// ===== 保存（エディタの Hierarchy の右クリック → Save As Prefab・Apply から）=====
	/// <summary>
	/// rootと、その子孫をPrefabのファイルに書く。もう読み込んでいる Prefab なら、中身も差し替える（このあと作る物から新しい中身になる）
	/// <para>root には「この Prefab から置いた物」の印を付ける（Unity と同じ。次からシーンには参照だけが書かれる）</para>
	/// <para>root がこの Prefab から置いた物なら、ずれを戻して書く（ファイルの番号が変わらないので、diff が小さい）</para>
	/// </summary>
	/// <param name="path">書く場所（UTF-8。ふつうは MakePrefabFilePath(名前)）</param>
	static bool Save(Handle<Entity> root, const std::string& path);
	// path に Prefab のファイルがもうあって、root がその Prefab から置いた物ではないか（Save As Prefab で、ほかの Prefab を上書きしないように調べる）
	static bool IsOtherPrefabFile(Handle<Entity> root, const std::string& path);

	// ===== 作る =====
	/// <summary>
	/// Prefab から Entity を作り、一番上の Entity を返す（読めていない番号なら無効な Handle）
	/// <para>一番上の Entity の位置（translation）は position になる。回転・大きさは Prefab のまま</para>
	/// </summary>
	/// <param name="parent">親（無効な Handle なら一番上に置く）</param>
	static Handle<Entity> Instantiate(uint32_t prefab, const Vector3& position, Handle<Entity> parent = {});
	/// <summary>
	/// シーンファイルを読むとき用。entry（シーンファイルの "prefab" の付いた1項目）のとおりに作り、Component・印まで今すぐ付ける（フレームの境目でだけ呼ぶ）
	/// <para>一番上の番号・名前・有効・Component（Transform など）は entry のとおり。親は付けない（呼ぶ側が付ける）</para>
	/// <para>中の番号がほかの Entity とぶつかったら、新しい番号で作って warnings に書く。読めていない番号なら無効な Handle</para>
	/// </summary>
	static Handle<Entity> InstantiateNow(uint32_t prefab, const SceneJson& entry, std::set<std::string>& warnings);
	// エディタから置く（ImGui を描いている途中で Entity を増やさないように、次のフレームの頭に作る。原点に置く）
	static void RequestPlace(uint32_t prefab);
	// 予約した物を作る・Component を付ける。フレームの頭（WindowManager::UpdateAll の EntityManager::FlushComponentChanges の後）に呼ぶ
	static void Flush();

	// ===== 置いた物（Prefab のインスタンス）=====
	// entity を「Prefab の中身」として持つ、一番近い置いた物（親の方へたどる。entity 自身は見ない）。無ければ無効な Handle
	// あとから足した子は中身ではない（シーンファイルにそのまま書かれる）。入れ子の中身なら、入れ子の一番上が持ち主（その一番上の持ち主は外側）
	static Handle<Entity> FindOwner(Handle<Entity> entity);
	// エディタで「Prefab から置いた物」として見せる一番上（entity が置いた物なら entity、中身なら持ち主）。無ければ無効な Handle
	// .prefab から置いた物だけ（UI Screen の .uiscreen から作った画面は、同じ仕組みで作るが Prefab としては見せない）
	static Handle<Entity> FindInstanceRoot(Handle<Entity> entity);
	// 置いた物を、元の Prefab のファイルに書く（Unity の Apply）
	static bool Apply(Handle<Entity> root);
	// Prefab とのつながりを切る（印を外す。次のフレームの頭に外れ、それからは中身もシーンファイルに書かれる）
	static void Unpack(Handle<Entity> root);
};
