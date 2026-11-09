#ifdef USE_IMGUI
#include "CameraGizmo.h"

#include <cstdint>
#include <span>

#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/Graphics/Renderer/Renderer.h"
#include "HTTEngine/Graphics/Texture/TextureManager.h"
#include "HTTEngine/Math/Geometry/PathCurve.h"
#include "HTTEngine/Systems/Camera/CameraPath.h"
#include "HTTEngine/Systems/Camera/CameraSystem.h"

namespace {
constexpr float kNoFadeStart = 10000.0f; // 距離でフェードさせないための値
constexpr float kNoFadeEnd = 20000.0f;
constexpr Vector4 kPathColor = {0.3f, 0.8f, 1.0f, 1.0f};     // 線の色（水色）
constexpr Vector4 kPositionColor = {1.0f, 0.8f, 0.2f, 1.0f}; // 線の上の今の位置に立てる線と、アイコンからそこへ引く線の色
constexpr float kPositionMarkHeight = 0.5f;                  // 今の位置に立てる線の高さ（m）

uint32_t iconTextureHandle = 0; // カメラのアイコン
bool isShowIcons = true;        // アイコンを出すか
bool isShowPaths = true;        // Camera Path の線を出すか
Renderer::LineListConfig lines; // 線の置き場（配列のメモリを使い回す）
PathCurve pathLine;             // 描く線（使い回す）

// ===== アイコンを1つ描く =====
void DrawIcon(const Vector3& position, Camera* camera) {
	Renderer::Rect3dConfig icon;
	icon.textureHandle = iconTextureHandle;
	icon.shadingType = ShadingType::Unlit;
	icon.blendMode = BlendMode::Normal;
	icon.rasterizerType = RasterizerType::SolidNone;
	icon.depthMode = DepthMode::TestNoWrite; // 半透明として描く（深度を書かない）
	icon.isBillboard = true;
	icon.camera = camera;
	icon.layer = DrawLayer::EditorGizmo;
	icon.transform.translation = position;
	Renderer::DrawRect3d(icon);
}

// ===== Camera Path の線と、今の位置の印を積む =====
// アイコンは Transform の位置（ギズモ・クリックと同じ所）なので、写す位置へ線を引いて、どのカメラの位置か分かるようにする
void PushPath(Handle<Entity> entity, const CameraPathComponent& path, const TransformComponent& transform) {
	CameraPath::BuildLine(path, pathLine);
	const std::span<const Vector3> polyline = pathLine.GetPolyline();
	for (size_t i = 1; i < polyline.size(); ++i) {
		lines.lines.push_back({polyline[i - 1], polyline[i], kPathColor});
	}
	if (!pathLine.IsEmpty()) {
		const Vector3 position = CameraSystem::CalcCameraPosition(entity);
		lines.lines.push_back({
		    position, position + Vector3{0.0f, kPositionMarkHeight, 0.0f},
               kPositionColor
        });
		lines.lines.push_back({GetWorldPosition(transform), position, kPositionColor});
	}
}
} // namespace

//=============================================================================
// 初期化
//=============================================================================
void CameraGizmo::Initialize() {
	iconTextureHandle = TextureManager::Load("HTTEngine/Resources/Textures/Camera.png");
	lines.fadeStartDistance = kNoFadeStart;
	lines.fadeEndDistance = kNoFadeEnd;
	lines.layer = DrawLayer::EditorGizmo;
}

//=============================================================================
// 描く
//=============================================================================
void CameraGizmo::Draw(Camera* camera) {
	if (isShowIcons) {
		// Transform の位置に出す（Camera Path の写す位置に出すと、ギズモが何も無い所に出て、線の上のアイコンは別の Entity に見える）
		EntityManager::ForEach<CameraComponent>([camera](Handle<Entity> entity, const CameraComponent&) {
			const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
			if (transform != nullptr && EntityManager::IsActiveInHierarchy(entity)) {
				DrawIcon(GetWorldPosition(*transform), camera);
			}
		});
	}
	if (isShowPaths) {
		lines.camera = camera;
		lines.lines.clear();
		EntityManager::ForEach<CameraPathComponent>([](Handle<Entity> entity, const CameraPathComponent& path) {
			const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
			if (path.enabled && transform != nullptr && EntityManager::IsActiveInHierarchy(entity)) {
				PushPath(entity, path, *transform);
			}
		});
		Renderer::DrawLines(lines); // 線が0本なら中で何もしない
		lines.camera = nullptr;     // フレームをまたいで持たない
	}
}

//=============================================================================
// 表示の設定
//=============================================================================
bool& CameraGizmo::GetShowIcons() { return isShowIcons; }

bool& CameraGizmo::GetShowPaths() { return isShowPaths; }
#endif