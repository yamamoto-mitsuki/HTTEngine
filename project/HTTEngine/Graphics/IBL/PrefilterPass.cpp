#include "PrefilterPass.h"
#include <array>
#include <numbers>
#include "HTTEngine/Math/MathIncludes.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Graphics/IBL/IBLConfig.h"
#include "HTTEngine/Graphics/GPU/DirectXCommon.h"
#include "HTTEngine/Graphics/GPU/GpuResources.h"
#include "HTTEngine/Graphics/Pipeline/ShaderCompiler.h"
#include "HTTEngine/Graphics/Pipeline/ShaderPackageLoader.h"
#include "HTTEngine/Graphics/RenderTarget/RenderWindow.h"

using namespace Microsoft::WRL;

namespace {
struct PrefilterData {
	Matrix4x4 viewProj;
	float roughness;
	float pad[3];
};
} // namespace


//=============================================================================
// 初期化
//=============================================================================
void PrefilterPass::Initialize() {
	cbSlotSize_ = IBLConfig::AlignTo256(sizeof(PrefilterData));
	// 6面 × mip段数 ぶん確保
	cbBuffer_ = GpuResources::CreateUploadBuffer(cbSlotSize_ * 6 * IBLConfig::kPrefilterMipCount);
	cbBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&cbMapped_));
	CreateRootSignature();
	CreatePSO();
}


//=============================================================================
// 
//=============================================================================
void PrefilterPass::Record(RenderTextureCube& dst, D3D12_GPU_DESCRIPTOR_HANDLE envSrv) {
	auto* cmdList = DirectXCommon::GetCommandList();
	// バリア
	DirectXCommon::TransitionBarrier(dst.GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

	DirectXCommon::BindSRVHeap(cmdList);
	cmdList->SetGraphicsRootSignature(rootSignature_.Get());
	cmdList->SetPipelineState(pso_.Get());
	cmdList->SetGraphicsRootDescriptorTable(1, envSrv);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	Matrix4x4 proj = MakePerspectiveFovMatrix(std::numbers::pi_v<float> / 2.0f, 1.0f, 0.1f, 10.0f);
	uint32_t mipCount = dst.GetMipLevels();
	uint32_t slot = 0;

	for (uint32_t mip = 0; mip < mipCount; ++mip) {
		uint32_t mipSize = dst.GetSize() >> mip; // mipごとに半分
		float roughness = (mipCount > 1) ? (float)mip / (mipCount - 1) : 0.0f;

		D3D12_VIEWPORT vp{0, 0, (float)mipSize, (float)mipSize, 0, 1};
		D3D12_RECT sc{0, 0, (LONG)mipSize, (LONG)mipSize};
		cmdList->RSSetViewports(1, &vp);
		cmdList->RSSetScissorRects(1, &sc);

		for (uint32_t face = 0; face < 6; ++face) {
			auto* cb = reinterpret_cast<PrefilterData*>(cbMapped_ + slot * cbSlotSize_);
			cb->viewProj = IBLConfig::MakeCubeView(IBLConfig::kDirs[face], IBLConfig::kUps[face]) * proj;
			cb->roughness = roughness;
			cmdList->SetGraphicsRootConstantBufferView(0, cbBuffer_->GetGPUVirtualAddress() + slot * cbSlotSize_);
			slot++;

			auto rtv = dst.GetFaceRTV(face, mip); // face×mip
			cmdList->OMSetRenderTargets(1, &rtv, false, nullptr);
			cmdList->ClearRenderTargetView(rtv, RenderWindow::kClearColor.data(), 0, nullptr);
			cmdList->DrawInstanced(36, 1, 0, 0);
		}
	}
	// バリア
	DirectXCommon::TransitionBarrier(dst.GetResource(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}


//=============================================================================
// RootSignature作成
//=============================================================================
void PrefilterPass::CreateRootSignature() {
	// ===== Sampler =====
	D3D12_STATIC_SAMPLER_DESC sampler{};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.ShaderRegister = 0; // s0
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;

	// ===== RootParameter =====
	std::array<D3D12_ROOT_PARAMETER, 2> params{};
	// b0 CBV（VS） Params
	params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; // PSでroughnessを読むため
	params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	params[0].Descriptor.ShaderRegister = 0;
	// t0 SRV table (PS) equirect
	D3D12_DESCRIPTOR_RANGE srvRange{};
	srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange.NumDescriptors = 1;
	srvRange.BaseShaderRegister = 0; // t0
	srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	params[1].DescriptorTable.NumDescriptorRanges = 1;
	params[1].DescriptorTable.pDescriptorRanges = &srvRange;

	// ===== RootSignature =====
	// desc
	D3D12_ROOT_SIGNATURE_DESC desc{};
	desc.NumParameters = static_cast<UINT>(params.size());
	desc.pParameters = params.data();
	desc.NumStaticSamplers = 1;
	desc.pStaticSamplers = &sampler;
	desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	// 作成
	ComPtr<ID3DBlob> blob, error;
	HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
	HTT_ASSERT_MSG(SUCCEEDED(hr), "IBL RootSignature Serialize失敗");
	hr = DirectXCommon::GetDevice()->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_));
	HTT_ASSERT_MSG(SUCCEEDED(hr), "IBL RootSignature作成失敗");
}


//=============================================================================
// PSO作成
//=============================================================================
void PrefilterPass::CreatePSO() {
	IDxcBlob* vsBlob = ShaderPackageLoader::GetShaderReflection("EquirectToCubeVS").blob.Get();
	IDxcBlob* psBlob = ShaderPackageLoader::GetShaderReflection("PrefilterPS").blob.Get();
	// 設定
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = rootSignature_.Get();
	desc.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
	desc.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};
	desc.InputLayout = {nullptr, 0}; // 頂点バッファ無し
	desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; // 内側から見る
	desc.DepthStencilState.DepthEnable = FALSE;           // 深度無し
	desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT; // HDR
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.SampleDesc.Count = 1;
	desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	// 作成
	HRESULT hr = DirectXCommon::GetDevice()->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso_));
	HTT_ASSERT_MSG(SUCCEEDED(hr), "IBL PSO作成失敗");
}