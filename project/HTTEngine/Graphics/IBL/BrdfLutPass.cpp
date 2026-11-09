#include "BrdfLutPass.h"
#include "HTTEngine/Graphics/Pipeline/ShaderCompiler.h"
#include "HTTEngine/Graphics/Pipeline/ShaderPackageLoader.h"

using  namespace Microsoft::WRL;

void BrdfLutPass::Initialize() { 
	CreateRootSignature();
	CreatePSO();
}

void BrdfLutPass::Record(RenderTexture& dst) {
	auto* cmdList = DirectXCommon::GetCommandList();
	// RenderTexture(2D)の状態遷移＋RTVセット＋クリアは PreDraw/PostDraw が面倒みてくれる
	dst.PreDraw();
	// viewportはPreDrawが張らないので自分で
	D3D12_VIEWPORT vp{0, 0, (float)dst.GetWidth(), (float)dst.GetHeight(), 0, 1};
	D3D12_RECT sc{0, 0, (LONG)dst.GetWidth(), (LONG)dst.GetHeight()};
	cmdList->RSSetViewports(1, &vp);
	cmdList->RSSetScissorRects(1, &sc);
	cmdList->SetGraphicsRootSignature(rootSignature_.Get());
	cmdList->SetPipelineState(pso_.Get());
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->DrawInstanced(3, 1, 0, 0); // フルスクリーン三角形
	dst.PostDraw();
}

//=============================================================================
// RootSignature作成
//=============================================================================
void BrdfLutPass::CreateRootSignature() {
	// テクスチャもCBVも使わない
	D3D12_ROOT_SIGNATURE_DESC desc{};
	desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
	ComPtr<ID3DBlob> blob, err;
	D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
	DirectXCommon::GetDevice()->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rootSignature_));
}


//=============================================================================
// PSO作成
//=============================================================================
void BrdfLutPass::CreatePSO() {
	// ShaderCompile
	IDxcBlob* vs = ShaderPackageLoader::GetShaderReflection("BrdfLutVS").blob.Get();
	IDxcBlob* ps = ShaderPackageLoader::GetShaderReflection("BrdfLutPS").blob.Get();
	// 作成
	D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
	d.pRootSignature = rootSignature_.Get();
	d.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
	d.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
	d.InputLayout = {nullptr, 0};
	d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	d.DepthStencilState.DepthEnable = FALSE;
	d.DSVFormat = DXGI_FORMAT_UNKNOWN;
	d.NumRenderTargets = 1;
	d.RTVFormats[0] = DXGI_FORMAT_R16G16_FLOAT; // RG16F
	d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	d.SampleDesc.Count = 1;
	d.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	DirectXCommon::GetDevice()->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&pso_));
}