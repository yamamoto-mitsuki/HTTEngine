#pragma once
#include <type_traits>

#include "HTTEngine/Core/EnumTips.h"
#include "HTTEngine/ECS/Entity.h"

// 同じ Entity の Collider の範囲に入ったら・出たら、Target（別の Entity）を出す・消す。Entityに付けるComponent（カテゴリ：Gameplay）
// チュートリアルの説明（Text・Image）・看板を、近づいた所でだけ見せるのに使う。Script を書かずに、エディタだけで置ける
// 範囲は同じ Entity の Collider（Is Trigger にする。ギズモは Collider の線）。誰が入ったら反応するかは Source で選ぶ
// 出す＝Target を有効にする（Inspector の名前の左のチェック）。消す＝無効にする。フェードは Target の Alpha Group の Alpha を動かす
// 動かすのは TriggerActivatorSystem（再生中だけ）。停止中は何もしない（Stop で、Play を押した瞬間の状態に戻る）

/// <summary>
/// 誰が入ったら反応するか。Inspector では、Entity を選んだときだけ Source Entity が出る
/// </summary>
enum class TriggerSource {
	FollowTarget, // メインカメラの Camera Follow・Camera Orbit の Target（横から見るゲームのプレイヤー。何も選ばなくてよい）
	Entity,       // Source Entity に入れた Entity（子に付けた Collider も、その Entity として数える）
	AnyRigidbody, // Rigidbody を付けた物なら、どれでも
	AnyMover,     // 動き方の Component（Rigidbody・Character Controller）を付けた物なら、どれでも
};

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(TriggerSource) {
	using enum TriggerSource;
	return MakeEnumTips<TriggerSource>({
	    {FollowTarget, "メインカメラの Camera Follow・Camera Orbit の Target（プレイヤー。何も選ばなくてよい）"},
	    {Entity, "Source Entity に入れた Entity（子に付けた Collider も、その Entity として数える）"},
	    {AnyRigidbody, "Rigidbody を付けた物なら、どれでも"},
	    {AnyMover, "動き方の Component（Rigidbody・Character Controller）を付けた物なら、どれでも"},
	});
}

/// <summary>
/// 入ったとき・出たときに、Target をどうするか
/// </summary>
enum class TriggerAction {
	None, // 何もしない
	Show, // 出す（有効にして、Fade In Time でだんだん見せる）
	Hide, // 消す（Fade Out Time でだんだん消して、消えきったら無効にする）
};

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(TriggerAction) {
	using enum TriggerAction;
	return MakeEnumTips<TriggerAction>({
	    {None, "何もしない"},
	    {Show, "出す（有効にして、Fade In Time でだんだん見せる）"},
	    {Hide, "消す（Fade Out Time でだんだん消して、消えきったら無効にする）"},
	});
}

/// <summary>
/// 今のフェード（実行中だけの値）
/// </summary>
enum class TriggerFade {
	None,      // していない（出しきった・消しきった）
	FadingIn,  // だんだん見せている
	FadingOut, // だんだん消している（消えきったら Target を無効にする）
};


/// <summary>
/// 範囲に入ったら・出たら、別の Entity を出す・消す（Unity なら、OnTriggerEnter で SetActive する Script を書く所）
/// </summary>
struct TriggerActivatorComponent {
	bool enabled = true;                                // false の間は反応しない（フェードの途中なら、最後まで続ける）
	EntityRef target;                                   // 出す・消す Entity（Hierarchy からドラッグ。文字と背景をまとめた親を入れると、子も一緒）
	TriggerSource source = TriggerSource::FollowTarget; // 誰が入ったら反応するか
	EntityRef sourceEntity;                             // source が Entity のとき：反応する相手
	TriggerAction onEnter = TriggerAction::Show;        // 入ったとき
	TriggerAction onExit = TriggerAction::Hide;         // 出たとき
	bool once = false;                                  // true：入ったとき・出たときの動きを、それぞれ1回だけにする（2回目に入っても何もしない）
	bool hideOnStart = true;                            // true：再生を始めたら、すぐ Target を消す（エディタでは見えたまま置ける）
	float fadeInTime = 0.3f;                            // 出すときに、見えきるまでの秒数（0 ならすぐ）
	float fadeOutTime = 0.3f;                           // 消すときに、消えきるまでの秒数（0 ならすぐ）

	// --- 実行中だけの値（TriggerActivatorSystem が書く。保存しない）---
	bool isStarted = false;               // 再生を始めたときの準備（Hide On Start・Alpha Group を付ける）をしたか
	bool isInside = false;                // 前のフレームに、Source が範囲の中にいたか（今と比べて、入った・出たを決める）
	bool isEnterUsed = false;             // 入ったときの動きをしたか（Once のとき、2回目をしないため）
	bool isExitUsed = false;              // 出たときの動きをしたか（同じ）
	TriggerFade fade = TriggerFade::None; // 今のフェード
	float fadeAlpha = 1.0f;               // フェードの透明度（0〜1。Target の Alpha Group の Alpha ＝ baseAlpha × これ）
	float baseAlpha = 1.0f;               // 再生を始めたときの、Target の Alpha Group の Alpha（エディタで 0.8 にしておいた物は、0.8 まで出す）
};

static_assert(std::is_trivially_copyable_v<TriggerActivatorComponent>);