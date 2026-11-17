#include "ShadowRenderer.h"

#include <algorithm>
#include <cmath>

#include "HTTEngine/Camera/Camera.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Graphics/GPU/DirectXCommon.h"
#include "HTTEngine/Graphics/Pipeline/PSOManager.h"
#include "HTTEngine/Graphics/Pipeline/ShaderConstants.h"
#include "HTTEngine/Graphics/Profiling/GPUScope.h"
#include "HTTEngine/Graphics/RenderTarget/RenderTexture.h"
#include "HTTEngine/Graphics/Renderer/RenderContext.h"
#include "HTTEngine/Graphics/Renderer/RenderQueue.h"

namespace {
// 影の量の画面を作るCS
constexpr const char* kShadowMaskCS = "ShadowMaskCS";
// ライトの方へ、球の真ん中から半径の何倍まで伸ばすか（球の外にあるライト側の影も落とすまで）
constexpr float kCasterReach = 3.0f;

// ===== ShadowMask.CSのgShadowMask =====
struct ShadowMaskParams {
	Matrix4x4 lightViewProj = MakeIdentity4x4(); // ワールド → ライトから見た画面
	uint32_t depthIndex = 0;                     // 3Dの絵の深度
	uint32_t shadowMapIndex = 0;                 // 影の深度の絵
	uint32_t shadowMapSize = 0;                  // 影の深度の絵の1辺（ピクセル）
	float depthBias = 0.0f;                      // 比べる前に引く深度（ライトの深度を0~1で）
	uint32_t maskWidth = 0;                      // 影の量の画面の幅（ピクセル）
	uint32_t maskHeight = 0;                     // 影の量の画面の高さ（ピクセル）
	float strength = 1.0f;                       // 影の濃さ
	float distance = 0.0f;                       // カメラからこの距離まで影を出す
};
static_assert(sizeof(ShadowMaskParams) == 96, "HLSLのShadowMaskParamsと大きさが違います");
} // namespace


//=============================================================================
// 作成
//=============================================================================
ShadowRenderer::ShadowRenderer(uint32_t width, uint32_t height) {
	// 影の量は0~1の1つの値なので、8bit 1チャンネルで足りる
	mask_ = std::make_unique<ComputeTexture>(width, height, DXGI_FORMAT_R8_UNORM, L"ShadowMask");
	// 最初に影を出したフレームで止まらないように、CSのPSOを先に作っておく
	PSOManager::GetComputePipeline(kShadowMaskCS);
}


//=============================================================================
// 影の深度
//=============================================================================
void ShadowRenderer::RenderShadowMap(const Camera* camera, const DirectionalShadowSettings& settings, const std::wstring& windowTitle) {
	// 前のビューの物は使わない
	hasShadowMap_ = false;
	hasMask_ = false;
	settings_ = settings;
	if (!settings.enabled || camera == nullptr) {
		return;
	}
	// --- 影の深度の絵 ---
	const uint32_t mapPixels = GetShadowMapPixels(settings.params.mapSize);
	if (!shadowMap_ || shadowMap_->GetSize() != mapPixels) {
		shadowMap_ = std::make_unique<ShadowMap>(mapPixels);
	}
	// --- ライトから見ると箱と、そこから見たカメラの定数 ---
	lightView_ = MakeLightView(*camera, settings, mapPixels);
	CameraData lightCamera{};
	lightCamera.viewProj = lightView_.viewProj;
	lightCamera.inverseViewProj = Inverse(lightView_.viewProj);
	lightCamera.worldPosition = lightView_.position;
	lightCamera.right = lightView_.right;
	lightCamera.up = lightView_.up;
	const D3D12_GPU_VIRTUAL_ADDRESS lightCameraCB = RenderContext::PushFrameConstant(lightCamera);
	// --- 描く（影を落とす不透明な物の深度だけ）---
	auto* cmdList = DirectXCommon::GetCommandList();
	GPU_SCOPE(cmdList, "Shadow Map");
	shadowMap_->BeginRender(); // バリア変更
	RenderQueue::FlushShadowCaster(windowTitle, lightCameraCB);
	shadowMap_->EndRender();   // バリア変更
	hasShadowMap_ = true;
}


