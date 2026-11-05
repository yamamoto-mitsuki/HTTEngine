#include "RenderContext.h"

#include <array>
#include <vector>
#include <format>
#include <iterator>

#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/Graphics/GPU/UploadContext.h"
#include "MyEngine/Graphics/Pipeline/PSOManager.h"
#include "MyEngine/Graphics/Pipeline/RenderStates.h"
#include "MyEngine/Graphics/Profiling/GPUScope.h"
#include "MyEngine/Graphics/RenderTarget/RenderWindow.h"
#include "MyEngine/Graphics/Texture/TextureManager.h"
#include "MyEngine/Particle/ParticleManager.h"
#include "MyEngine/Graphics/GPU/GpuResources.h"

// 静的メンバ変数
std::unique_ptr<RenderContext> RenderContext::instance_ = nullptr;

static UINT SlotOf(const RootSignatureInfo& rs, RootBind bind) {
	auto it = rs.slotOf.find(bind);
	MY_ASSERT_MSG(it != rs.slotOf.end(), std::format("slotOf に RootBind::{} がありません。NameToRoleの名前とHLSLの変数名が不一致の可能性があります。", 
		magic_enum::enum_name(bind)));
	return it->second;
}

// ===== 2Dの四角の板1枚（頂点4つ）を、このフレーム用のバッファに書いてセットする。戻り値はインデックスの数（スプライトと図形と同じ）=====
static UINT BindQuad2d(ID3D12GraphicsCommandList* cmdList, FrameUploadBuffer& upload, const std::array<Vertex2dData, 4>& quad) {
	static constexpr std::array<uint32_t, 6> kIndices = {0, 1, 2, 1, 3, 2};
	FrameUploadBuffer::Allocation vertices = upload.PushArray(quad.data(), quad.size());
	FrameUploadBuffer::Allocation indices = upload.PushArray(kIndices.data(), kIndices.size());
	// VertexBufferView
	D3D12_VERTEX_BUFFER_VIEW vbv{};
	vbv.BufferLocation = vertices.gpuAddress;
	vbv.SizeInBytes = static_cast<UINT>(vertices.size);
	vbv.StrideInBytes = sizeof(Vertex2dData);
	// IndexBufferView
	D3D12_INDEX_BUFFER_VIEW ibv{};
	ibv.BufferLocation = indices.gpuAddress;
	ibv.SizeInBytes = static_cast<UINT>(indices.size);
	ibv.Format = DXGI_FORMAT_R32_UINT;

	cmdList->IASetVertexBuffers(0, 1, &vbv);
	cmdList->IASetIndexBuffer(&ibv);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	return static_cast<UINT>(kIndices.size());
}

// ===== シェーダーが使っている定数だけを結ぶ。使っていない定数バッファはコンパイルで消えるので、スロットが無い =====
static void BindConstantIfUsed(ID3D12GraphicsCommandList* cmdList, const RootSignatureInfo& rs, RootBind bind, D3D12_GPU_VIRTUAL_ADDRESS address) {
	auto it = rs.slotOf.find(bind);
	if (it != rs.slotOf.end()) {
		cmdList->SetGraphicsRootConstantBufferView(it->second, address);
	}
}


//=============================================================================
// 初期化・解放
//=============================================================================
// ===== 初期化 =====
void RenderContext::Initialize() {
	MY_ASSERT_MSG(instance_ == nullptr, "Initialize()が2回以上呼ばれています");
	instance_ = std::make_unique<RenderContext>(ConstructorKey());
	instance_->InitInternal();
	LogManager::Log("Initialized");
}

// ===== 解放 =====
void RenderContext::Release() {
	instance_.reset();
	LogManager::Log("Released");
}


