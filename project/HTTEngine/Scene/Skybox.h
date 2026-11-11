#pragma once
#include <cstdint>
#include <memory>
#include <wrl.h>
#include <d3d12.h>
#include "HTTEngine/Graphics/RenderTarget/RenderTextureCube.h"
#include "HTTEngine/Graphics/Pipeline/RootSignatureManager.h"
#include "HTTEngine/Graphics/Renderer/SceneBackground.h"

// 前方宣言
class Camera;


/// <summary>
/// 天球（キューブマップ方式）。.png / .hdrどちらでも。背景が1色のときも、ここで塗る（クリアの色は変えない）
/// </summary>
class Skybox {
public:
	void Initialize();
	// 背景を塗る（background が SolidColor なら、天球の絵の代わりにその色。塗る所は物が描かれていない所だけ）
	void Draw(const Camera* camera, D3D12_GPU_VIRTUAL_ADDRESS cameraCB, const SceneBackground& background);
	void ResetSlot() { slot_ = 0; }

	/// <summary>
	/// equirectの幅からキューブ1面のサイズを決める
	/// </summary>
	static uint32_t CalcCubeSize(float equirectWidth);

	// 既定の空の画像（起動したときの天球。DefaultScene の IBL もこれで作る）
	static constexpr const char* kDefaultSkyPath = "HTTEngine/Resources/Textures/skybox.hdr";

	// ===== セッター =====
	// Cubeにするので、中でClose → ExecuteCommandLists → WaitForGPU → Resetする。注意！
	void SetEquirect(uint32_t handle);
	void SetIntensity(float v) { intensity_ = v; }
	void SetRotationY(float rad) { rotationY_ = rad; }

private:
	void CreateRootSignature();
	void CreatePSO();
	void LoadDefaultTexture();

	static constexpr uint32_t kMaxViewsPerFrame = 4;
	static constexpr uint32_t kSlotSize = 256;
	uint32_t slot_ = 0;

	RootSignatureInfo rsInfo_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_;
	Microsoft::WRL::ComPtr<ID3D12Resource> cb_;
	uint8_t* cbMapped_ = nullptr;

	std::unique_ptr<RenderTextureCube> ownedCube_; // 使用キューブ
	RenderTextureCube* cube_ = nullptr;
	float intensity_ = 1.0f;
	float rotationY_ = 0.0f;
};