//=============================================================================
// 影の量の画面
//=============================================================================
void ShadowRenderer::BuildShadowMask(RenderTexture& scene, D3D12_GPU_VIRTUAL_ADDRESS cameraCB) {
	if (!hasShadowMap_ || cameraCB == 0) {
		return;
	}
	auto* cmdList = DirectXCommon::GetCommandList();
	GPU_SCOPE(cmdList, "Shadow Mask");
	// --- 定数 ---
	ShadowMaskParams params;
	params.lightViewProj = lightView_.viewProj;
	params.depthIndex = scene.GetDepthSRVSlot();
	params.shadowMapIndex = shadowMap_->GetSRVSlot();
	params.shadowMapSize = shadowMap_->GetSize();
	params.depthBias = (std::max)(settings_.params.bias, 0.0f) / lightView_.depthRange; // ライトの深度の0~1
	params.maskWidth = mask_->GetWidth();
	params.maskHeight = mask_->GetHeight();
	params.strength = std::clamp(settings_.params.strength, 0.0f, 1.0f);
	params.distance = settings_.params.distance;
	const D3D12_GPU_VIRTUAL_ADDRESS paramsCB = RenderContext::PushFrameConstant(params);
	// --- 3Dの絵の深度をCSで読める状態にして、1ピクセル1スレッドで比べる ---
	scene.BeginDepthComputeRead();
	const D3D12_GPU_DESCRIPTOR_HANDLE heapStart = DirectXCommon::BindSRVHeap(cmdList);
	const ComputePipeline& pipeline = PSOManager::GetComputePipeline(kShadowMaskCS);
	mask_->BeginWrite();
	pipeline.Bind(cmdList);
	pipeline.SetConstantBuffer(cmdList, "gCamera", cameraCB);
	pipeline.SetConstantBuffer(cmdList, "gShadowMask", paramsCB);
	pipeline.SetTextureTable(cmdList, "gTextures", heapStart);
	pipeline.SetUnorderedAccessTexture(cmdList, "gOutput", mask_->GetUAVGPUHandle());
	pipeline.Dispatch(cmdList, mask_->GetWidth(), mask_->GetHeight());
	mask_->EndWrite(); // 書き終わってから、色を塗るPSが読む
	scene.EndDepthComputeRead();
	hasMask_ = true;
}


//=============================================================================
// 影の受け方
//=============================================================================
// ===== 不透明な物 =====
D3D12_GPU_VIRTUAL_ADDRESS ShadowRenderer::PushOpaqueReceiver() const { 
	ShadowReceiverData data;
	data.maskIndex = mask_->GetSRVSlot();
	data.hasMask = hasMask_ ? 1u : 0u;
	return RenderContext::PushFrameConstant(data);
}

// ===== 半透明な物 =====
D3D12_GPU_VIRTUAL_ADDRESS ShadowRenderer::PushTransparentReceiver() const {
	ShadowReceiverData data; // hasMask が 0（影なし）
	return RenderContext::PushFrameConstant(data);
}


//=============================================================================
// ライトから見る箱
//=============================================================================
ShadowRenderer::LightView ShadowRenderer::MakeLightView(const Camera& camera, const DirectionalShadowSettings& settings, uint32_t mapPixels) {
	// ===== 1. カメラから見える所のうち、影を出す範囲を包む球 =====
	// 奥へZ進んだ所の四角の角は、カメラの向きの線からz × √(tanX² + tanY²) 離れている
	const float nearZ = camera.GetNearZ();
	const float farZ = (std::max)((std::min)(camera.GetFarZ(), settings.params.distance), nearZ + 0.01f);
	const float tanY = std::tan(camera.GetFovY() * 0.5f);
	const float tanX = tanY * camera.GetAspectRatio();
	const float spreadSq = tanX * tanX + tanY * tanY; // 1進むごとの角の広がりの2乗
	// 球の真ん中（カメラの向きの線の上）: nearの角とfarの角から同じ距離になる所。farを超えるならfar
	const float centerZ = (std::min)((farZ + nearZ) * 0.5f * (1.0f + spreadSq), farZ);
	const float toFarCorner = std::sqrt((farZ - centerZ) * (farZ - centerZ) + farZ * farZ * spreadSq);
	const float toNearCorner = std::sqrt((centerZ - nearZ) * (centerZ - nearZ) + nearZ * nearZ * spreadSq);
	const float radius = (std::max)(toFarCorner, toNearCorner);
	// カメラの前
	const Matrix4x4& view = camera.GetViewMatrix();
	const Vector3 forward = Normalize(Vector3{view.m[0][2], view.m[1][2], view.m[2][2]});
	const Vector3 center = camera.GetTranslation() + forward * centerZ;

	// ===== 2. ライトの向き（+Zを光の進む向きへ）=====
	const Matrix4x4 lightRotation = MakeRotateMatrix(MakeLookRotation(settings.direction));
	const Matrix4x4 worldToLight = Transpose(lightRotation);

	// ===== 3. 箱の真ん中を影の深度の絵の1ピクセルずつに合わせる =====
	Vector3 centerInLight = TransformPoint(center, worldToLight);
	const float texelSize = (radius * 2.0f) / static_cast<float>(mapPixels);
	centerInLight.x = std::floor(centerInLight.x / texelSize) * texelSize;
	centerInLight.y = std::floor(centerInLight.y / texelSize) * texelSize;

	// ===== 4. 正射影 =====
	const float nearInLight = centerInLight.z - radius * kCasterReach;
	const float farInLight = centerInLight.z + radius;
	const Matrix4x4 projection = MakeOrthographicMatrix(centerInLight.x - radius, centerInLight.y + radius, centerInLight.x + radius, centerInLight.y - radius, nearInLight, farInLight);

	LightView result;
	result.viewProj = worldToLight * projection;
	result.right = {lightRotation.m[0][0], lightRotation.m[0][1], lightRotation.m[0][2]};
	result.up = {lightRotation.m[1][0], lightRotation.m[1][1], lightRotation.m[1][2]};
	result.position = TransformPoint({centerInLight.x, centerInLight.y, nearInLight}, lightRotation);
	result.depthRange = farInLight - nearInLight;
	return result;
}