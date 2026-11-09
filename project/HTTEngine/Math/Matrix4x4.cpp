#include "HTTEngine/Math/Matrix4x4.h"

#include <array>
#include <cmath>
#include <algorithm>

#include "HTTEngine/Diagnostics/HTTAssert.h"


// ===== 二項演算子 =====

// 行列の加算
Matrix4x4 operator+(const Matrix4x4& m1, const Matrix4x4& m2) { return Add(m1, m2); }
// 行列の減算
Matrix4x4 operator-(const Matrix4x4& m1, const Matrix4x4& m2) { return Subtract(m1, m2); }
// 行列の乗算
Matrix4x4 operator*(const Matrix4x4& m1, const Matrix4x4& m2) { return Multiply(m1, m2); }

// ===== 単項演算子 =====
Matrix4x4 operator-(const Matrix4x4& m) {
	Matrix4x4 result = MakeIdentity4x4();
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			result.m[i][j] = -m.m[i][j];
		}
	}
	return result;
}

// ===== 行列の加法 =====
Matrix4x4 Add(const Matrix4x4& m1, const Matrix4x4& m2) {
	Matrix4x4 result = MakeIdentity4x4();
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			result.m[i][j] = m1.m[i][j] + m2.m[i][j];
		}
	}
	return result;
}

// ===== 行列の減法 =====
Matrix4x4 Subtract(const Matrix4x4& m1, const Matrix4x4& m2) {
	Matrix4x4 result = MakeIdentity4x4();
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			result.m[i][j] = m1.m[i][j] - m2.m[i][j];
		}
	}
	return result;
}
// ===== 行列の積 =====
Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2) {
	Matrix4x4 result;
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			result.m[i][j] = m1.m[i][0] * m2.m[0][j] + m1.m[i][1] * m2.m[1][j] + m1.m[i][2] * m2.m[2][j] + m1.m[i][3] * m2.m[3][j];
		}
	}
	return result;
}

// ===== 逆行列 =====
Matrix4x4 Inverse(const Matrix4x4& m) {
	Matrix4x4 inv = MakeIdentity4x4();
	Matrix4x4 temp = m;

	for (int i = 0; i < 4; i++) {
		// --- ピボット選択 ---
		// その列の中で一番絶対値が大きい数字を持つ行を探し、現在の行と入れ替える
		int pivot = i;
		for (int j = i + 1; j < 4; j++) {
			if (std::abs(temp.m[j][i]) > std::abs(temp.m[pivot][i])) {
				pivot = j;
			}
		}
		std::swap(temp.m[i], temp.m[pivot]);
		std::swap(inv.m[i], inv.m[pivot]);
		// --- 逆行列が存在しないとき：単位行列を返す ---
		// 大きさ（Scale）が 0 の軸がある・3つの軸が1つの面につぶれていると、逆行列は無い
		// Inspector で Scale をドラッグして 0 を通ると、描くたびに法線の行列（Transpose(Inverse(world))）でここへ来るので、止めずに続ける
		// 大きさ 0 の物は見えないので、単位行列でも見た目は変わらない
		if (std::abs(temp.m[i][i]) <= 1e-6f) {
			return MakeIdentity4x4();
		}

		// --- 正規化 ---
		// 現在の行のすべての要素を対角成分で除算
		float div = temp.m[i][i];
		for (int j = 0; j < 4; j++) {
			temp.m[i][j] /= div;
			inv.m[i][j] /= div;
		}

		// --- 掃き出し ---
		// 他の行のi列目をすべて0にする
		for (int k = 0; k < 4; k++) {
			if (k != i) {
				float mul = temp.m[k][i];
				for (int j = 0; j < 4; j++) {
					temp.m[k][j] -= temp.m[i][j] * mul;
					inv.m[k][j] -= inv.m[i][j] * mul;
				}
			}
		}
	}
	return inv;
}

// ===== 転置行列 =====
Matrix4x4 Transpose(const Matrix4x4& m) {
	Matrix4x4 result;
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			result.m[i][j] = m.m[j][i];
		}
	}
	return result;
}

// ===== 単位行列の作成 =====
Matrix4x4 MakeIdentity4x4() {
	Matrix4x4 result = {0};
	result.m[0][0] = 1.0f;
	result.m[1][1] = 1.0f;
	result.m[2][2] = 1.0f;
	result.m[3][3] = 1.0f;
	return result;
}

// ===== 平行移動行列 =====
Matrix4x4 MakeTranslateMatrix(const Vector3& translate) {
	Matrix4x4 result = MakeIdentity4x4();
	result.m[3][0] = translate.x;
	result.m[3][1] = translate.y;
	result.m[3][2] = translate.z;
	return result;
}

