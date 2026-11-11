#ifdef USE_IMGUI
#include "CameraGizmo.h"

#include <array>
#include <cstdint>
#include <span>

#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/Graphics/Renderer/Renderer.h"
#include "HTTEngine/Graphics/Texture/TextureManager.h"
#include "HTTEngine/Math/Geometry/PathCurve.h"
#include "HTTEngine/Systems/Camera/CameraFollow.h"
#include "HTTEngine/Systems/Camera/CameraPath.h"
#include "HTTEngine/Systems/Camera/CameraSystem.h"

namespace {
constexpr float kNoFadeStart = 10000.0f; // 距離でフェードさせないための値
constexpr float kNoFadeEnd = 20000.0f;
constexpr Vector4 kPathColor = {0.3f, 0.8f, 1.0f, 1.0f};     // 線の色（水色）
constexpr Vector4 kPositionColor = {1.0f, 0.8f, 0.2f, 1.0f}; // 線の上の今の位置に立てる線と、アイコンからそこへ引く線の色
constexpr float kPositionMarkHeight = 0.5f;                  // 今の位置に立てる線の高さ（m）
// Camera Follow の範囲の色（Inspector の Show Gizmo の説明と合わせる）
constexpr Vector4 kDeadZoneColor = {0.35f, 1.0f, 0.45f, 1.0f}; // Dead Zone（緑）
constexpr Vector4 kSoftZoneColor = {0.35f, 0.6f, 1.0f, 1.0f};  // Soft Zone（青）
constexpr Vector4 kFocusColor = {1.0f, 0.85f, 0.2f, 1.0f};     // focus の印と、追う物から focus へ引く線（黄）
constexpr Vector4 kBoundsColor = {1.0f, 0.35f, 0.3f, 1.0f};    // Use Bounds の範囲（赤）

uint32_t iconTextureHandle = 0;       // カメラのアイコン
bool isShowIcons = true;              // アイコンを出すか
bool isShowPaths = true;              // Camera Path の線を出すか
bool isShowFollowZones = true;        // Camera Follow の範囲の線を出すか
Renderer::LineListConfig lines;       // 線の置き場（配列のメモリを使い回す）
Renderer::LineListConfig followLines; // Camera Follow のワールドの線の置き場（Scene・Game ビュー。Scene ビューだけの lines と分ける）
Renderer::LineListConfig screenLines; // Camera Follow の Screen の範囲の置き場（Game ビューだけ）
PathCurve pathLine;                   // 描く線（使い回す）

// ===== 四角（4つの角を順に結ぶ）=====
void PushRect(std::vector<Renderer::LineSegment>& out, const std::array<Vector3, 4>& corners, const Vector4& color) {
	for (size_t i = 0; i < corners.size(); ++i) {
		out.push_back({corners[i], corners[(i + 1) % corners.size()], color});
	}
}

// ===== Camera Follow 1つ分の線を積む =====
// 範囲は focus を通る面の上にあるので、このカメラから見ると画面の上の範囲にぴったり重なる（Game ビューでは画面の四角に見える）
// World の範囲・focus・Bounds はワールドの物なので、Scene・Game ビューの両方に出す
// Screen の範囲は「このカメラから見た画面の四角」なので、ほかのカメラから見ても意味がない
// → Game ビューだけ（GameViewGuide の層）に、Game ビューを写しているカメラ（メインカメラ）の物だけを出す
void PushFollowGuide(const CameraFollow::Guide& guide, CameraFollowZoneSpace zoneSpace, bool isMainCamera) {
	std::vector<Renderer::LineSegment>& out = followLines.lines;
	switch (zoneSpace) {
	case CameraFollowZoneSpace::World:
		PushRect(out, guide.softZone, kSoftZoneColor);
		PushRect(out, guide.deadZone, kDeadZoneColor);
		break;
	case CameraFollowZoneSpace::Screen:
		if (isMainCamera) {
			PushRect(screenLines.lines, guide.softZone, kSoftZoneColor);
			PushRect(screenLines.lines, guide.deadZone, kDeadZoneColor);
		}
		break;
	}
	// focus の十字と、追う物から focus へ（Offset・先読みでどれだけずれているか）
	const Vector3 h = guide.horizontal * guide.markSize;
	const Vector3 v = guide.vertical * guide.markSize;
	out.push_back({guide.focus - h, guide.focus + h, kFocusColor});
	out.push_back({guide.focus - v, guide.focus + v, kFocusColor});
	out.push_back({guide.target, guide.focus, kFocusColor});
	if (guide.bounds.has_value()) {
		PushRect(out, *guide.bounds, kBoundsColor);
	}
}

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
	lines.isGizmo = true; // ポストエフェクトの後に、選んだ色のまま描く
	// Camera Follow の範囲は Game ビューにも出す（画面のどこで追い始めるかを、遊ぶときの絵の上で見る）。物の後ろでも見えるように深度で隠さない
	followLines.fadeStartDistance = kNoFadeStart;
	followLines.fadeEndDistance = kNoFadeEnd;
	followLines.layer = DrawLayer::Default;
	followLines.depthMode = DepthMode::Disable;
	followLines.isGizmo = true;
	screenLines.fadeStartDistance = kNoFadeStart;
	screenLines.fadeEndDistance = kNoFadeEnd;
	screenLines.layer = DrawLayer::GameViewGuide;
	screenLines.depthMode = DepthMode::Disable;
	screenLines.isGizmo = true;
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
	if (isShowFollowZones) {
		followLines.camera = camera;
		followLines.lines.clear();
		screenLines.camera = camera;
		screenLines.lines.clear();
		const Handle<Entity> mainCamera = CameraSystem::GetMainCameraEntity();
		EntityManager::ForEach<CameraFollowComponent>([mainCamera](Handle<Entity> entity, const CameraFollowComponent& follow) {
			if (!follow.enabled || !follow.showGizmo || !EntityManager::IsActiveInHierarchy(entity)) {
				return;
			}
			if (const std::optional<CameraFollow::Guide> guide = CameraFollow::CalcGuide(entity, follow)) {
				PushFollowGuide(*guide, follow.zoneSpace, entity == mainCamera);
			}
		});
		Renderer::DrawLines(followLines);
		Renderer::DrawLines(screenLines);
		followLines.camera = nullptr;
		screenLines.camera = nullptr;
	}
}

//=============================================================================
// 表示の設定
//=============================================================================
bool& CameraGizmo::GetShowIcons() { return isShowIcons; }

bool& CameraGizmo::GetShowPaths() { return isShowPaths; }

bool& CameraGizmo::GetShowFollowZones() { return isShowFollowZones; }
#endif