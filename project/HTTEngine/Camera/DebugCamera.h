#pragma once
#include <numbers>

#include "HTTEngine/Camera/Camera.h"
#include "HTTEngine/Math/Vector2.h"
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
	float rotateSpeed = 0.001f;    // 向きを変える速さ（マウスが数えた数 1 あたりのラジアン）
	float panSpeed = 0.025f;       // 平行移動の速さ（マウスが数えた数 1 あたりの距離）
	float dollyDragSpeed = 0.05f;  // ドラッグで前後に動く速さ（マウスが数えた数 1 あたりの距離）
	float dollyWheelSpeed = 1.2f;  // ホイールで前後に動く速さ（1 目盛りあたりの距離）
	// 手振れの補正
	float mouseDeltaThreshold = 2.0f; // 向きを変えるとき、この値より小さい動きは無視

	/// <summary>
	/// 1フレームの操作の量（EditorViewport が、EditorKeymap の割り当てとマウスの動きから作る）
	/// </summary>
	struct Input {
		Vector2 rotate = {0.0f, 0.0f}; // 向きを変える量（マウスが数えた数。右・下が+）
		Vector2 pan = {0.0f, 0.0f};    // 平行移動の量（マウスが数えた数。右・下が+）
		float dollyDrag = 0.0f;        // ドラッグで前後に動く量（マウスが数えた数。前が+）
		float dollyWheel = 0.0f;       // ホイールで前後に動く量（目盛り。前が+）
	};

public:

	void Initialize(float fovY, float aspectRatio, float nearZ, float farZ) override;
	void Update() override;
	// 操作の量で動かして、ビュー行列を作り直す（どのキー・ボタンで動かすかは、呼ぶ側が決める）
	void ApplyInput(const Input& input);
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

private:
	void Rotate(Vector2 amount);       // 向きを変える
	void Pan(const Vector2& amount);   // 上下左右に動く
	void Dolly(float distance);        // 向いている方へ前後に動く
};
