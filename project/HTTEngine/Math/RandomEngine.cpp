#include "HTTEngine/Math/RandomEngine.h"

// 静的メンバ変数
std::mt19937 RandomEngine::engine_;


//=============================================================================
// 初期化
//=============================================================================
void RandomEngine::Initialize() { 
	std::random_device seedGenerator; 
	engine_.seed(seedGenerator());
}


//=============================================================================
// 基本型
//=============================================================================
float RandomEngine::GetFloat(float min, float max) {
	std::uniform_real_distribution<float> dist(min, max);
	return dist(engine_);
}

int32_t RandomEngine::GetInt(int32_t min, int32_t max) {
	std::uniform_int_distribution<int32_t> dist(min, max);
	return dist(engine_);
}

//=============================================================================
// ベクトル
//=============================================================================
Vector3 RandomEngine::GetVector3(const Vector3& min, const Vector3& max) { return {GetFloat(min.x, max.x), GetFloat(min.y, max.y), GetFloat(min.z, max.z)}; }

Vector4 RandomEngine::GetVector4(const Vector4& min, const Vector4& max) { return {GetFloat(min.x, max.x), GetFloat(min.y, max.y), GetFloat(min.z, max.z), GetFloat(min.w, max.w)}; }

//=============================================================================
// 半径1の球の内部のランダムな点
//=============================================================================
Vector3 RandomEngine::GetInsideUnitSphere() {
	while (true) {
		Vector3 p = GetVector3({-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f});
		if (p.x * p.x + p.y * p.y + p.z * p.z <= 1.0f) {
			return p;
		}
	}
}