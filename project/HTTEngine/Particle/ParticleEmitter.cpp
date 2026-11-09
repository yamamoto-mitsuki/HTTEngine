#include "HTTEngine/Particle/ParticleEmitter.h"

#include "HTTEngine/Time/EngineTime.h"


//=============================================================================
// 更新
//=============================================================================
void ParticleEmitter::Update() { 
	frequencyTime_ += Time::GetDeltaTime();
	// 発生頻度に応じて、パーティクルを作成
	if (frequencyTime_ >= frequency) {
		frequencyTime_ -= frequency;
		Emit();
	}
}


//=============================================================================
// パーティクルを1つ作成
//=============================================================================
Particle ParticleEmitter::MakeNewParticle() { 
	Particle newParticle;
	newParticle.transform.scale = {1.0f,1.0f,1.0f};
	newParticle.transform.rotation = {0.0f,0.0f,0.0f};
	newParticle.transform.translation = startPosition;
	newParticle.velocity = RandomEngine::GetVector3({-0.5f, -0.5f, -0.5f}, {0.5f,0.5f,0.5f});
	newParticle.lifeTime = RandomEngine::GetFloat(1.0f, 3.0f);
	newParticle.color = {1.0f, 1.0f, 1.0f, 1.0f};
	newParticle.color = RandomEngine::GetVector4({0.0f, 0.0f, 0.0f, 0.0f}, {1.0f,1.0f,1.0f,1.0f});

	return newParticle;
}


//=============================================================================
// 設定したエミッターによってパーティクルを発生
//=============================================================================
void ParticleEmitter::Emit() {
	// 初回はグループを作成
	if (group_ == ParticleManager::kInvalidGroup) {
		group_ = ParticleManager::CreateGroup(setting);
	} else {
		ParticleManager::SetGroupSetting(group_, setting);
	}
	// 登録
	for (uint32_t i = 0; i < count; ++i) {
		ParticleManager::Register(group_, MakeNewParticle());
	}
}