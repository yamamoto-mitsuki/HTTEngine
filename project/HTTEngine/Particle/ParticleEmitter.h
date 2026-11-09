#pragma once
#include "HTTEngine/Math/MathIncludes.h"
#include "HTTEngine/Particle/ParticleManager.h"
#include "HTTEngine/Graphics/Pipeline/RenderStates.h"

/// <summary>
/// パーティクルのエミッター
/// </summary>
class ParticleEmitter {
public:
	OBB obb;                // パーティクルの範囲
	uint32_t count;         // 1回の発生数
	float frequency = 1.0f; // 発生頻度（秒）
	Vector3 startPosition;  // 発生開始位置
	ParticleGroupSetting setting; // 色やテクスチャなどパーティクル全体の設定
	bool isBillboard = true; // ビルボード

	/// <summary>
	/// 引数のパーティクルを更新する
	/// </summary>
	/// <param name="emitter">エミッター</param>
	void Update();

	/// <summary>
	/// 設定されたエミッターによってパーティクルを発生させる
	/// </summary>
	/// <param name="emitter">発生させたい設定</param>
	void Emit();

	/// <summary>
	/// パーティクルを1つ新しく作成
	/// </summary>
	/// <returns></returns>
	Particle MakeNewParticle();

private:
	float frequencyTime_ = 0;                         // 頻度用の時刻
	uint32_t group_ = ParticleManager::kInvalidGroup; // このエミッターが放出するグループ
};