//=============================================================================
// このフレームのライト
//=============================================================================
void RenderContext::SetFrameLights(const DirectionalLightData& directionalLight, const PointLightListData& pointLights, const SpotLightListData& spotLights) {
	// GPUの処理が終わるのを待ってから次のフレームに進む作りなので、毎フレーム同じ場所を上書きしてよい
	std::memcpy(instance_->frameDirectionalLightMappedPtr_, &directionalLight, sizeof(DirectionalLightData));
	std::memcpy(instance_->framePointLightsMappedPtr_, &pointLights, sizeof(PointLightListData));
	std::memcpy(instance_->frameSpotLightsMappedPtr_, &spotLights, sizeof(SpotLightListData));
}


//=============================================================================
// フレームの終わり
//=============================================================================
void RenderContext::ResetFrame() { instance_->frameUploadBuffer_.Reset(); }


//=============================================================================
// メッシュ描画
//=============================================================================
void RenderContext::DrawMesh(const MeshRequest& req) {
	auto* cmdList = DirectXCommon::GetCommandList();
	FrameUploadBuffer& upload = instance_->frameUploadBuffer_;

	// ===== 定数（動的・静的共通） =====
	D3D12_GPU_VIRTUAL_ADDRESS materialAddress = upload.PushConstant(req.materialData);
	D3D12_GPU_VIRTUAL_ADDRESS transformAddress = upload.PushConstant(req.objectTransformData);

	// ===== ジオメトリ =====
	uint32_t indexCount = 0;
	if (req.isStatic) {
		// --- 静的（Model）: GPU常駐バッファをバインドする ---
		cmdList->IASetVertexBuffers(0, 1, &req.vbv); // 頂点バッファ（スキンのあるメッシュは、CS で曲げた頂点）
		cmdList->IASetIndexBuffer(&req.ibv);         // インデックスバッファ
		indexCount = req.indexCount;                 // インデックス数
	} else {
		// --- 動的（Primitive）: 頂点とインデックスをこのフレーム用のバッファに書いて VBV / IBV を組む ---
		FrameUploadBuffer::Allocation vertices = upload.PushArray(req.vertices.data(), req.vertices.size());
		FrameUploadBuffer::Allocation indices = upload.PushArray(req.indices.data(), req.indices.size());
		// VertexBufferView（型の区別は StrideInBytes で伝える）
		D3D12_VERTEX_BUFFER_VIEW vbv{};
		vbv.BufferLocation = vertices.gpuAddress;
		vbv.SizeInBytes = static_cast<UINT>(vertices.size);
		vbv.StrideInBytes = sizeof(Vertex3dData);
		// IndexBufferView（型の区別は Format で伝える）
		D3D12_INDEX_BUFFER_VIEW ibv{};
		ibv.BufferLocation = indices.gpuAddress;
		ibv.SizeInBytes = static_cast<UINT>(indices.size);
		ibv.Format = DXGI_FORMAT_R32_UINT;

		cmdList->IASetVertexBuffers(0, 1, &vbv);
		cmdList->IASetIndexBuffer(&ibv);
		indexCount = static_cast<uint32_t>(req.indices.size());
	}
	// トポロジ
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// ===== ShaderConstantsバインド =====
	// RootSignature
	uint32_t prog = PSOManager::GetShaderProgramID(req.category, req.shadingType);
	const RootSignatureInfo& rs = PSOManager::GetRootSignatureInfo(prog);
	// Material
	cmdList->SetGraphicsRootConstantBufferView(SlotOf(rs, RootBind::Material), materialAddress);
	// TransformationMatrix
	cmdList->SetGraphicsRootConstantBufferView(rs.slotOf.at(RootBind::ObjectTransform), transformAddress);
	// --- ライト（そのシェーダーが使っている種類だけ結ぶ） ---
	BindConstantIfUsed(cmdList, rs, RootBind::DirectionalLight, instance_->frameDirectionalLightBuffer_->GetGPUVirtualAddress());
	BindConstantIfUsed(cmdList, rs, RootBind::PointLights, instance_->framePointLightsBuffer_->GetGPUVirtualAddress());
	BindConstantIfUsed(cmdList, rs, RootBind::SpotLights, instance_->frameSpotLightsBuffer_->GetGPUVirtualAddress());
	// --- IBL（PBRのRootSignatureにだけ存在する） ---
	if (req.shadingType == ShadingType::PBR) {
		auto it = rs.slotOf.find(RootBind::IBL);
		MY_ASSERT_MSG(it != rs.slotOf.end(), "PBRのRootSignatureにIBLスロットがありません");
		MY_ASSERT_MSG(req.iblParamsAddress != 0, "PBRにはIBLEnvironmentの設定が必要です");
		cmdList->SetGraphicsRootConstantBufferView(it->second, req.iblParamsAddress);
	}

	// ===== DrawCall =====
#ifdef _DEBUG
	// 例: "suzanne / Material.001"
	std::string marker = req.debugName ? *req.debugName : std::string("Primitive");
	if (req.debugSubName && !req.debugSubName->empty()) {
		marker += " / " + *req.debugSubName;
	}
	GPU_MARKER(cmdList, marker.c_str());
#endif
	DirectXCommon::IncrementDrawCallCount();
	cmdList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
}


