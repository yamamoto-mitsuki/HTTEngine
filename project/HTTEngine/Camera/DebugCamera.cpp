#include "HTTEngine/Camera/DebugCamera.h"

#include <cmath>
#include <algorithm>

#include "HTTEngine/Camera/Camera.h"
#include "HTTEngine/Input/InputManager.h"

void DebugCamera::Initialize(float fovY, float aspectRatio, float nearZ, float farZ) {
	fovY_ = fovY;
	aspectRatio_ = aspectRatio;
	nearZ_ = nearZ;
	farZ_ = farZ;
	projectionMatrix_ = MakePerspectiveFovMatrix(fovY_, aspectRatio_, nearZ_, farZ_);
	ReCalcViewMatrix();
}

void DebugCamera::Update() {
	Rotate();
	Zoom();
	Translate();
	ReCalcViewMatrix();
}

//=============================================================
// ホイールボタン押しながらドラッグ → オービット回転
//=============================================================
void DebugCamera::Rotate() {
	// ---- マウス移動量を取得 ----
	float dx = static_cast<float>(InputManager::GetMouseDeltaX());
	float dy = static_cast<float>(InputManager::GetMouseDeltaY());

	if (std::abs(dx) < mouseDeltaThreshold) {
		dx = 0.0f;
	}
	if (std::abs(dy) < mouseDeltaThreshold) {
		dy = 0.0f;
	}

	if (InputManager::IsMouseButtonPressed(2)) {
		transform_.rotation.x -= dy * orbitSpeed;
		transform_.rotation.y -= dx * orbitSpeed;
		transform_.rotation.x = std::clamp(transform_.rotation.x, pitchMin, pitchMax);
	}
}

void DebugCamera::Zoom() {
	float dw = static_cast<float>(InputManager::GetMouseDeltaWheel());

	if (std::abs(dw) < mouseDeltaThreshold) {
		dw = 0.0f;
	}

	if (dw != 0.0f) {
		// カメラが向いている前方向を求める
		float cosPitch = std::cos(transform_.rotation.x);
		float sinPitch = std::sin(transform_.rotation.x);
		float cosYaw = std::cos(transform_.rotation.y);
		float sinYaw = std::sin(transform_.rotation.y);

		// 前方向ベクトル（左手座標系）
		Vector3 forward = {cosPitch * sinYaw, -sinPitch, cosPitch * cosYaw};

		transform_.translation.x += forward.x * dw * zoomSpeed;
		transform_.translation.y += forward.y * dw * zoomSpeed;
		transform_.translation.z += forward.z * dw * zoomSpeed;
		transform_.rotation.x = std::clamp(transform_.rotation.x, pitchMin, pitchMax);
	}
}

void DebugCamera::Translate() {
	if (InputManager::IsMouseButtonPressed(1)) {
		float dx = static_cast<float>(InputManager::GetMouseDeltaX());
		float dy = static_cast<float>(InputManager::GetMouseDeltaY());
		float cosYaw = std::cos(transform_.rotation.y);
		float sinYaw = std::sin(transform_.rotation.y);
		// カメラの右方向
		Vector3 right = {cosYaw, 0.0f, -sinYaw};

		transform_.translation.x -= right.x * dx * panSpeed;
		transform_.translation.z -= right.z * dx * panSpeed;
		transform_.translation.y += dy * panSpeed;
	}
}

void DebugCamera::ReCalcViewMatrix() {
	// カメラのワールド行列 → 逆行列でビュー行列を作る
	Matrix4x4 cameraMatrix = MakeAffineMatrix(transform_.scale, transform_.rotation, transform_.translation);
	viewMatrix_ = Inverse(cameraMatrix);
}

Matrix4x4 DebugCamera::CalcWVP(const Matrix4x4& worldMatrix) const { return Multiply(worldMatrix, Multiply(viewMatrix_, projectionMatrix_)); }