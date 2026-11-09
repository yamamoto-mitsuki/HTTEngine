#pragma once

struct Matrix3x3 {
	float m[3][3];
};

// 単位行列の生成
Matrix3x3 MakeIdentity3x3();