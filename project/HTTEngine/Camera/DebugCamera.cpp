#include "HTTEngine/Camera/DebugCamera.h"

#include <cmath>
#include <algorithm>

#include "HTTEngine/Camera/Camera.h"

void DebugCamera::Initialize(float fovY, float aspectRatio, float nearZ, float farZ) {
	fovY_ = fovY;
	aspectRatio_ = aspectRatio;
	nearZ_ = nearZ;
	farZ_ = farZ;
	projectionMatrix_ = MakePerspectiveFovMatrix(fovY_, aspectRatio_, nearZ_, farZ_);
	ReCalcViewMatrix();
}

void DebugCamera::Update() { ReCalcViewMatrix(); }

//=============================================================
// 操作の量で動かす
//=============================================================
void DebugCamera::ApplyInput(const Input& input) {
	Rotate(input.rotate);
	Dolly(input.dollyDrag * dollyDragSpeed + input.dollyWheel * dollyWheelSpeed);
	Pan(input.pan);
	ReCalcViewMatrix();
}

// ===== 向きを変える（その場で回る）=====
void DebugCamera::Rotate(Vector2 amount) {
	// 手振れ：小さい動きは無視する
	if (std::abs(amount.x) < mouseDeltaThreshold) {
		amount.x = 0.0f;
	}
	if (std::abs(amount.y) < mouseDeltaThreshold) {
		amount.y = 0.0f;
	}
	transform_.rotation.x -= amount.y * rotateSpeed;
	transform_.rotation.y -= amount.x * rotateSpeed;
	transform_.rotation.x = std::clamp(transform_.rotation.x, pitchMin, pitchMax);
}

// ===== 向いている方へ前後に動く =====
void DebugCamera::Dolly(float distance) {
	if (distance == 0.0f) {
		return;
	}
	// カメラが向いている前方向を求める
	const float cosPitch = std::cos(transform_.rotation.x);
	const float sinPitch = std::sin(transform_.rotation.x);
	const float cosYaw = std::cos(transform_.rotation.y);
	const float sinYaw = std::sin(transform_.rotation.y);
	// 前方向ベクトル（左手座標系）
	const Vector3 forward = {cosPitch * sinYaw, -sinPitch, cosPitch * cosYaw};
	transform_.translation.x += forward.x * distance;
	transform_.translation.y += forward.y * distance;
	transform_.translation.z += forward.z * distance;
}

// ===== 上下左右に動く =====
void DebugCamera::Pan(const Vector2& amount) {
	if (amount.x == 0.0f && amount.y == 0.0f) {
		return;
	}
	const float cosYaw = std::cos(transform_.rotation.y);
	const float sinYaw = std::sin(transform_.rotation.y);
	// カメラの右方向
	const Vector3 right = {cosYaw, 0.0f, -sinYaw};
	transform_.translation.x -= right.x * amount.x * panSpeed;
	transform_.translation.z -= right.z * amount.x * panSpeed;
	transform_.translation.y += amount.y * panSpeed;
}

void DebugCamera::ReCalcViewMatrix() {
	// カメラのワールド行列 → 逆行列でビュー行列を作る
	Matrix4x4 cameraMatrix = MakeAffineMatrix(transform_.scale, transform_.rotation, transform_.translation);
	viewMatrix_ = Inverse(cameraMatrix);
}

Matrix4x4 DebugCamera::CalcWVP(const Matrix4x4& worldMatrix) const { return Multiply(worldMatrix, Multiply(viewMatrix_, projectionMatrix_)); }