//=============================================================================
// デカール描画
//=============================================================================
void RenderContext::DrawDecal(const DecalRequest& req, uint32_t depthIndex) {
	auto* cmdList = DirectXCommon::GetCommandList();
	FrameUploadBuffer& upload = instance_->frameUploadBuffer_;

	// ===== 箱（1辺 1 の立方体。角 i の x・y・z は、i のビット 1・2・4 が立っていれば +0.5、無ければ -0.5）=====
	// 三角形は外から見て時計回り（表）。描くときは表を捨てて裏を描く（SolidFront）ので、カメラが箱の中にあっても塗れる
	static const std::array<Vertex3dData, 8> kCorners = [] {
		std::array<Vertex3dData, 8> corners{};
		for (size_t i = 0; i < corners.size(); ++i) {
			corners[i].position = {(i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f, 1.0f};
		}
		return corners;
	}();
	static constexpr std::array<uint32_t, 36> kIndices = {
	    0, 2, 1, 2, 3, 1, // -Z
	    4, 5, 6, 5, 7, 6, // +Z
	    0, 4, 2, 4, 6, 2, // -X
	    1, 3, 5, 3, 7, 5, // +X
	    0, 1, 4, 1, 5, 4, // -Y
	    2, 6, 3, 6, 7, 3, // +Y
	};
	FrameUploadBuffer::Allocation vertices = upload.PushArray(kCorners.data(), kCorners.size());
	FrameUploadBuffer::Allocation indices = upload.PushArray(kIndices.data(), kIndices.size());
	D3D12_VERTEX_BUFFER_VIEW vbv{vertices.gpuAddress, static_cast<UINT>(vertices.size), sizeof(Vertex3dData)};
	D3D12_INDEX_BUFFER_VIEW ibv{indices.gpuAddress, static_cast<UINT>(indices.size), DXGI_FORMAT_R32_UINT};
	cmdList->IASetVertexBuffers(0, 1, &vbv);
	cmdList->IASetIndexBuffer(&ibv);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// ===== 定数（シェーダーが使っている物だけ結ぶ）=====
	const RootSignatureInfo& rs = PSOManager::GetRootSignatureInfo(req.programId);
	// 箱の位置（VS）
	cmdList->SetGraphicsRootConstantBufferView(SlotOf(rs, RootBind::ObjectTransform), upload.PushConstant(MakeObjectTransform(req.worldMatrix)));
	// 箱の中へ戻す行列と深度（PS。全部のデカールで共通）
	DecalData decal;
	decal.worldToDecal = Inverse(req.worldMatrix);
	decal.depthIndex = depthIndex;
	BindConstantIfUsed(cmdList, rs, RootBind::Decal, upload.PushConstant(decal));
	// マテリアルの値（gMaterial の並びに詰めた物）
	if (!req.materialConstants.empty()) {
		BindConstantIfUsed(cmdList, rs, RootBind::Material, upload.PushConstantBytes(req.materialConstants));
	}
	// 描き手の値（形など。読む Shader か・大きさが合うかは Renderer::DrawDecal で確かめてある）
	if (!req.drawData.empty()) {
		BindConstantIfUsed(cmdList, rs, RootBind::DrawData, upload.PushConstantBytes(req.drawData));
	}

	// ===== DrawCall =====
	DirectXCommon::IncrementDrawCallCount();
	cmdList->DrawIndexedInstanced(static_cast<UINT>(kIndices.size()), 1, 0, 0, 0);
}


//=============================================================================
// パーティクル描画
//=============================================================================
void RenderContext::DrawParticles(const ParticleRequest& req) {
	auto* cmdList = DirectXCommon::GetCommandList();
	FrameUploadBuffer& upload = instance_->frameUploadBuffer_;
	// RootSignature
	uint32_t prog = PSOManager::GetShaderProgramID(DrawCategory::Particle);
	const RootSignatureInfo& rs = PSOManager::GetRootSignatureInfo(prog);

	// ===== このフレーム用のバッファに書く =====
	// インスタンス配列（VSがStructuredBufferとして読む）
	FrameUploadBuffer::Allocation instances = upload.PushArray(req.instances.data(), req.instances.size());
	// グループのマテリアル
	D3D12_GPU_VIRTUAL_ADDRESS materialAddress = upload.PushConstant(req.materialData);

	// ===== バインド =====
	// VSのParticle
	cmdList->SetGraphicsRootShaderResourceView(rs.slotOf.at(RootBind::Particles), instances.gpuAddress);
	// マテリアル
	cmdList->SetGraphicsRootConstantBufferView(rs.slotOf.at(RootBind::Material), materialAddress);

	// quadをインスタンス数分
	UINT count = static_cast<UINT>(req.instances.size());
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->IASetVertexBuffers(0, 1, &instance_->particleQuadVBV_);
	cmdList->IASetIndexBuffer(&instance_->particleQuadIBV_);
	DirectXCommon::IncrementDrawCallCount();
	cmdList->DrawIndexedInstanced(6, count, 0, 0, 0);
}


//=============================================================================
// 2Dスプライト描画
//=============================================================================
void RenderContext::DrawSprite(const SpriteRequest& req) {
	auto* cmdList = DirectXCommon::GetCommandList();
	FrameUploadBuffer& upload = instance_->frameUploadBuffer_;

	// ===== このフレーム用のバッファに書く =====
	D3D12_GPU_VIRTUAL_ADDRESS materialAddress = upload.PushConstant(req.materialData);
	const UINT indexCount = BindQuad2d(cmdList, upload, req.vertices);

	// ===== ShaderConstantsバインド =====
	// RootSignatureID
	uint32_t prog = PSOManager::GetShaderProgramID(DrawCategory::Sprite);
	const RootSignatureInfo& rs = PSOManager::GetRootSignatureInfo(prog);
	// Material
	cmdList->SetGraphicsRootConstantBufferView(rs.slotOf.at(RootBind::Material), materialAddress);

	// ===== Draw Call =====
	DirectXCommon::IncrementDrawCallCount();
	cmdList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
}


//=============================================================================
// 2Dの文字描画
//=============================================================================
void RenderContext::DrawText2d(const TextRequest& req) {
	const uint32_t quadCount = static_cast<uint32_t>(req.vertices.size() / 4); // 1文字に頂点4つ
	if (quadCount == 0) {
		return;
	}
	auto* cmdList = DirectXCommon::GetCommandList();
	FrameUploadBuffer& upload = instance_->frameUploadBuffer_;

	// ===== 頂点の番号（1文字に三角形2つ。スプライトと同じ並びを、4つずつずらして並べる）=====
	std::vector<uint32_t> indices;
	indices.reserve(static_cast<size_t>(quadCount) * 6);
	for (uint32_t i = 0; i < quadCount; ++i) {
		const uint32_t first = i * 4;
		indices.insert(indices.end(), {first + 0, first + 1, first + 2, first + 1, first + 3, first + 2});
	}

	// ===== このフレーム用のバッファに書く =====
	D3D12_GPU_VIRTUAL_ADDRESS materialAddress = upload.PushConstant(req.materialData);
	FrameUploadBuffer::Allocation vertices = upload.PushArray(req.vertices.data(), req.vertices.size());
	FrameUploadBuffer::Allocation indexBuffer = upload.PushArray(indices.data(), indices.size());

	// ===== ジオメトリ =====
	D3D12_VERTEX_BUFFER_VIEW vbv{};
	vbv.BufferLocation = vertices.gpuAddress;
	vbv.SizeInBytes = static_cast<UINT>(vertices.size);
	vbv.StrideInBytes = sizeof(Vertex2dData);
	D3D12_INDEX_BUFFER_VIEW ibv{};
	ibv.BufferLocation = indexBuffer.gpuAddress;
	ibv.SizeInBytes = static_cast<UINT>(indexBuffer.size);
	ibv.Format = DXGI_FORMAT_R32_UINT;
	cmdList->IASetVertexBuffers(0, 1, &vbv);
	cmdList->IASetIndexBuffer(&ibv);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// ===== ShaderConstantsバインド =====
	const RootSignatureInfo& rs = PSOManager::GetRootSignatureInfo(PSOManager::GetShaderProgramID(DrawCategory::Text));
	cmdList->SetGraphicsRootConstantBufferView(SlotOf(rs, RootBind::Material), materialAddress);

	// ===== Draw Call =====
	DirectXCommon::IncrementDrawCallCount();
	cmdList->DrawIndexedInstanced(static_cast<UINT>(indices.size()), 1, 0, 0, 0);
}


//=============================================================================
// 2Dの図形描画
//=============================================================================
void RenderContext::DrawVectorShape(const VectorShapeRequest& req) {
	auto* cmdList = DirectXCommon::GetCommandList();
	FrameUploadBuffer& upload = instance_->frameUploadBuffer_;

	// ===== このフレーム用のバッファに書く（毎フレーム値が変わっても、確保済みのバッファの続きに書くだけ）=====
	D3D12_GPU_VIRTUAL_ADDRESS materialAddress = upload.PushConstant(req.materialData);
	const UINT indexCount = BindQuad2d(cmdList, upload, req.vertices);

	// ===== ShaderConstantsバインド =====
	const RootSignatureInfo& rs = PSOManager::GetRootSignatureInfo(PSOManager::GetShaderProgramID(DrawCategory::VectorShape));
	cmdList->SetGraphicsRootConstantBufferView(SlotOf(rs, RootBind::Material), materialAddress);

	// ===== Draw Call =====
	DirectXCommon::IncrementDrawCallCount();
	cmdList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
}


//=============================================================================
// 2Dの折れ線描画
//=============================================================================
void RenderContext::DrawPolyline(const PolylineRequest& req) {
	if (req.mesh == nullptr || req.indexCount == 0 || req.mesh->vertices.empty()) {
		return;
	}
	auto* cmdList = DirectXCommon::GetCommandList();
	FrameUploadBuffer& upload = instance_->frameUploadBuffer_;
	const PolylineMeshData& mesh = *req.mesh;

	// ===== このフレーム用のバッファに書く（頂点は全部、番号は描く範囲だけ）=====
	D3D12_GPU_VIRTUAL_ADDRESS materialAddress = upload.PushConstant(req.materialData);
	FrameUploadBuffer::Allocation vertices = upload.PushArray(mesh.vertices.data(), mesh.vertices.size());
	FrameUploadBuffer::Allocation indices = upload.PushArray(mesh.indices.data() + req.indexStart, req.indexCount);

	// ===== ジオメトリ =====
	D3D12_VERTEX_BUFFER_VIEW vbv{};
	vbv.BufferLocation = vertices.gpuAddress;
	vbv.SizeInBytes = static_cast<UINT>(vertices.size);
	vbv.StrideInBytes = sizeof(VertexPolylineData);
	D3D12_INDEX_BUFFER_VIEW ibv{};
	ibv.BufferLocation = indices.gpuAddress;
	ibv.SizeInBytes = static_cast<UINT>(indices.size);
	ibv.Format = DXGI_FORMAT_R32_UINT;
	cmdList->IASetVertexBuffers(0, 1, &vbv);
	cmdList->IASetIndexBuffer(&ibv);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// ===== ShaderConstantsバインド（VS と PS の両方が同じ定数を読む）=====
	const RootSignatureInfo& rs = PSOManager::GetRootSignatureInfo(PSOManager::GetShaderProgramID(DrawCategory::Polyline));
	cmdList->SetGraphicsRootConstantBufferView(SlotOf(rs, RootBind::Material), materialAddress);

	// ===== Draw Call =====
	DirectXCommon::IncrementDrawCallCount();
	cmdList->DrawIndexedInstanced(req.indexCount, 1, 0, 0, 0);
}


//=============================================================================
// Line3D描画
//=============================================================================
void RenderContext::DrawLines(const LineRequest& req) {
	// 奇数は切り捨て。描く線が無ければ何もしない
	size_t vertexCount = req.vertices.size() & ~size_t(1);
	if (vertexCount == 0) {
		return;
	}
	auto* cmdList = DirectXCommon::GetCommandList();
	FrameUploadBuffer& upload = instance_->frameUploadBuffer_;
	uint32_t prog = PSOManager::GetShaderProgramID(DrawCategory::Line);
	const RootSignatureInfo& rs = PSOManager::GetRootSignatureInfo(prog);

	// ===== このフレーム用のバッファに書く =====
	D3D12_GPU_VIRTUAL_ADDRESS materialAddress = upload.PushConstant(req.materialData);
	D3D12_GPU_VIRTUAL_ADDRESS transformAddress = upload.PushConstant(req.objectTransformData);
	FrameUploadBuffer::Allocation vertices = upload.PushArray(req.vertices.data(), vertexCount);

	// ===== ジオメトリ =====
	// VertexBufferView
	D3D12_VERTEX_BUFFER_VIEW vbv{};
	vbv.BufferLocation = vertices.gpuAddress;
	vbv.SizeInBytes = static_cast<UINT>(vertices.size);
	vbv.StrideInBytes = sizeof(VertexLineData);
	cmdList->IASetVertexBuffers(0, 1, &vbv);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);

	// ===== ShaderConstantsバインド =====
	// Material
	cmdList->SetGraphicsRootConstantBufferView(rs.slotOf.at(RootBind::Material), materialAddress);
	// TransformationMatrix
	cmdList->SetGraphicsRootConstantBufferView(rs.slotOf.at(RootBind::ObjectTransform), transformAddress);

	// ===== Draw Call =====
	DirectXCommon::IncrementDrawCallCount();
	cmdList->DrawInstanced(static_cast<UINT>(vertexCount), 1, 0, 0);
}


