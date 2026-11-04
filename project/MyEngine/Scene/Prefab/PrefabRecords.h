#pragma once
#include <set>
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

#include "MyEngine/ECS/ComponentSerializer.h"
#include "MyEngine/ECS/Entity.h"

// Prefab のファイル・シーンファイルの、Prefab から置いた物の印（値は Prefab のパス）
inline constexpr const char* kPrefabEntryKey = "prefab";
// 置いた物の一番上に付く印（PrefabInstanceComponent）の保存の名前（EngineComponents.cpp の Register と同じ）
inline constexpr const char* kPrefabInstanceComponentName = "Prefab Instance";
// 入れ子の深さの上限（Prefab の中に自分が入っていても、ここで止まる）
inline constexpr size_t kMaxPrefabNestDepth = 8;

// Entity 1つ分の JSON（{"id", "name", "parent", "active", "layer", "components"}。シーンファイルの1項目と同じ形）の並び。親が子より前
// Prefab の中身を「置いたときの番号」で並べた物。Entity を作る前の形
using EntityRecords = std::vector<SceneJson>;

// ===== Entity 1つ分の JSON のレイヤー（"layer"）=====
// 0（Default）は書かない（前のファイル・ほとんどの Entity は Default なので、ファイルを変えないように）。無ければ 0
inline constexpr const char* kLayerKey = "layer";
// 読む（無い・0〜31 の数でなければ 0）
uint8_t ReadEntityLayer(const SceneJson& item);
// 書く（0 なら項目を消す）
void WriteEntityLayer(SceneJson& item, uint8_t layer);


/// <summary>
/// Prefab の中身を、Entity を作らずに JSON のまま並べる・直す
/// <para>Expand：一番上の番号を決めて、中身を置いたときの番号で並べる（入れ子の Prefab も開く）。PrefabManager はこれから Entity を作る</para>
/// <para>ApplyEntry：置いた物の項目（シーンファイルの "prefab" の付いた1項目）を、並べた中身に当てる（置いた所ごとの上書き）</para>
/// <para>番号は全部「ファイルの番号 XOR ずれ」で決まるので、ずらすのも戻すのも XOR（ShiftEntry・Relabel）</para>
/// </summary>
class PrefabRecords {
public:
	/// <summary>
	/// Prefab の中身を、一番上を rootId の番号にして並べる。読めない Prefab なら空
	/// <para>番号・親・Component の中の Entity への参照は、置いたときの番号にして返す。一番上（[0]）の親は 0</para>
	/// <para>入れ子（"prefab" の付いた項目）は、その Prefab の中身を開いて差し込み、一番上に「置いた物の印」を入れる。入れ子の上書きも当てる</para>
	/// </summary>
	static EntityRecords Expand(uint32_t prefab, EntityId rootId, std::set<std::string>& warnings);

	/// <summary>
	/// 置いた物の項目（番号は置いた番号）を、並べた中身 records[begin, begin + count) に当てる
	/// <para>一番上（records[begin]）：名前・有効・親・Component の上書き（"components"）・外した Component（"removedComponents"）</para>
	/// <para>"overrides"：中身1つずつの上書き（"id" で相手を決める）。"removed"：消した中身（子も一緒に消える。records が詰まる）</para>
	/// </summary>
	static void ApplyEntry(EntityRecords& records, size_t begin, size_t count, const SceneJson& entry, std::set<std::string>& warnings);

	// 置いた物の項目の番号（一番上・親・上書きの相手・消した中身）を全部 shift で XOR し、Component の中の参照を map で付け替える
	// Prefab のファイルの番号 ⇔ 置いた番号 の行き来に使う（どちらも全員が同じ数で XOR なので、同じ関数で行き来できる）
	static SceneJson ShiftEntry(SceneJson entry, EntityId shift, const EntityIdMap& map);

	// 並べた中身の番号が、どれも0でなく、まだ誰も使っていないか
	static bool AreIdsFree(const EntityRecords& records);
	// 並べた中身の番号を、全部 delta で XOR する（中身どうしの参照も同じだけずらす。一番上の親は変えない）
	static void Relabel(EntityRecords& records, EntityId delta);

	// Entity 1つ分の "components"（無い・{...} でなければ nullptr）
	static const SceneJson* FindComponents(const SceneJson& item);

private:
	// Expand の中身。stack は開いている途中の Prefab（自分の中に自分が入っていたら、そこで止める）
	static EntityRecords ExpandNested(uint32_t prefab, EntityId rootId, std::vector<uint32_t>& stack, std::set<std::string>& warnings);
};
