#pragma once
#include <vector>
#include <cstdint>

#include "Animation.h"
#include "HTTEngine/Graphics/Animation/AnimationPose.h"


/// <summary>
/// アニメーションを1つ再生する（今の時刻を進めて、その時刻のポーズを作る）
/// <para>1体ごとに1つ持つ。同じモデルを2体動かすなら、AnimationPlayerも2つ</para>
/// <para>使い方: Play → 毎フレーム Update → SamplePose → BuildNodeMatrices（AnimationPose.h）で行列にする</para>
/// <para>2つを混ぜるときは、2つの AnimationPlayer で SamplePose して、BlendPose で混ぜてから行列にする</para>
/// </summary>
class AnimationPlayer {
public:
	// ===== 再生の操作 =====
	/// <summary>
	/// 最初から再生する（逆に流す（SetSpeed が負）なら終わりから。速さは先に SetSpeed で決めておく）
	/// </summary>
	/// <param name="index">ファイルの中の何番目か（ふつうの0）</param>
	void Play(uint32_t animationHandle, uint32_t index = 0, bool loop = true);

	/// <summary>
	/// 止めて最初の時刻に戻す（アニメーションは覚えたまま。Play し直さなくても SetTime でポーズを見られる）
	/// </summary>
	void Stop();

	/// <summary>
	/// 一時停止・再開
	/// </summary>
	void SetPaused(bool paused) { isPaused_ = paused; }


	// ===== 毎フレーム =====
	/// <summary>
	/// 時刻を進める
	/// </summary>
	void Update(float deltaTime);

	/// <summary>
	/// 今の時刻のポーズ（ノードごとの 大きさ・回転・位置）を作る
	/// <para>アニメが動かさないノードは、モデルに入っている最初の形（isAnimated が false）</para>
	/// <para>モデルが無ければ outPose を空にする。Play していなければ、全部のノードが最初の形</para>
	/// </summary>
	/// <param name="outPose">入れる所（メモリを使い回すため、呼ぶ側が持つ）</param>
	void SamplePose(uint32_t modelHandle, AnimationPose& outPose);


	// ===== 取得・設定 =====
	// 再生中のアニメーション（Playしていなければ nullptr）
	const Animation* GetAnimation() const { return animation_; }


	// ===== ゲッター・セッター =====
	bool IsPlaying() const { return isPlaying_; }
	bool IsPaused() const { return isPaused_; }
	float GetTime() const { return time_; }
	void SetTime(float time);
	float GetSpeed() const { return speed_; }
	void SetSpeed(float speed) { speed_ = speed; }
	bool IsLoop() const { return loop_; }
	void SetLoop(bool loop) { loop_ = loop; }


private:
	// ===== ノード1つ分の「何で動かすか」 =====
	// モデルとアニメーションの組み合わせが変わったときだけ作り直す
	struct NodeBinding {
		const NodeAnimation* animation = nullptr; // このノードを動かすカーブ（無ければ nullptr=動かないノード）
		// モデルに入っている最初の形（全部のノードで覚える）
		// キーが無いカーブ（回転だけのアニメ）と、別のアニメと混ぜるとき（相手だけが動かすノード）に使う
		Vector3 scale = {1.0f, 1.0f, 1.0f};
		Quaternion rotate;
		Vector3 translate = {0.0f, 0.0f, 0.0f};
	};

	// モデルのノードとアニメーションのカーブをノード名でつなぐ
	void Bind(uint32_t modelHandle);

	// ===== 再生している物 =====
	// AnimationManager が持っている物を見ているだけ
	const Animation* animation_ = nullptr;

	// ===== 再生の状態 =====
	float time_ = 0.0f;      // 今の時刻（秒 0~duration）
	float speed_ = 1.0f;     // 速さ
	bool loop_ = true;       // ループするか
	bool isPlaying_ = false; // 再生中か
	bool isPaused_ = false;  // 一時停止中か

	// ===== ノードとカーブのつながり =====
	uint32_t boundModelHandle_ = 0;             // Bind したときのモデル（0はまだ）
	const Animation* boundAnimation_ = nullptr; // Bind したときのアニメーション
	std::vector<NodeBinding> bindings_;         // ModelAsset::nodes と同じ並び
};