// ===== 拡大縮小行列 =====
Matrix4x4 MakeScaleMatrix(const Vector3& scale) {
	Matrix4x4 result = {0};
	result.m[0][0] = scale.x;
	result.m[1][1] = scale.y;
	result.m[2][2] = scale.z;
	result.m[3][3] = 1.0f;
	return result;
}

// ===== 点の変換 =====
// (x, y, z, 1) として掛ける。4行目（位置）も足す。最後にwで割る（射影行列を掛けたとき w が1でなくなるので）
Vector3 TransformPoint(const Vector3& point, const Matrix4x4& matrix) {
	Vector3 result;
	result.x = point.x * matrix.m[0][0] + point.y * matrix.m[1][0] + point.z * matrix.m[2][0] + matrix.m[3][0];
	result.y = point.x * matrix.m[0][1] + point.y * matrix.m[1][1] + point.z * matrix.m[2][1] + matrix.m[3][1];
	result.z = point.x * matrix.m[0][2] + point.y * matrix.m[1][2] + point.z * matrix.m[2][2] + matrix.m[3][2];
	const float w = point.x * matrix.m[0][3] + point.y * matrix.m[1][3] + point.z * matrix.m[2][3] + matrix.m[3][3];
	HTT_ASSERT_MSG(w != 0.0f, "w が0です（カメラと同じ場所の点を射影した？）");
	result.x /= w;
	result.y /= w;
	result.z /= w;
	return result;
}

// ===== 向きの変換 =====
// (x, y, z, 0) として掛ける。4行目（位置）は足さない＝1〜3行目（回転と大きさ）だけ使う
Vector3 TransformNormal(const Vector3& vector, const Matrix4x4& matrix) {
	return {
	    vector.x * matrix.m[0][0] + vector.y * matrix.m[1][0] + vector.z * matrix.m[2][0],
	    vector.x * matrix.m[0][1] + vector.y * matrix.m[1][1] + vector.z * matrix.m[2][1],
	    vector.x * matrix.m[0][2] + vector.y * matrix.m[1][2] + vector.z * matrix.m[2][2],
	};
}

// X軸回転行列
Matrix4x4 MakeRotateXMatrix(float radian) {
	float s = std::sin(radian);
	float c = std::cos(radian);
	Matrix4x4 result = MakeIdentity4x4();
	result.m[1][1] = c;
	result.m[1][2] = s;
	result.m[2][1] = -s;
	result.m[2][2] = c;
	return result;
}

// Y軸回転行列
Matrix4x4 MakeRotateYMatrix(float radian) {
	float s = std::sin(radian);
	float c = std::cos(radian);
	Matrix4x4 result = MakeIdentity4x4();
	result.m[0][0] = c;
	result.m[0][2] = -s;
	result.m[2][0] = s;
	result.m[2][2] = c;
	return result;
}

// Z軸回転行列
Matrix4x4 MakeRotateZMatrix(float radian) {
	float s = std::sin(radian);
	float c = std::cos(radian);
	Matrix4x4 result = MakeIdentity4x4();
	result.m[0][0] = c;
	result.m[0][1] = s;
	result.m[1][0] = -s;
	result.m[1][1] = c;
	return result;
}

// 3次元アフィン変換行列
Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate) {
	Matrix4x4 scaleMatrix = MakeScaleMatrix(scale);
	Matrix4x4 rotateXMatrix = MakeRotateXMatrix(rotate.x);
	Matrix4x4 rotateYMatrix = MakeRotateYMatrix(rotate.y);
	Matrix4x4 rotateZMatrix = MakeRotateZMatrix(rotate.z);
	Matrix4x4 rotateMatrix = Multiply(Multiply(rotateXMatrix, rotateYMatrix), rotateZMatrix);
	Matrix4x4 translateMatrix = MakeTranslateMatrix(translate);
	Matrix4x4 result = Multiply(Multiply(scaleMatrix, rotateMatrix), translateMatrix);

	return result;
}

