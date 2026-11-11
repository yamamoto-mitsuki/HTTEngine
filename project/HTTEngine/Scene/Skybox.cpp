#include "Skybox.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Camera/Camera.h"
#include "HTTEngine/Math/MathIncludes.h"
#include "HTTEngine/Graphics/IBL/IBLBaker.h"
#include "HTTEngine/Graphics/IBL/IBLConfig.h"
#include "HTTEngine/Graphics/Texture/TextureManager.h"
#include "HTTEngine/Graphics/GPU/DirectXCommon.h"
#include "HTTEngine/Graphics/GPU/GpuResources.h"
#include "HTTEngine/Graphics/Pipeline/ShaderConstants.h"
#include "HTTEngine/Graphics/Pipeline/ShaderPackageLoader.h"
#include "HTTEngine/Graphics/Pipeline/PSOManager.h"
#include "HTTEngine/Graphics/RenderTarget/RenderTexture.h"

using namespace Microsoft::WRL;

namespace {
const std::string vsName = "SkyboxVS";
const std::string psName = "SkyboxPS";
}


//=============================================================================
// 初期化
//=============================================================================
void Skybox::Initialize() { 
	CreateRootSignature();
	CreatePSO();
	cb_ = GpuResources::CreateMappedUploadBuffer(kSlotSize * kMaxViewsPerFrame, reinterpret_cast<void**>(&cbMapped_));
	cb_->SetName(L"SkyboxCB");
	LoadDefaultTexture();
	LogManager::Log("Initialized");
}


//=============================================================================
// デフォルト天球	
//=============================================================================
void Skybox::LoadDefaultTexture() { 
	SetEquirect(TextureManager::Load(kDefaultSkyPath)); 
}

//=============================================================================
// 描画
//=============================================================================
void Skybox::Draw(const Camera* camera, D3D12_GPU_VIRTUAL_ADDRESS cameraCB, const SceneBackground& background) {
	// 早期リターン
	HTT_ASSERT_MSG(cube_, "テクスチャが設定されていません。SetEquirectで設定してください。");
	HTT_ASSERT_MSG(camera, "カメラが設定されていません。");
	HTT_ASSERT_MSG(slot_ < kMaxViewsPerFrame, "1フレームのビュー数が上限を超えました");

	//	カメラの回転のみ
	Matrix4x4 view = camera->GetViewMatrix();
	view.m[3][0] = view.m[3][1] = view.m[3][2] = 0.0f;
	// CBV更新。コマンドの実行はフレーム末。1スロットを使い回すと後のビューの値で上書きされる
	size_t offset = slot_ * kSlotSize;
	auto* cb = reinterpret_cast<SkyboxData*>(cbMapped_ + offset);
	cb->world = MakeRotateYMatrix(rotationY_);
	cb->viewProj = view * camera->GetProjectionMatrix();
	cb->intensity = intensity_;
	cb->cubeIndex = cube_->GetSRVSlot();
	// 背景の塗り方（1色・グラデーションも、天球と同じ所をここで塗る。postRT_ のクリアの色は作ったときの色から変えない）
	cb->backgroundType = static_cast<uint32_t>(background.type); // シェーダーの kBackground〜 と同じ番号
	cb->solidColor = background.color;
	cb->skyColor = background.skyColor;
	cb->horizonColor = background.horizonColor;
	cb->groundColor = background.groundColor;
	cb->horizonWidth = background.horizonWidth;
	++slot_;

	auto* cmdList = DirectXCommon::GetCommandList();
	const D3D12_GPU_DESCRIPTOR_HANDLE heapStart = DirectXCommon::BindSRVHeap(cmdList);
	cmdList->SetGraphicsRootSignature(rsInfo_.rootSignature.Get()); // RootSignature
	cmdList->SetPipelineState(pso_.Get()); // PipelineState
	cmdList->SetGraphicsRootConstantBufferView(rsInfo_.slotOf.at(RootBind::Skybox), cb_->GetGPUVirtualAddress() + offset); // b20
	cmdList->SetGraphicsRootConstantBufferView(rsInfo_.slotOf.at(RootBind::Camera), cameraCB);                             // b0（霧の値）
	cmdList->SetGraphicsRootDescriptorTable(rsInfo_.slotOf.at(RootBind::BindlessTextureCube), heapStart); // t0
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->DrawInstanced(36, 1, 0, 0);
}


