#include "HTTEngine/Math/Matrix3x3.h"

// 単位行列の生成
Matrix3x3 MakeIdentity3x3() {
	Matrix3x3 result = {0};
	result.m[0][0] = 1.0f;
	result.m[1][1] = 1.0f;
	result.m[2][2] = 1.0f;
	return result;
}