//=============================================================================
// 初期化（内部）
//=============================================================================
void RenderContext::InitInternal() {
	// ===== 描画ごとに増えるデータの置き場（1つだけ） =====
	frameUploadBuffer_.Initialize(kFrameUploadBytes, "frameUploadBuffer_");

	// ===== ライト（1フレームに1個。全描画で同じ場所を結ぶので、描画回数分の大きさは要らない） =====
	// 大きさはCreateUploadBufferの中で256の倍数にそろえられる
	frameDirectionalLightBuffer_ = GpuResources::CreateMappedUploadBuffer(sizeof(DirectionalLightData), reinterpret_cast<void**>(&frameDirectionalLightMappedPtr_));
	frameDirectionalLightBuffer_->SetName(L"frameDirectionalLightBuffer_");
	framePointLightsBuffer_ = GpuResources::CreateMappedUploadBuffer(sizeof(PointLightListData), reinterpret_cast<void**>(&framePointLightsMappedPtr_));
	framePointLightsBuffer_->SetName(L"framePointLightsBuffer_");
	frameSpotLightsBuffer_ = GpuResources::CreateMappedUploadBuffer(sizeof(SpotLightListData), reinterpret_cast<void**>(&frameSpotLightsMappedPtr_));
	frameSpotLightsBuffer_->SetName(L"frameSpotLightsBuffer_");
	*frameDirectionalLightMappedPtr_ = DirectionalLightData{}; // 最初のSetFrameLightsまでの既定値（白・真下・強さ1）
	*framePointLightsMappedPtr_ = PointLightListData{};        // ポイントライト0個
	*frameSpotLightsMappedPtr_ = SpotLightListData{};          // スポットライト0個

	// ===== パーティクル用の共通Quad =====
	// 頂点フォーマットは Particle の InputLayout（POSITION + TEXCOORD） = VertexParticleData
	const std::array<Vertex2dData, 4> quadVertices = {
	    {
            {{-0.5f, +0.5f, 0.0f, 1.0f}, {0.0f, 0.0f}}, // 左上
	        {{+0.5f, +0.5f, 0.0f, 1.0f}, {1.0f, 0.0f}}, // 右上
	        {{-0.5f, -0.5f, 0.0f, 1.0f}, {0.0f, 1.0f}}, // 左下
	        {{+0.5f, -0.5f, 0.0f, 1.0f}, {1.0f, 1.0f}}, // 右下
	    }
    };
	const std::array<uint32_t, 6> quadIndices = {0, 1, 2, 1, 3, 2};
	// 二度と書き換えないので、モデルと同じ GPU 常駐バッファにする（毎フレーム GPU から読むので、Upload ヒープより速い）
	particleQuadVBV_ = UploadContext::CreateVertexBuffer(quadVertices, particleQuadVB_, L"ParticleQuadVB");
	particleQuadIBV_ = UploadContext::CreateIndexBuffer(quadIndices, particleQuadIB_, L"ParticleQuadIB");
	UploadContext::Flush(); // 送り終わるまで待つ
}


