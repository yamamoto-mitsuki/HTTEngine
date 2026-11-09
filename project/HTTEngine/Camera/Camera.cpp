#include "Camera.h"

#include <array>

#include "HTTEngine/Graphics/Renderer/Renderer.h"


//=============================================================================
// 初期化
//=============================================================================
void Camera::Initialize(float fovY, float aspectRatio, float nearZ, float farZ) {
	fovY_ = fovY;
	aspectRatio_ = aspectRatio;
	nearZ_ = nearZ;
	farZ_ = farZ;
	// 初期行列を計算しておく
	Update();
}



//=============================================================================
// 更新
//=============================================================================
void Camera::Update() {
	Matrix4x4 cameraMatrix = MakeAffineMatrix(transform_.scale, transform_.rotation, transform_.translation);
	viewMatrix_ = Inverse(cameraMatrix);
	projectionMatrix_ = MakePerspectiveFovMatrix(fovY_, aspectRatio_, nearZ_, farZ_);
}


//=============================================================================
// 視錐台のデバッグ表示
//=============================================================================
void Camera::DrawFrustum(Camera* viewCamera) const {
	// 早期リターン
	if (!viewCamera || viewCamera == this) {
		return; 
	}

	// --- 錐台の8頂点をNDCから逆変換で求める ---
	// farZ をそのまま使うと巨大になるので、手前を切った射影行列を作り直す
	Matrix4x4 proj = MakePerspectiveFovMatrix(fovY_, aspectRatio_, nearZ_, debugFarDistance);
	Matrix4x4 invViewProj = Inverse(viewMatrix_ * proj);
	// DirectXのNDCは x,y が -1〜1、z が 0〜1
	const std::array<Vector3, 8> kNdc = {{
	    {-1.0f, -1.0f, 0.0f},
        {1.0f,  -1.0f, 0.0f},
        {1.0f,  1.0f,  0.0f},
        {-1.0f, 1.0f,  0.0f}, // 近平面
	    {-1.0f, -1.0f, 1.0f},
        {1.0f,  -1.0f, 1.0f},
        {1.0f,  1.0f,  1.0f},
        {-1.0f, 1.0f,  1.0f}, // 遠平面
	}};
	std::array<Vector3, 8> corner{};
	for (int i = 0; i < 8; ++i) {
		corner[i] = MathUtility::TransformPoint(kNdc[i], invViewProj);
	}

	// --- 12本の辺 ---
	const std::array<std::array<int, 2>, 12> kEdge = {{
	    {0, 1},
        {1, 2},
        {2, 3},
        {3, 0}, // 近平面
	    {4, 5},
        {5, 6},
        {6, 7},
        {7, 4}, // 遠平面
	    {0, 4},
        {1, 5},
        {2, 6},
        {3, 7}, // 側面
	}};
	Renderer::LineListConfig config;
	config.camera = viewCamera;
	config.color = debugColor;
	config.fadeStartDistance = 10000.0f; // フェードさせない
	config.fadeEndDistance = 20000.0f;
	for (const auto& e : kEdge) {
		config.lines.push_back({
		    corner[e[0]], corner[e[1]], {1.0f, 1.0f, 1.0f, 1.0f}
        });
	}

	// --- カメラ位置に十字 ---
	const Vector3& pos = transform_.translation;
	constexpr float kCrossSize = 1.0f;
	config.lines.push_back({
	    pos - Vector3{kCrossSize, 0.0f, 0.0f},
        pos + Vector3{kCrossSize, 0.0f, 0.0f},
        {1.0f, 1.0f, 1.0f, 1.0f}
    });
	config.lines.push_back({
	    pos - Vector3{0.0f, kCrossSize, 0.0f},
        pos + Vector3{0.0f, kCrossSize, 0.0f},
        {1.0f, 1.0f, 1.0f, 1.0f}
    });
	config.lines.push_back({
	    pos - Vector3{0.0f, 0.0f, kCrossSize},
        pos + Vector3{0.0f, 0.0f, kCrossSize},
        {1.0f, 1.0f, 1.0f, 1.0f}
    });

	Renderer::DrawLines(config);
}

Matrix4x4 Camera::CalcWVP(const Matrix4x4& worldMatrix) const {
	return Multiply(worldMatrix, Multiply(viewMatrix_, projectionMatrix_));
}


//=============================================================================
// 画面の位置からレイを作る
//=============================================================================
Ray Camera::ScreenPointToRay(const Vector2& ndc) const {
	// 画面の同じ位置にある「近い面の点（z=0）」と「遠い面の点（z=1）」を、ビュー・プロジェクションの逆行列でワールドへ戻す
	const Matrix4x4 inverseViewProjection = Inverse(Multiply(viewMatrix_, projectionMatrix_));
	const Vector3 nearPoint = MathUtility::TransformPoint({ndc.x, ndc.y, 0.0f}, inverseViewProjection);
	const Vector3 farPoint = MathUtility::TransformPoint({ndc.x, ndc.y, 1.0f}, inverseViewProjection);
	// 正射影でも同じ式で動く（近い点が画面の位置ごとにずれ、向きはどこでも同じになる）
	return {nearPoint, Normalize(farPoint - nearPoint)};
}


//=============================================================================
// ワールドの点が、画面のどこに写るか
//=============================================================================
bool Camera::WorldToScreenPoint(const Vector3& world, Vector2* ndc) const {
	// ビュー・プロジェクション行列を掛けた (x, y, z, w) を w で割ると画面の位置になる。w はカメラからの奥行き（エンジンと同じ「v × 行列」で、4列目が w）
	const Matrix4x4 m = Multiply(viewMatrix_, projectionMatrix_);
	const float w = world.x * m.m[0][3] + world.y * m.m[1][3] + world.z * m.m[2][3] + m.m[3][3];
	if (w <= kMinScreenW) {
		return false; // カメラの後ろ（w で割ると、裏返って変な所に出る）
	}
	ndc->x = (world.x * m.m[0][0] + world.y * m.m[1][0] + world.z * m.m[2][0] + m.m[3][0]) / w;
	ndc->y = (world.x * m.m[0][1] + world.y * m.m[1][1] + world.z * m.m[2][1] + m.m[3][1]) / w;
	return true;
}