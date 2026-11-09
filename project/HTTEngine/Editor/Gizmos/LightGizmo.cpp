#include "LightGizmo.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Editor/Gizmos/GizmoLines.h"
#include "HTTEngine/Graphics/Texture/TextureManager.h"
#include "HTTEngine/Math/MathUtility.h"

// 静的メンバ変数
uint32_t LightGizmo::directionalIconTextureHandle_ = 0;
uint32_t LightGizmo::pointIconTextureHandle_ = 0;
uint32_t LightGizmo::spotIconTextureHandle_ = 0;
LightGizmoFlags LightGizmo::globalFlags_;
Camera* LightGizmo::camera_ = nullptr;
bool LightGizmo::isRecording_ = false;
Renderer::LineListConfig LightGizmo::rangeLines_;

namespace {
constexpr uint32_t kCircleDivision = 32;         // 円を何本の線で描くか
constexpr uint32_t kSpotEdgeCount = 4;           // スポットライトの頂点から円へ引く線の本数
constexpr Vector4 kRangeColor = {1.0f, 0.647f, 0.0f, 1.0f};     // 範囲の線の色（オレンジ）
constexpr Vector4 kDirectionColor = {1.0f, 0.878f, 0.4f, 1.0f}; // 平行光源の線の色（黄色）
constexpr uint32_t kSunRayCount = 8;             // 平行光源の円盤から伸ばす線の本数
constexpr float kSunRadius = 0.5f;               // 平行光源の円盤の半径
constexpr float kSunRayLength = 2.0f;            // 平行光源の線の長さ
constexpr float kNoFadeStart = 10000.0f;         // 距離でフェードさせないための値
constexpr float kNoFadeEnd = 20000.0f;

/// <summary>
/// 向き（長さ1）と直交する2本の軸を作る（向きに垂直な円を描く平面）
/// <para>外積は平行なベクトル同士だと0になるので、向きとほぼ平行なら別の軸を基準にする</para>
/// </summary>
void MakePerpendicularAxes(const Vector3& direction, Vector3& axisA, Vector3& axisB) {
	Vector3 reference = (std::abs(direction.y) < 0.99f) ? Vector3{0.0f, 1.0f, 0.0f} : Vector3{1.0f, 0.0f, 0.0f};
	axisA = Normalize(Cross(reference, direction));
	axisB = Cross(direction, axisA); // 直交する単位ベクトル同士の外積なので、長さは1
}
} // namespace


//=============================================================================
// 初期化
//=============================================================================
void LightGizmo::Initialize() {
	directionalIconTextureHandle_ = TextureManager::Load("HTTEngine/Resources/Textures/directionalLight.png");
	pointIconTextureHandle_ = TextureManager::Load("HTTEngine/Resources/Textures/pointLight.png");
	spotIconTextureHandle_ = TextureManager::Load("HTTEngine/Resources/Textures/spotLight.png");
	// フェードしない値にする
	rangeLines_.fadeStartDistance = kNoFadeStart;
	rangeLines_.fadeEndDistance = kNoFadeEnd;
	rangeLines_.layer = DrawLayer::EditorGizmo; // Scene ビューだけに描く
}


//=============================================================================
// 開始 / 終了
//=============================================================================
void LightGizmo::Begin(Camera* camera) {
	HTT_ASSERT_MSG(!isRecording_, "LightGizmo::End を呼ぶ前に Begin が呼ばれました");
	isRecording_ = true;
	camera_ = camera;
	rangeLines_.camera = camera;
	rangeLines_.lines.clear(); // 中身だけ消す。確保済みのメモリは残るので、次から再確保が起きにくい
}

void LightGizmo::End() {
	HTT_ASSERT_MSG(isRecording_, "LightGizmo::Begin を呼ばずに End が呼ばれました");
	Renderer::DrawLines(rangeLines_); // 線が0本なら中で何もしない
	camera_ = nullptr;                // フレームをまたいで持たない
	rangeLines_.camera = nullptr;
	isRecording_ = false;
}


//=============================================================================
// アイコン（共通）
//=============================================================================
void LightGizmo::DrawIcon(const Vector3& position, uint32_t textureHandle) {
	Renderer::Rect3dConfig icon;
	icon.textureHandle = textureHandle;
	icon.shadingType = ShadingType::Unlit;
	icon.blendMode = BlendMode::Normal;
	icon.rasterizerType = RasterizerType::SolidNone;
	icon.depthMode = DepthMode::TestNoWrite; // 半透明として描く（深度を書かない。Skyboxの後に奥から順に描かれる）
	icon.isBillboard = true;
	icon.camera = camera_;
	icon.layer = DrawLayer::EditorGizmo; // Scene ビューだけに描く
	icon.transform.translation = position;
	Renderer::DrawRect3d(icon);
}


