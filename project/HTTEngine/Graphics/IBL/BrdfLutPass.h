#pragma once
#include <wrl.h>
#include <d3d12.h>
#include "HTTEngine/Graphics/RenderTarget/RenderTexture.h" 


class BrdfLutPass {
public:
	void Initialize();
	void Record(RenderTexture& dst);

private:
	void CreateRootSignature();
	void CreatePSO();
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_;
};