//=============================================================================
// GPUページフォルトのアドレスをどのリソースが原因かログに出力する
//=============================================================================
void RenderContext::LogFaultResource(D3D12_GPU_VIRTUAL_ADDRESS faultVA) {
	// 解放後に呼ばれたときは調べない
	if (!instance_) {
		return;
	}

	struct Entry {
		const char* name;
		ID3D12Resource* resource;
	};
	const auto buffers = std::to_array<Entry>({
	    {"frameUploadBuffer_",           instance_->frameUploadBuffer_.GetResource()  },
	    {"frameDirectionalLightBuffer_", instance_->frameDirectionalLightBuffer_.Get()},
	    {"framePointLightsBuffer_",      instance_->framePointLightsBuffer_.Get()     },
	    {"frameSpotLightsBuffer_",       instance_->frameSpotLightsBuffer_.Get()      },
	    {"particleQuadVB_",              instance_->particleQuadVB_.Get()             },
	    {"particleQuadIB_",              instance_->particleQuadIB_.Get()             },
	});

	for (const Entry& e : buffers) {
		if (!e.resource) {
			continue;
		}
		D3D12_GPU_VIRTUAL_ADDRESS base = e.resource->GetGPUVirtualAddress(); // GPUアドレス
		UINT64 size = e.resource->GetDesc().Width;                           // バッファのバイトサイズ

		if (faultVA >= base && faultVA < base + size) {
			UINT64 offset = faultVA - base;
			LogManager::Error(std::format("[DRED] fault VA is inside '{}'  offset = {} / {} bytes", e.name, offset, size));
			if (e.resource == instance_->frameUploadBuffer_.GetResource()) {
				// 使っていた範囲より後ろなら、確保していない場所を読んでいる
				LogManager::Error(std::format("[DRED] frameUploadBuffer_ used = {} bytes", instance_->frameUploadBuffer_.GetUsedBytes()));
			}
			return;
		}
	}
	LogManager::Error("[DRED] fault VA does not match any RenderContext buffer");
}