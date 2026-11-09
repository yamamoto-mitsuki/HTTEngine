#include "IBLEnvironment.h"
#include "HTTEngine/Graphics/IBL/IBLIncludes.h"
#include "HTTEngine/Graphics/Texture/TextureManager.h"
#include "HTTEngine/Graphics/Pipeline/ShaderConstants.h"
#include "HTTEngine/Graphics/GPU/DirectXCommon.h"
#include "HTTEngine/Graphics/GPU/GpuResources.h"


//=============================================================================
//
//=============================================================================
void IBLEnvironment::MakeFromHDR(uint32_t handle) { 
	// 読み込み
	auto equirectSRV = TextureManager::GetTextureData(handle)->srvHandleGPU;
	// 焼き先を確保
	env_ = std::make_unique<RenderTextureCube>(IBLConfig::kEnvironmentSize, RenderTextureFormat::HDR, 1);
	irradiance_ = std::make_unique<RenderTextureCube>(IBLConfig::kIrradianceSize, RenderTextureFormat::HDR, 1);
	prefilter_ = std::make_unique<RenderTextureCube>(IBLConfig::kPrefilterSize, RenderTextureFormat::HDR, IBLConfig::kPrefilterMipCount);
	brdfLut_ = std::make_unique<RenderTexture>(IBLConfig::kBrdfLutSize, IBLConfig::kBrdfLutSize, RenderTextureFormat::RG16F, /*useDepth=*/false);
	// 記録
	IBLBaker::Equirect().Record(*env_, equirectSRV);
	IBLBaker::Irradiance().Record(*irradiance_, env_->GetSRVGPUHandle());
	IBLBaker::Prefilter().Record(*prefilter_, env_->GetSRVGPUHandle());
	IBLBaker::BrdfLut().Record(*brdfLut_);
	// まとめて1回Execute + Wait
	DirectXCommon::ExecuteAndWait();
	// CB
	paramsCB_ = GpuResources::CreateUploadBuffer(256);
	IBLParamsData* p = nullptr;
	paramsCB_->Map(0, nullptr, reinterpret_cast<void**>(&p));
	p->irradianceIndex = irradiance_->GetSRVSlot();
	p->prefilterIndex = prefilter_->GetSRVSlot();
	p->brdfLutIndex = brdfLut_->GetSRVSlot();
	p->prefilterMipCount = IBLConfig::kPrefilterMipCount;
	p->enabled = 1;
	paramsCB_->Unmap(0, nullptr);
	paramsAddress_ = paramsCB_->GetGPUVirtualAddress();
}