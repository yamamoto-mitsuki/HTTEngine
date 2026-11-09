#pragma once
#include "HTTEngine/Math/Vector2.h"
#include "HTTEngine/Math/Vector3.h"
#include "HTTEngine/Math/Vector4.h"
#include "HTTEngine/Math/Matrix4x4.h"
#include "HTTEngine/Math/Transform.h"
#include "HTTEngine/Math/Geometry/Ray.h"


/// <summary>
/// カメラの基底クラス
/// <para>オーバーライドせずとも機能はする</para>
/// </summary>
class Camera {
public:
	virtual ~Camera() = default;

	/// <summary>
	/// 初期化。生成後に1回呼ぶ
	/// </summary>
	/// <param name="fovY">縦方向の視野角(ラジアン)。例: 0.45f</param>
	/// <param name="aspectRatio">アスペクト比。例: width / height</param>
	/// <param name="nearZ">近クリップ面。例: 0.1f</param>
	/// <param name="farZ">遠クリップ面。例: 100.0f</param>
	virtual void Initialize(float fovY, float aspectRatio, float nearZ, float farZ);

	/// <summary>
	/// 毎フレーム呼ぶ。TransformからView・Projection行列を更新する
	/// </summary>
	virtual void Update();

	/// <summary>
	/// このカメラの視錐台をデバッグ表示する（別カメラの視点から見たときだけ意味がある）
	/// </summary>
	/// <param name="viewCamera">描画に使うカメラ。デバッグカメラを渡す</param>
	void DrawFrustum(Camera* viewCamera) const;

	/// <summary>
	/// ワールド行列とWVP行列を計算して返す
	/// </summary>
	/// <param name="worldMatrix">オブジェクトのワールド行列</param>
	virtual Matrix4x4 CalcWVP(const Matrix4x4& worldMatrix) const;

	/// <summary>
	/// 画面の位置から、カメラの奥へ向かうレイを作る（マウスで地面・物を指す）
	/// <para>始点は近クリップ面の上、向きは長さ1。最後の Update の行列を使う</para>
	/// </summary>
	/// <param name="ndc">画面の位置（InputManager::GetMousePositionInGame() の値。真ん中0、右と上が+、端が±1）</param>
	Ray ScreenPointToRay(const Vector2& ndc) const;

	/// <summary>
	/// ワールドの点が、画面のどこに写るか（ScreenPointToRay の逆。エディタで点に印を描く・マウスが点の近くか調べる）
	/// <para>最後の Update の行列を使う</para>
	/// </summary>
	/// <param name="ndc">画面の位置が入る（真ん中0、右と上が+、端が±1。画面の外なら ±1 を超える）</param>
	/// <returns>写るか（カメラの後ろの点は false。ndc は変えない）</returns>
	bool WorldToScreenPoint(const Vector3& world, Vector2* ndc) const;

    // セッター
	void SetFovY(float fovY) { fovY_ = fovY; }
	void SetAspectRatio(float aspectRatio) { aspectRatio_ = aspectRatio; }
	void SetNearZ(float nearZ) { nearZ_ = nearZ; }
	void SetFarZ(float farZ) { farZ_ = farZ; }
	void SetScale(const Vector3& scale) { transform_.scale = scale; }
	void SetRotation(const Vector3& rotation) { transform_.rotation = rotation; }
	void SetTranslation(const Vector3& translation) { transform_.translation = translation; }

	// ゲッター
	float GetFovY() const { return fovY_; }
	float GetAspectRatio() const { return aspectRatio_; }
	float GetNearZ() const { return nearZ_; }
	float GetFarZ() const { return farZ_; }
	const Vector3& GetScale() const { return transform_.scale; }
	const Vector3& GetRotation() const { return transform_.rotation; }
	const Vector3& GetTranslation() const { return transform_.translation; }
	const Transform& GetTransform() const { return transform_; }
	const Matrix4x4& GetViewMatrix() const { return viewMatrix_; }
	const Matrix4x4& GetProjectionMatrix() const { return projectionMatrix_; }
	Matrix4x4 GetViewProjectionMatrix() const { return Multiply(viewMatrix_, projectionMatrix_); }

protected:
	float fovY_ = 0.45f;
	float aspectRatio_ = 1.0f;
	float nearZ_ = 0.1f;
	float farZ_ = 100.0f;
	Matrix4x4 viewMatrix_ = {};
	Matrix4x4 projectionMatrix_ = {};
	// カメラのTransform（位置・回転・スケール）
	Transform transform_ = {
	    {1.0f, 1.0f, 1.0f },
        {0.0f, 0.0f, 0.0f },
        {0.0f, 0.0f, -5.0f},
	};
	float debugFarDistance = 60.0f;   // 錐台をどこまで描くか
	Vector4 debugColor = {1.0f, 1.0f, 0.0f, 1.0f}; // 表示色（黄色）

	static constexpr float kMinScreenW = 1e-3f; // WorldToScreenPoint：奥行き（w）がこれより小さい点は、カメラの後ろとして写さない
};