//=============================================================================
// 平行光源
//=============================================================================
void LightGizmo::AddDirectionalLight(const Vector3& position, const Vector3& direction, const DirectionalLightComponent& light) {
	HTT_ASSERT_MSG(isRecording_, "LightGizmo::Begin を呼んでから AddDirectionalLight を呼んでください");

	// 全体とライトごと、両方ONのときだけ出す
	bool showIcon = globalFlags_.showIcon && light.gizmo.showIcon;
	bool showRange = globalFlags_.showRange && light.gizmo.showRange;

	// --- アイコン（太陽）---
	if (showIcon) {
		DrawIcon(position, directionalIconTextureHandle_);
	}

	// --- 向き。円盤と、円盤の縁と中心から向きの方へ伸びる線（Unityの平行光源のギズモと同じ形）---
	if (showRange) {
		Vector3 axisA;
		Vector3 axisB;
		MakePerpendicularAxes(direction, axisA, axisB);
		GizmoLines::PushCircle(rangeLines_.lines, position, kSunRadius, axisA, axisB, kDirectionColor, kCircleDivision);
		rangeLines_.lines.push_back({position, position + direction * kSunRayLength, kDirectionColor});
		for (uint32_t i = 0; i < kSunRayCount; ++i) {
			float angle = MathUtility::kTwoPi * static_cast<float>(i) / static_cast<float>(kSunRayCount);
			Vector3 start = position + axisA * (std::cos(angle) * kSunRadius) + axisB * (std::sin(angle) * kSunRadius);
			rangeLines_.lines.push_back({start, start + direction * kSunRayLength, kDirectionColor});
		}
	}
}


//=============================================================================
// ポイントライト
//=============================================================================
void LightGizmo::AddPointLight(const Vector3& position, const PointLightComponent& light) {
	HTT_ASSERT_MSG(isRecording_, "LightGizmo::Begin を呼んでから AddPointLight を呼んでください");

	// 全体とライトごと、両方ONのときだけ出す
	bool showIcon = globalFlags_.showIcon && light.gizmo.showIcon;
	bool showRange = globalFlags_.showRange && light.gizmo.showRange;

	// --- アイコン ---
	if (showIcon) {
		DrawIcon(position, pointIconTextureHandle_);
	}

	// --- 光の届く範囲。3方向の円を重ねて球に見せる。描くのはEndでまとめて ---
	if (showRange) {
		GizmoLines::PushWireSphere(rangeLines_.lines, position, light.radius, kRangeColor, kCircleDivision); // XY平面
	}
}


//=============================================================================
// スポットライト
//=============================================================================
void LightGizmo::AddSpotLight(const Vector3& position, const Vector3& direction, const SpotLightComponent& light) {
	HTT_ASSERT_MSG(isRecording_, "LightGizmo::Begin を呼んでから AddSpotLight を呼んでください");

	bool showIcon = globalFlags_.showIcon && light.gizmo.showIcon;
	bool showRange = globalFlags_.showRange && light.gizmo.showRange;

	// --- アイコン ---
	if (showIcon) {
		DrawIcon(position, spotIconTextureHandle_);
	}

	// --- 光の届く範囲（円錐） ---
	if (showRange) {
		// 向きと直交する2本の軸を作る（円を描く平面）。directionはLightSystemで正規化済み
		Vector3 axisA;
		Vector3 axisB;
		MakePerpendicularAxes(direction, axisA, axisB);

		// 底の円：頂点から斜めにrange進んだ所（光が届く距離の端）
		float outerAngle = std::clamp(light.outerAngle, 0.0f, SpotLightComponent::kMaxAngle) * MathUtility::kDegToRad;
		float radius = light.range * std::sin(outerAngle);                            // 円の半径
		Vector3 center = position + direction * (light.range * std::cos(outerAngle)); // 円の中心
		GizmoLines::PushCircle(rangeLines_.lines, center, radius, axisA, axisB, kRangeColor, kCircleDivision);

		// 頂点から円へ線を引いて、円錐に見せる
		for (uint32_t i = 0; i < kSpotEdgeCount; ++i) {
			float angle = MathUtility::kTwoPi * static_cast<float>(i) / static_cast<float>(kSpotEdgeCount);
			Vector3 edge = center + axisA * (std::cos(angle) * radius) + axisB * (std::sin(angle) * radius);
			rangeLines_.lines.push_back({position, edge, kRangeColor});
		}
	}
}