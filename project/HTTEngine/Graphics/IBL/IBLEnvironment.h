#pragma once
#include <string>
#include <memory>
#include "HTTEngine/Graphics/RenderTarget/RenderTexture.h"
#include "HTTEngine/Graphics/RenderTarget/RenderTextureCube.h"
#include "HTTEngine/Graphics/IBL/CubeBakePass.h"


/// <summary>
/// 
/// </summary>
class IBLEnvironment {
public:
	/// <summary>
	/// 静的環境マップ 
	/// </summary>
	/// <param name="handle">テクスチャハンドル</param>
	void MakeFromHDR(uint32_t handle);

	RenderTextureCube* GetEnvironment() const { return env_.get(); }
	RenderTextureCube* GetIrradiance() const { return irradiance_.get(); }
	RenderTextureCube* GetPrefilter() const { return prefilter_.get(); }
	RenderTexture* GetBrdfLut() const { return brdfLut_.get(); }
	D3D12_GPU_VIRTUAL_ADDRESS GetParametersAddress() { return paramsAddress_; }

private:
	Microsoft::WRL::ComPtr<ID3D12Resource> paramsCB_;
	D3D12_GPU_VIRTUAL_ADDRESS paramsAddress_ = 0;
	// 所有リソース
	std::unique_ptr<RenderTextureCube> env_, irradiance_, prefilter_;
	std::unique_ptr<RenderTexture> brdfLut_;
};