// 行列 → 大きさ・回転・位置（MakeAffineMatrix の逆）
// 行列 = 大きさ × 回転(X×Y×Z) × 位置 なので、
//   4行目   … 位置
//   1〜3行目 … 「i番目の大きさ × 回転の i行目」。行の長さが大きさで、大きさで割ると回転の行が残る
// 回転の行列 X×Y×Z を掛け算して並べると、次の場所に sin・cos がそのまま出てくる
//   r[0][2] = -sin(y)
//   r[1][2] =  sin(x)cos(y)   r[2][2] = cos(x)cos(y)   → x = atan2(r[1][2], r[2][2])
//   r[0][1] =  cos(y)sin(z)   r[0][0] = cos(y)cos(z)   → z = atan2(r[0][1], r[0][0])
// cos(y) が 0（Y軸で真横まで回った）のときは x と z が同じ回り方になって区別できないので、z = 0 として x に入れる
Transform DecomposeAffineMatrix(const Matrix4x4& matrix) {
	constexpr float kEpsilon = 1e-6f;
	Transform result;

	// --- 位置：4行目 ---
	result.translation = {matrix.m[3][0], matrix.m[3][1], matrix.m[3][2]};

	// --- 大きさ：1〜3行目の長さ ---
	std::array<Vector3, 3> rows{};
	for (int i = 0; i < 3; ++i) {
		rows[i] = {matrix.m[i][0], matrix.m[i][1], matrix.m[i][2]};
	}
	result.scale = {Length(rows[0]), Length(rows[1]), Length(rows[2])};
	// 鏡に映したように裏返っている（行列式が負）ときは、X の大きさを負にして表す
	if (Dot(Cross(rows[0], rows[1]), rows[2]) < 0.0f) {
		result.scale.x = -result.scale.x;
	}

	// --- 回転：各行を大きさで割る ---
	const std::array<float, 3> scales = {result.scale.x, result.scale.y, result.scale.z};
	std::array<std::array<float, 3>, 3> r = {{
	    {1.0f, 0.0f, 0.0f},
	    {0.0f, 1.0f, 0.0f},
	    {0.0f, 0.0f, 1.0f},
	}};
	for (int i = 0; i < 3; ++i) {
		// 大きさが0の軸は向きが分からないので、回っていない行のままにする
		if (std::abs(scales[i]) > kEpsilon) {
			r[i][0] = rows[i].x / scales[i];
			r[i][1] = rows[i].y / scales[i];
			r[i][2] = rows[i].z / scales[i];
		}
	}
	// sin(y) = -r[0][2] だけで判定すると、y が 89.95 度でも sin は 0.9999996 でちょうど90度と区別できない
	const float cosY = std::sqrt(r[0][0] * r[0][0] + r[0][1] * r[0][1]);
	result.rotation.y = std::atan2(-r[0][2], cosY);
	if (cosY > kEpsilon) {
		result.rotation.x = std::atan2(r[1][2], r[2][2]);
		result.rotation.z = std::atan2(r[0][1], r[0][0]);
	} else {
		// cos(y) = 0 のとき、r[1][1] = cos(x∓z)、r[2][1] = -sin(x∓z) になる
		result.rotation.x = std::atan2(-r[2][1], r[1][1]);
		result.rotation.z = 0.0f;
	}
	return result;
}

// 透視投影行列
Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip) {
	Matrix4x4 result = {0};
	float cot = 1.0f / std::tan(fovY / 2.0f);
	result.m[0][0] = cot / aspectRatio;
	result.m[1][1] = cot;
	result.m[2][2] = farClip / (farClip - nearClip);
	result.m[2][3] = 1.0f;
	result.m[3][2] = (-nearClip * farClip) / (farClip - nearClip);
	return result;
}

// 正射影行列
Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip) {
	Matrix4x4 result = {0};
	result.m[0][0] = 2.0f / (right - left);
	result.m[1][1] = 2.0f / (top - bottom);
	result.m[2][2] = 1.0f / (farClip - nearClip);
	result.m[3][0] = (left + right) / (left - right);
	result.m[3][1] = (top + bottom) / (bottom - top);
	result.m[3][2] = nearClip / (nearClip - farClip);
	result.m[3][3] = 1.0f;
	return result;
}

// ビューポート変換行列
Matrix4x4 MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth) {
	Matrix4x4 result = {0};
	result.m[0][0] = width / 2.0f;
	result.m[1][1] = -height / 2.0f;
	result.m[2][2] = maxDepth - minDepth;
	result.m[3][0] = left + (width / 2.0f);
	result.m[3][1] = top + (height / 2.0f);
	result.m[3][2] = minDepth;
	result.m[3][3] = 1.0f;
	return result;
}

Matrix4x4 MakeUVTransformMatrix(const Transform& uvTransform) {
	Vector3 invScale = {
	    1.0f / uvTransform.scale.x,
	    1.0f / uvTransform.scale.y,
	    1.0f / uvTransform.scale.z,
	};
	Matrix4x4 mat = MakeScaleMatrix(invScale);
	//Matrix4x4 mat = MakeScaleMatrix(uvTransform.scale);
	mat = Multiply(mat, MakeRotateZMatrix(uvTransform.rotation.z));
	mat = Multiply(mat, MakeTranslateMatrix(uvTransform.translation));
	return mat;
}