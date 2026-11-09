#pragma once
#include <numbers>

#include "HTTEngine/Camera/Camera.h"
#include "HTTEngine/Math/Vector3.h"
#include "HTTEngine/Math/Matrix4x4.h"
#include "HTTEngine/Math/Transform.h"

// 前方宣言
class Camera;


/// <summary>
/// デバックカメラ
/// <para>カメラクラスを継承したもの</para>
/// </summary>
class DebugCamera : public Camera {
public:
	// ピッチ上限
	float pitchMin = -static_cast<float>(std::numbers::pi) / 2.0f + 0.01f;
	float pitchMax = static_cast<float>(std::numbers::pi) / 2.0f - 0.01f;
	// 速度係数
	float orbitSpeed = 0.001f; // オービット回転の速さ
	float panSpeed = 0.025f;    // パン移動の速さ
	float zoomSpeed = 0.01f;   // ズームの速さ
	// 距離の下限
	float distanceMin = -50.0f;
	// 手振れの補正
	float mouseDeltaThreshold = 2.0f; // 操作時にこの値以下は無視

public:

	void Initialize(float fovY, float aspectRatio, float nearZ, float farZ) override;
	void Update() override;
	void Rotate();
	void Zoom();
	void Translate();
	Matrix4x4 CalcWVP(const Matrix4x4& worldMatrix) const override;
	void ReCalcViewMatrix();

	// セッター
	void SetFovY(float fovY) { fovY_ = fovY; }
	void SetAspectRatio(float aspectRatio) { aspectRatio_ = aspectRatio; }
	void SetNearZ(float nearZ) { nearZ_ = nearZ; }
	void SetFarZ(float farZ) { farZ_ = farZ; }
	void SetScale(const Vector3& scale) { transform_.scale = scale; }
	void SetRotation(const Vector3& rotate) { transform_.rotation = rotate; }
	void SetTranslation(const Vector3& translate) { transform_.translation = translate; }

	// ゲッター
	float GetFovY() const { return fovY_; }
	float GetAspectRatio() const { return aspectRatio_; }
	float GetNearZ() const { return nearZ_; }
	float GetFarZ() const { return farZ_; }
	const Vector3& GetScale() const { return transform_.scale; }
	const Vector3& GetRotation() const { return transform_.rotation; }
	const Vector3& GetTranslation() const { return transform_.translation; }
};
