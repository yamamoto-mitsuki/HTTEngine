#pragma once
#include <cstdint>
#include <string>

#include "MyEngine/Core/Handle.h"

using EntityId = uint64_t; // 保存・Undoで使う番号。0は無し。実行中Handleとは別物。でたらめな数で配る（EntityManager::NewId）


/// <summary>
/// 別のEntityへの参照（Componentに持たせる用）
/// <para>Handleではなく EntityId で覚えるので、保存・Play/Stop・削除のUndo・コピーをまたいでも同じ相手を指す
/// （Handleは作り直すたびに変わる）。使うときは EntityManager::Get&lt;T&gt;(ref) / EntityManager::Find(ref)</para>
/// </summary>
struct EntityRef {
	EntityId id = 0; // 0は「無し」

	bool IsSet() const { return id != 0; }
	bool operator==(const EntityRef&) const = default;
};


/// <summary>
/// Entity
/// </summary>
struct Entity {
	std::string name = "Entity";
	EntityId id = 0;
	Handle<Entity> self;
	Handle<Entity> parent;
	bool isActive = true;
	bool isDontSave = false; // シーンに保存しない（子も一緒に。エディタで編集中の画面の中身）。シーンファイル・Play の退避・保存していない変更（*）に入らない
	uint8_t layer = 0;       // レイヤー（0〜31。名前は Settings の Layers。Layers.h）。攻撃の当たりが「当てる相手」を絞るのに使う
};