//=============================================================================
// RootSignatureInfo作成
//=============================================================================
void Skybox::CreateRootSignature() { 
	// ShaderReflection
	const ShaderReflection& vsRef = ShaderPackageLoader::GetShaderReflection(vsName);
	const ShaderReflection& psRef = ShaderPackageLoader::GetShaderReflection(psName);
	// RootSignatureInfo
	auto shaderResources = RootSignatureManager::MergeStages(vsRef.resources, psRef.resources);
	rsInfo_ = RootSignatureManager::MakeRootSignatureInfo(shaderResources);
}


//=============================================================================
// PSO作成
//=============================================================================
void Skybox::CreatePSO() { 
	// --- Shaderコンパイル結果 ---
	IDxcBlob* vsBlob = ShaderPackageLoader::GetShaderReflection(vsName).blob.Get();
	IDxcBlob* psBlob = ShaderPackageLoader::GetShaderReflection(psName).blob.Get();

	// --- PipelineStateDesc ---
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = rsInfo_.rootSignature.Get();
	desc.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
	desc.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
	desc.InputLayout = {nullptr, 0};
	desc.BlendState = RenderStates::MakeBlendDesc(BlendMode::None); // Blend
	desc.RasterizerState = RenderStates::MakeRasterizerDesc(RasterizerType::SolidNone);  // Rasterizer
	desc.DepthStencilState = RenderStates::MakeDepthStencilDesc(DepthMode::TestNoWrite); // 深度
	// シーンの描画先（SceneRenderer の postRT_。HDR）と一致させる
	desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = RenderTexture::ToDXGIFormat(RenderTextureFormat::HDR);
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.SampleDesc.Count = 1;
	desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	// 作成
	HRESULT hr = DirectXCommon::GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso_));
	HTT_ASSERT_MSG(SUCCEEDED(hr), "Skybox PSO作成失敗");
}


//=============================================================================
// セッター
//=============================================================================
void Skybox::SetEquirect(uint32_t handle) { 
	auto equirectSRV = TextureManager::GetTextureData(handle)->srvHandleGPU;
	// 表示用のEnvキューブだけ確保して焼く。サイズは元画像から決める
	Vector2 texSize = TextureManager::GetTextureSize(handle);
	uint32_t cubeSize = CalcCubeSize(texSize.x);
	LogManager::Log("Skybox cube size = " + std::to_string(cubeSize) + " (source " + std::to_string(static_cast<int>(texSize.x)) + "x" + std::to_string(static_cast<int>(texSize.y)) + ")");
	ownedCube_ = std::make_unique<RenderTextureCube>(cubeSize, RenderTextureFormat::HDR, 1);
	IBLBaker::Equirect().Record(*ownedCube_, equirectSRV);
	// Recordはコマンドリストに積むだけなので実行と待ち
	DirectXCommon::ExecuteAndWait();

	cube_ = ownedCube_.get();
}


//=============================================================================
// equirectの解像度からキューブ1面のサイズを決める
//=============================================================================
uint32_t Skybox::CalcCubeSize(float equirectWidth) {
	// equirectの横幅は360度ぶん。1面は90度なので width/4 が等倍になる
	uint32_t ideal = static_cast<uint32_t>(equirectWidth) / 4;
	// 2のべき乗へ切り上げる（ミップやサンプラの都合が良い）
	uint32_t size = IBLConfig::kSkyboxSizeMin;
	while (size < ideal && size < IBLConfig::kSkyboxSizeMax) {
		size <<= 1;
	}
	return size;
}