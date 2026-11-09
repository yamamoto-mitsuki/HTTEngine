#pragma once
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"

/// <summary>
/// Attack Table ウィンドウ。再生中の敵の様子（フェーズ・状態・段階・残り時間）と、攻撃ごとの抽選確率・条件・クールタイムを出す
/// <para>Hierarchy で Attack Table の付いた Entity を選んでいればその敵、選んでいなければ最初の敵を出す</para>
/// </summary>
class AttackTableWindow {
public:
	// 毎フレーム1回（ImGuiManager::Begin から）
	static void Draw();

private:
	// 出す敵を決める（居なければ無効な Handle）
	static Handle<Entity> FindEntity();
	// 上の段（フェーズ・状態・段階・残り時間）
	static void DrawStatus(Handle<Entity> entity);
	// 下の段（攻撃ごとの表）
	static void DrawAttacks(Handle<Entity> entity);
};