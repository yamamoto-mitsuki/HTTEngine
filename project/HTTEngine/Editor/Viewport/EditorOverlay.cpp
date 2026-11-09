#ifdef USE_IMGUI
#include "HTTEngine/Editor/Viewport/EditorOverlay.h"
#include <array>
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Camera/Camera.h"

// 静的メンバ変数
std::unique_ptr<EditorOverlay> EditorOverlay::instance_ = nullptr;


//=============================================================================
// 初期化 / 解放
//=============================================================================
// ===== 初期化 =====
void EditorOverlay::Initialize() {
	HTT_ASSERT_MSG(instance_ == nullptr, "Initialize()が2回以上呼ばれています");
	instance_ = std::make_unique<EditorOverlay>(ConstructorKey());
	LogManager::Log("Initialized");
}

// ===== 解放 =====
void EditorOverlay::Release() {
	instance_.reset();
	LogManager::Log("Released");
}


//=============================================================================
// オーバーレイを描画
//=============================================================================
void EditorOverlay::Draw(ImVec2 imageMin, ImVec2 imageMax, ImVec2 imageSize) {
	HTT_ASSERT_MSG(instance_, "Initialize()を先に呼んでください");
	const std::shared_ptr<Camera> cam = GetActiveCamera();
	if (!cam) {
		return; // 二重呼び出し解消＋nullガード
	}
		
	ImDrawList* dl = ImGui::GetWindowDrawList();
	// ImGuizmo の準備（毎フレーム1回。右上の軸と、SceneGizmo の移動・回転・拡縮の両方で使うので、軸を出さない時も呼ぶ）
	ImGuizmo::BeginFrame();
	ImGuizmo::SetDrawlist(dl); // 描き先を Scene ウィンドウにする（ほかのウィンドウの後ろに隠れないように）
	ImGuizmo::SetRect(imageMin.x, imageMin.y, imageSize.x, imageSize.y);
	// 軸
	if (instance_->isShowAxis_) {
		DrawAxisGizmo(dl, cam.get(), imageMin, imageMax, imageSize);
	}
}


//=============================================================================
// DrawSettings
// 任意の ImGui ウィンドウ内から呼ぶ ON/OFF コントロール
//=============================================================================
void EditorOverlay::DrawSettings() {
	HTT_ASSERT_MSG(instance_, "Initialize()を先に呼んでください");
	ImGui::SeparatorText("Editor Overlay");
	ImGui::Checkbox("Axis Gizmo", &instance_->isShowAxis_);
}


//=============================================================================
// 軸表示
//=============================================================================
void EditorOverlay::DrawAxisGizmo(ImDrawList* dl, const Camera* cam, ImVec2 imageMin, ImVec2 imageMax, ImVec2 imageSize) {
	HTT_ASSERT_MSG(cam, "EditorOverlay::SetActiveCameraを呼び出してください。");
	(void)dl; // 描き先はDraw
	// 列優先の行列に変換
	auto toColMajor = [](const Matrix4x4& m, std::array<float, 16>& out) {
		for (int r = 0; r < 4; ++r) {
			for (int c = 0; c < 4; ++c) {
				out[c * 4 + r] = m.m[r][c];
			}
		}
	};

	std::array<float, 16> viewF{};
	std::array<float, 16> projF{};
	toColMajor(cam->GetViewMatrix(), viewF);
	toColMajor(cam->GetProjectionMatrix(), projF);

	std::array<float, 16> viewCopy = viewF;

	// 大きさ、位置
	const float gizmoSizePx = std::max(60.0f, imageSize.x * 0.08f);
	const ImVec2 gizmoSize = {gizmoSizePx, gizmoSizePx};
	const ImVec2 gizmoPos = {imageMax.x - gizmoSizePx, imageMin.y};

	std::array<float, 16> identity = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

	ImGuizmo::ViewManipulate(
	    viewCopy.data(), 
		projF.data(), 
		ImGuizmo::ROTATE,
		ImGuizmo::LOCAL,
	    identity.data(), 
	    8.0f, gizmoPos, gizmoSize, 
		0x10101080);
}
#endif