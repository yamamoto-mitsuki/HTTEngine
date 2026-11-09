#pragma once
#include <wrl.h>
#include <d3d12.h>
#include "HTTEngine/Graphics/RenderTarget/RenderTextureCube.h"


class PrefilterPass {
public:
	void Initialize();
	void Record(RenderTextureCube& env, D3D12_GPU_DESCRIPTOR_HANDLE envSrv);


private:
	void CreateRootSignature();
	void CreatePSO();

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_;
	Microsoft::WRL::ComPtr<ID3D12Resource> cbBuffer_;
	uint8_t* cbMapped_ = nullptr;
	size_t cbSlotSize_ = 0;
};