#pragma once
#include <set>
#include <string>

#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/Entity.h"


/// <summary>
/// Prefab から置いた物の、置いた所ごとの上書き（Unity の Override）
/// <para>Prefab のとおりに並べた物（PrefabRecords::Expand）と今の物を JSON で比べ、違う所だけをシーンファイルの1項目の形にする</para>
/// <para>読むときに上書きを当てるのは PrefabRecords::ApplyEntry（PrefabManager::InstantiateNow から）</para>
/// </summary>
class PrefabOverrides {
public:
	/// <summary>
	/// 置いた物を、シーンファイルの1項目の形にする（{"id", "name", "parent", "active", "prefab", "components", "removedComponents", "overrides", "removed"}）
	/// <para>Prefab のとおりに作った物と比べて、置いた所ごとに変えた所（上書き）だけを書く。番号は今の番号。一番上の Transform はいつも書く</para>
	/// <para>"components"：一番上の Component の違う項目だけ（Prefab に無い型は丸ごと）。"removedComponents"：外した型</para>
	/// <para>"overrides"：中身1つずつの、違う名前・有効・親・Component。"removed"：消した中身。あとから足した子は書かない（シーンにふつうに書く）</para>
	/// <para>置いた物でなければ null</para>
	/// </summary>
	static SceneJson MakeInstanceEntry(Handle<Entity> root, std::set<std::string>& warnings);
	// 置いた所ごとの上書きがあるか（一番上の名前・有効・親・Transform と、あとから足した子は数えない）
	static bool HasOverrides(Handle<Entity> root);
	// Prefab が読めなかった置いた物の、ファイルの項目を覚える（MakeInstanceEntry が、上書きをそのまま書き戻す）
	static void KeepUnreadEntry(const SceneJson& entry);
};
