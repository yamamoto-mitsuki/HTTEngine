#pragma once
#include <random>
#include <cstdint>

#include "HTTEngine/Math/MathIncludes.h"


/// <summary>
/// 乱数生成クラス。
/// </summary>
class RandomEngine {
public:
	static void Initialize();

	/// min以上max以下のfloatを返す
	static float GetFloat(float min, float max);

	/// min以上max以下のint32_tを返す
	static int32_t GetInt(int32_t min, int32_t max);

	/// 各成分がmin〜maxのVector3を返す
	static Vector3 GetVector3(const Vector3& min, const Vector3& max);

	/// 各成分がmin〜maxのVector4を返す
	static Vector4 GetVector4(const Vector4& min, const Vector4& max);

	/// 半径1の球の内部のランダムな点を返す
	static Vector3 GetInsideUnitSphere();


private:
	static std::mt19937 engine_;
};