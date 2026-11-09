#pragma once
#include <vector>
#include <cstdint>

#include "Matrix4x4.h"
#include "Animation.h"


/// <summary>
/// アニマ―ションを1つ再生する（今の時刻を進めて、モデルのノードの行列を作り直す）
/// <para>1体ごとに1つ持つ。同じモデルを2体動かすなら、AnimationPlayerも2つ</para>
/// <para>使い方: Play → 毎フレームUpdate → ApplyToModel → Renderer::ModelConfig::nodeMatrices に GetNodeMatrices を渡す</para>
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
	/// 今の時刻のポーズで、モデルのノードの行列（根からのワールド行列）を作り直す
	/// </summary>
	void ApplyToModel(uint32_t modelHandle);


	// ===== 取得・設定 =====
	// ApplyToModelで作った行列。ModelAsset::nodesと同じ並び
	const std::vector<Matrix4x4>& GetNodeMatrices() const { return nodeMatrices_; }
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
		// カーブにキーがないとき（回転だけのアニメ）
		Vector3 scale = {1.0f, 1.0f, 1.0f};
		Quaternion rotate;
		Vector3 translate = {0.0f, 0.0f, 0.0f};
	};

	// モデルのノードとアニメーションのカーブをノード名でつなぐ
	void Bind(uint32_t modelhandle);

	// ===== 再生している物 =====
	// AnimationManager が持っている物を見ているだけ
	const Animation* animation_ = nullptr;

	// ===== 再生の状態 =====
	float time_ = 0.0f;      // 今の時刻（秒 0~duration）
	float speed_ = 1.0f;     // 速さ
	bool loop_ = true;       // ループするか
	bool isPlaying_ = false; // 再生中か
	bool isPaused_ = false;  // 一時停止中か

	// ===== ノードの行列 =====
	uint32_t boundModelHandle_ = 0;             // Bind したときのモデル（0はまだ）
	const Animation* boundAnimation_ = nullptr; // Bind したときのアニメーション
	std::vector<NodeBinding> bindings_;         // ModelAsset::nodes と同じ並び
	std::vector<Matrix4x4> nodeMatrices_;       // ModelAsset::nodes と同じ並び（根からのワールド行列）
};