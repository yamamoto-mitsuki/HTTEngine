#pragma once
#include "HTTEngine/Math/Vector3.h"
#include "HTTEngine/Math/Transform.h"

// 4x4行列構造体
struct Matrix4x4 {
	float m[4][4];
};

// --- 二項演算子 ---
Matrix4x4 operator+(const Matrix4x4& m1, const Matrix4x4& m2);
Matrix4x4 operator-(const Matrix4x4& m1, const Matrix4x4& m2);
Matrix4x4 operator*(const Matrix4x4& m1, const Matrix4x4& m2);

// --- 単項演算子 ---
Matrix4x4 operator-(const Matrix4x4& m);

// 行列の加法
Matrix4x4 Add(const Matrix4x4& m1, const Matrix4x4& m2);
// 行列の減法
Matrix4x4 Subtract(const Matrix4x4& m1, const Matrix4x4& m2);
// 行列の積
Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2);
// 逆行列（無ければ単位行列。大きさが 0 の軸がある行列など）
Matrix4x4 Inverse(const Matrix4x4& m);
// 転置行列
Matrix4x4 Transpose(const Matrix4x4& m);
// 単位行列の作成
Matrix4x4 MakeIdentity4x4();

// 平行移動行列
Matrix4x4 MakeTranslateMatrix(const Vector3& translate);
// 拡大拡縮行列
Matrix4x4 MakeScaleMatrix(const Vector3& scale);
// 点の変換（位置も足す。例：ローカル座標の点 → ワールド座標の点。射影行列でも使えるよう、最後に w で割る）
Vector3 TransformPoint(const Vector3& point, const Matrix4x4& matrix);
// 向きの変換（位置は足さない。例：ローカルの向き → ワールドの向き。）
Vector3 TransformNormal(const Vector3& vector, const Matrix4x4& matrix);

// X軸回転行列
Matrix4x4 MakeRotateXMatrix(float radian);
// Y軸回転行列
Matrix4x4 MakeRotateYMatrix(float radian);
// Z軸回転行列
Matrix4x4 MakeRotateZMatrix(float radian);

// 3次元アフィン変換行列
Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate);
// MakeAffineMatrix の逆。行列から 大きさ・回転（ラジアン。X→Y→Zの順）・位置 を取り出す
// 親の大きさが軸ごとに違い、しかも子が回っているとゆがみが混ざり、3つでは表しきれない。そのときは近い値になる
Transform DecomposeAffineMatrix(const Matrix4x4& matrix);


// 透視投影行列
Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip);
// 正射影行列
Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip);
// ビューポート変換行列
Matrix4x4 MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth);

// UV変換行列
Matrix4x4 MakeUVTransformMatrix(const Transform& uvTransform);