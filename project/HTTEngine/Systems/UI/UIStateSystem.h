#pragma once
#include "HTTEngine/Components/UI/UIStateComponent.h"
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"

/// <summary>
/// UI State（要素の状態で Timeline を再生する）を動かす
/// <para>毎フレーム：マウスから Hover・Pressed を決めて、状態が変わったら、その状態の Timeline を再生する（TimelineLayer::UIState。前の状態の再生は置き換わる）</para>
/// <para>Show・Hide は別の層（TimelineLayer::UIVisibility）で、状態の再生より後に書く。消えている間はマウスに反応しない</para>
/// <para>最初のフレームは、最初の状態の Timeline の最後の形をすぐ当てる（動かさない）。Start Visible を外していれば Hide の最後の形</para>
/// <para>ゲームが止まっている間（ポーズ中）は、開いている画面（UI Screen）の中の要素だけがマウスに反応する</para>
/// </summary>
class UIStateSystem {
public:
	// UI State を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	// 毎フレーム（Script の後、TimelineSystem::Update の前。頼んだ再生を、そのフレームの値に使う）。停止中は全部忘れる
	static void Update(bool isGameRunning);

	// ===== ゲームから使う（UI State が無い Entity なら何もしない・初期値を返す）=====
	// 今の状態
	static UIElementState GetState(Handle<Entity> entity);
	// クリックされた（上で押して、上で離した）。決まるのは Script の後なので、Script から見えるのは次のフレームの1回だけ
	static bool IsClicked(Handle<Entity> entity);
	// 選んでいる印を付ける・外す（マウスが乗っていなければ Selected になる）
	static void SetSelected(Handle<Entity> entity, bool isSelected);
	// 出す・消す（Show・Hide の Timeline を再生する。今と同じなら何もしない）
	static void SetVisible(Handle<Entity> entity, bool isVisible);
	static bool IsVisible(Handle<Entity> entity);

private:
	// Entity が消える直前（EntityManager から呼ばれる）
	static void OnEntityDestroyed(Handle<Entity> entity);
};