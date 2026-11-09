#pragma once
#include <cstdint>
#include <type_traits>

#include "HTTEngine/String/FixedString.h"

// アニメーション（クリップ）の名前。Blender ならアクションの名前（63 バイトまで）
// FBX の「Armature|Idle」（アーマチュアの名前｜アクションの名前）の形も入るように、127 バイトまでにする
using AnimationClipName = FixedString<128>;

// アニメーションを再生するためのデータ。Entityに付けるComponent（カテゴリ: Animation）
// 同じEntityのModel Rendererのノードを動かす（Model Rendererが無ければ何もしない）
// 今の時刻・ノードの行列（AnimationPlayer）は持たない。AnimationSystem が持つ
// ここに入れると、コピー・Undo・保存で丸ごと写されてしまう

struct AnimatorComponent {
	uint32_t animationHandle = 0; // AnimationManager::Loadの番号。保存はパス
	AnimationClipName clip;       // 流すアニメーションの名前（空なら流さない）。番号ではなく名前で持つので、ファイルの中で足す・消す・並べ替えても同じアニメが流れる
	float speed = 1.0f;           // 1で等速、2で倍速、負で逆再生
	bool loop = true;             // ループするか
	bool playOnStart = true;      // Playを押したとき（シーンが始まったとき）に再生する
	float previewTime = 0.0f;     // 停止中（Editing）に見せる時刻（秒）。Inspectorでつまんでポーズを確かめる
	bool enabled = true;          // false なら動かさない（Model Renderer はモデルに入っている形で描く）
};

static_assert(std::is_trivially_copyable_v<AnimatorComponent>);