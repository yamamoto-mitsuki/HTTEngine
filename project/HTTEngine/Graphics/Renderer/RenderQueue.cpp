#include "HTTEngine/Graphics/Renderer/RenderQueue.h"

#include <algorithm>

#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Graphics/Renderer/RenderContext.h"
#include "HTTEngine/Graphics/RenderTarget/RenderWindow.h"
#include "HTTEngine/Graphics/Pipeline/PSOManager.h"
#include "HTTEngine/Graphics/Profiling/GPUProfiler.h"
#include "HTTEngine/Graphics/GPU/DirectXCommon.h"

// 静的メンバ変数
std::unique_ptr<RenderQueue> RenderQueue::instance_ = nullptr;


//=============================================================================
// 初期化・解放
//=============================================================================
// ===== 初期化 =====
void RenderQueue::Initialize() { 
	instance_ = std::make_unique<RenderQueue>(ConstructorKey());
	instance_->isLayerVisible_.fill(true); // 初めは全部の層を描く（fill：配列の全部の要素を同じ値にする）
	LogManager::Log("Initialized");
}

// ===== 解放 =====
void RenderQueue::Release() { 
	instance_.reset();
	instance_ = nullptr;
	LogManager::Log("Released");
}

// ===== クリア =====
void RenderQueue::Clear() { 
	instance_->transparentMeshRequests_.clear();
	instance_->opaqueMeshRequests_.clear();
	instance_->particleRequests_.clear();
	instance_->requests2d_.clear();
	instance_->lineRequests_.clear();
	instance_->decalRequests_.clear();
}

// ===== 層ごとに描くか =====
void RenderQueue::SetLayerVisible(DrawLayer layer, bool isVisible) { instance_->isLayerVisible_[static_cast<size_t>(layer)] = isVisible; }

// ===== 目で隠した物を描くか =====
void RenderQueue::SetSceneHiddenVisible(bool isVisible) { instance_->isSceneHiddenVisible_ = isVisible; }

// ===== 今のビューで描く物か =====
bool RenderQueue::IsDrawnInView(const DrawTarget& target, const std::wstring& windowTitle) {
	// 別のウィンドウの物は描かない（windowTitle が空の物は、どのウィンドウにも描く）
	if (!target.windowTitle.empty() && target.windowTitle != windowTitle) {
		return false;
	}
	// このビューで切ってある層の物は描かない（Game ビューのグリッドなど）
	if (!instance_->isLayerVisible_[static_cast<size_t>(target.layer)]) {
		return false;
	}
	// Hierarchy の目で隠した物は、Scene ビューだけ描かない
	return !target.isSceneHidden || instance_->isSceneHiddenVisible_;
}


//=============================================================================
// リクエスト
//=============================================================================
// ===== モデル群リクエスト =====
void RenderQueue::Request(MeshRequest&& req) {
	// PSOKey → SortKey
	// デカールを受けない不透明な物は、描いた所のステンシルに印を付ける（半透明な物は深度を書かないので、デカールはもともと塗らない）
	req.stencilMode = (!req.receivesDecals && req.depthMode == DepthMode::TestWrite) ? StencilMode::MarkNoDecal : StencilMode::None;
	// PSOKey → SortKey
	PSOKey psoKey = PSOManager::GetPSOKey(req.category, req.shadingType, req.blendMode, req.rasterizerType, req.depthMode, req.stencilMode);
	req.sortKey = PSOManager::GetSortKey(psoKey);
	// リクエストに積む
	if (req.depthMode == DepthMode::TestNoWrite) {
		instance_->transparentMeshRequests_.push_back(std::move(req)); // 半透明
	} else {
		instance_->opaqueMeshRequests_.push_back(std::move(req)); // 不透明(TestWrite)
	}
}

// ===== パーティクル群リクエスト =====
void RenderQueue::Request(ParticleRequest&& req) { 
	PSOKey psoKey = PSOManager::GetPSOKey(DrawCategory::Particle, ShadingType::Unlit, req.blendMode, RasterizerType::SolidNone, DepthMode::TestNoWrite);
	req.sortKey = PSOManager::GetSortKey(psoKey);
	instance_->particleRequests_.push_back(std::move(req)); 
}

// ===== スプライト群リクエスト =====
void RenderQueue::Request(SpriteRequest&& req) { instance_->requests2d_.emplace_back(std::move(req)); }

// ===== 文字リクエスト（スプライトと同じ列に積む）=====
void RenderQueue::Request(TextRequest&& req) { instance_->requests2d_.emplace_back(std::move(req)); }

// ===== 図形リクエスト（スプライトと同じ列に積む）=====
void RenderQueue::Request(VectorShapeRequest&& req) { instance_->requests2d_.emplace_back(std::move(req)); }

// ===== 折れ線リクエスト（スプライトと同じ列に積む）=====
void RenderQueue::Request(PolylineRequest&& req) { instance_->requests2d_.emplace_back(std::move(req)); }

// ===== Line群リクエスト =====
void RenderQueue::Request(LineRequest&& req) { instance_->lineRequests_.push_back(std::move(req)); }

// ===== デカールのリクエスト =====
void RenderQueue::Request(DecalRequest&& req) {
	// 深度は読むだけの窓口で結んで描くので、書くことはできない（書く PSO で描くとデバッグレイヤーに止められる）
	HTT_ASSERT_MSG(req.depthMode != DepthMode::TestWrite, "デカールの深度は TestNoWrite か Disable にしてください（深度を読みながら描くので、書けません）");
	instance_->decalRequests_.push_back(std::move(req));
}


//=============================================================================
// Mesh
//=============================================================================
// ===== RootSignature, PSOソート =====
void RenderQueue::FlushMeshList(const std::vector<MeshRequest>& meshes, const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB, const char* name) {
	auto* cmdList = DirectXCommon::GetCommandList();
	// SRVセット
	const D3D12_GPU_DESCRIPTOR_HANDLE heapStart = DirectXCommon::BindSRVHeap(cmdList);
	// ステンシルの参照値（デカールを受けない物が書く印。印を書かない PSO はステンシルを使わないので、全部に同じ値でよい）
	cmdList->OMSetStencilRef(kStencilNoDecalBit);
	// キー初期化
	uint64_t currentKey = UINT64_MAX;
	uint32_t scopeIndex = UINT32_MAX;
	uint32_t currentShaderProg = static_cast<uint32_t>(UINT32_MAX);

	// リクエスト分ループ
	for (const MeshRequest& req : meshes) {
		// このビューで描かない物は飛ばす（別のウィンドウ・切ってある層・目で隠した物）
		if (!IsDrawnInView(req, windowTitle)) {
			continue;
		}
		// --- シェーダー(RootSignature)が変わったとき ---
		uint32_t shaderProg = PSOManager::GetShaderProgramID(req.category, req.shadingType);
		if (shaderProg != currentShaderProg) {
			// GPUProfiler
			if (scopeIndex != UINT32_MAX) {
				GPUProfiler::End(cmdList, scopeIndex);
			}
			// RootSignature
			const RootSignatureInfo& rs = PSOManager::GetRootSignatureInfo(shaderProg);
			cmdList->SetGraphicsRootSignature(rs.rootSignature.Get());
			// CBuffer: Camera
			auto camSlot = rs.slotOf.find(RootBind::Camera);
			if (cameraCB && camSlot != rs.slotOf.end()) {
				cmdList->SetGraphicsRootConstantBufferView(camSlot->second, cameraCB);
			}
			// CBuffer: Texture
			auto texSlot = rs.slotOf.find(RootBind::BindlessTexture);
			if (texSlot != rs.slotOf.end()) {
				cmdList->SetGraphicsRootDescriptorTable(texSlot->second, heapStart);
			}
			// CBuffer: TextureCube
			auto texCubeSlot = rs.slotOf.find(RootBind::BindlessTextureCube);
			if (texCubeSlot != rs.slotOf.end()) {
				cmdList->SetGraphicsRootDescriptorTable(texCubeSlot->second, heapStart);
			}
			currentShaderProg = shaderProg;
		}
		// --- PSOが変わったとき ---
		if (req.sortKey != currentKey) {
			// GPUProfiler
			if (scopeIndex != UINT32_MAX) {
				GPUProfiler::End(cmdList, scopeIndex);
			}
			// PSO
			PSOKey psoKey = PSOManager::GetPSOKey(req.category, req.shadingType, req.blendMode, req.rasterizerType, req.depthMode, req.stencilMode);
			cmdList->SetPipelineState(PSOManager::GetPSO(psoKey).Get());
			scopeIndex = GPUProfiler::Begin(cmdList, (std::string(name) + "/" + PSOManager::GetStateName(psoKey)).c_str());
			currentKey = req.sortKey;
		}

		RenderContext::DrawMesh(req);
	}
	if (scopeIndex != UINT32_MAX) {
		GPUProfiler::End(cmdList, scopeIndex);
	}
}

// ===== 不透明 =====
void RenderQueue::FlushOpaqueMesh(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB) {
	auto& meshes = instance_->opaqueMeshRequests_;
	// 状態切替を減らす＝状態キーでソート
	std::sort(meshes.begin(), meshes.end(), [](const MeshRequest& a, const MeshRequest& b) { return a.sortKey < b.sortKey; });
	FlushMeshList(meshes, windowTitle, cameraCB, "Opaque");
}

void RenderQueue::FlushTransparentMesh(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB) {
	auto& meshes = instance_->transparentMeshRequests_;
	// 正しくブレンド＝奥→手前（距離²の降順）
	std::sort(meshes.begin(), meshes.end(), [](const MeshRequest& a, const MeshRequest& b) { return a.cameraDistanceSq > b.cameraDistanceSq; });
	FlushMeshList(meshes, windowTitle, cameraCB, "Transparent");
}


//=============================================================================
// Decal
//=============================================================================
// ===== 積んだか（無ければ、深度を読む状態に切り替えなくてよい）=====
bool RenderQueue::HasDecal() { return !instance_->decalRequests_.empty(); }

// ===== 発行（Order の順。同じ Order は Shader・状態の順にまとめる）=====
void RenderQueue::FlushDecal(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB, uint32_t depthIndex) {
	auto* cmdList = DirectXCommon::GetCommandList();
	const D3D12_GPU_DESCRIPTOR_HANDLE heapStart = DirectXCommon::BindSRVHeap(cmdList);
	// ステンシルの参照値（受けない物の印。印の無い所だけ塗る）
	cmdList->OMSetStencilRef(kStencilNoDecalBit);

	// --- 並べる：重なりの順（Order）を先に守り、同じ Order の中は PSO が同じ物を続ける ---
	auto& decals = instance_->decalRequests_;
	const auto psoKeyOf = [](const DecalRequest& req) { return PSOKey{req.programId, req.blendMode, req.rasterizerType, req.depthMode, StencilMode::DecalTest}; };
	// std::stable_sort：同じ順の物は、積んだ順のまま並べる（毎フレーム、重なりの上下が入れ替わらないように）
	std::stable_sort(decals.begin(), decals.end(), [&psoKeyOf](const DecalRequest& a, const DecalRequest& b) {
		if (a.order != b.order) {
			return a.order < b.order;
		}
		return PSOManager::GetSortKey(psoKeyOf(a)) < PSOManager::GetSortKey(psoKeyOf(b));
	});

	uint32_t currentShaderProg = UINT32_MAX;
	uint64_t currentKey = UINT64_MAX;
	for (const DecalRequest& req : decals) {
		if (!IsDrawnInView(req, windowTitle)) {
			continue;
		}
		// --- シェーダー(RootSignature)が変わったとき：カメラ・テクスチャを結び直す ---
		if (req.programId != currentShaderProg) {
			const RootSignatureInfo& rs = PSOManager::GetRootSignatureInfo(req.programId);
			cmdList->SetGraphicsRootSignature(rs.rootSignature.Get());
			if (auto camSlot = rs.slotOf.find(RootBind::Camera); cameraCB && camSlot != rs.slotOf.end()) {
				cmdList->SetGraphicsRootConstantBufferView(camSlot->second, cameraCB);
			}
			if (auto texSlot = rs.slotOf.find(RootBind::BindlessTexture); texSlot != rs.slotOf.end()) {
				cmdList->SetGraphicsRootDescriptorTable(texSlot->second, heapStart);
			}
			currentShaderProg = req.programId;
			currentKey = UINT64_MAX; // RootSignature を変えたら PSO も結び直す
		}
		// --- PSO が変わったとき ---
		const PSOKey psoKey = psoKeyOf(req);
		const uint64_t sortKey = PSOManager::GetSortKey(psoKey);
		if (sortKey != currentKey) {
			cmdList->SetPipelineState(PSOManager::GetPSO(psoKey).Get());
			currentKey = sortKey;
		}
		RenderContext::DrawDecal(req, depthIndex);
	}
}


//=============================================================================
// Particleの描画を並び変えて発行
//=============================================================================
void RenderQueue::FlushParticle(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB, bool isBeforeDepthOfField) {
	auto* cmdList = DirectXCommon::GetCommandList();
	// SRVヒープセット
	const D3D12_GPU_DESCRIPTOR_HANDLE heapStart = DirectXCommon::BindSRVHeap(cmdList);
	// 並び替え条件
	std::sort(instance_->particleRequests_.begin(), instance_->particleRequests_.end(), 
		[](const ParticleRequest& a, const ParticleRequest& b) { return a.sortKey < b.sortKey; });
	// 並び替え初期値
	uint64_t currentParticleKey = UINT64_MAX;
	uint32_t scopeIndex = UINT32_MAX;
	bool particleRSSet = false;

	// ===== リクエストが来た分だけループ =====
	for (const ParticleRequest& req : instance_->particleRequests_) {
		// このビューで描かない物は飛ばす（別のウィンドウ・切ってある層・目で隠した物）
		if (!IsDrawnInView(req, windowTitle)) {
			continue;
		}
		// 被写界深度の前・後のうち、今描く方だけ（前の粒は SceneRenderer::RenderOpaque、後の粒は RenderTransparent で描く）
		if (req.isBeforeDepthOfField != isBeforeDepthOfField) {
			continue;
		}
		// --- 最初の1回だけRootSignatureを設定 ---
		if (!particleRSSet) {
			// RootSignature
			const RootSignatureInfo& rs = PSOManager::GetRootSignatureInfo(PSOManager::GetShaderProgramID(DrawCategory::Particle));
			cmdList->SetGraphicsRootSignature(rs.rootSignature.Get());
			// カメラ
			auto camSlot = rs.slotOf.find(RootBind::Camera);
			if (cameraCB && camSlot != rs.slotOf.end()) {
				cmdList->SetGraphicsRootConstantBufferView(camSlot->second, cameraCB);
			}
			// Texture
			auto texSlot = rs.slotOf.find(RootBind::BindlessTexture);
			if (texSlot != rs.slotOf.end()) {
				cmdList->SetGraphicsRootDescriptorTable(texSlot->second, heapStart);
			}
			particleRSSet = true;
		}

		// --- PSOが変わったとき ---
		if (req.sortKey != currentParticleKey) {
			// GPUProfiler: 前の区間を閉じてから新しい区間を開く
			if (scopeIndex != UINT32_MAX) {
				GPUProfiler::End(cmdList, scopeIndex);
			}

			PSOKey psoKey = PSOManager::GetPSOKey(DrawCategory::Particle, ShadingType::Unlit, req.blendMode, RasterizerType::SolidNone, DepthMode::TestNoWrite);
			scopeIndex = GPUProfiler::Begin(cmdList, PSOManager::GetStateName(psoKey));
			cmdList->SetPipelineState(PSOManager::GetPSO(psoKey).Get()); // PSOセット
			currentParticleKey = req.sortKey;
		}
		// DrawCallする関数へ
		RenderContext::DrawParticles(req);
	}
	// GPUProfiler: 計測終了
	if (scopeIndex != UINT32_MAX) {
		GPUProfiler::End(cmdList, scopeIndex);
	}
}


//=============================================================================
// Lineの描画を並び変えて発行
//=============================================================================
void RenderQueue::FlushLine(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB) {
	auto* cmdList = DirectXCommon::GetCommandList();
	// SRVヒープセット
	const D3D12_GPU_DESCRIPTOR_HANDLE heapStart = DirectXCommon::BindSRVHeap(cmdList);
	// 並び替え初期値
	bool lineStateSet = false;        // 最初だけセットするため
	uint32_t scopeIndex = UINT32_MAX; // GPUProfiler用
	DepthMode currentDepth = DepthMode::TestWrite; // 今セットしているPSOの深度設定
	// 線のPSOをセットする
	auto setLinePSO = [cmdList](DepthMode depth) {
		cmdList->SetPipelineState(PSOManager::GetPSO(PSOManager::GetPSOKey(DrawCategory::Line, ShadingType::Unlit, 
			BlendMode::Normal, RasterizerType::SolidBack, depth)).Get());
	};

	// ===== リクエストが来た分だけループ =====
	for (const LineRequest& req : instance_->lineRequests_) {
		// このビューで描かない物は飛ばす（別のウィンドウ・切ってある層・目で隠した物）
		if (!IsDrawnInView(req, windowTitle)) {
			continue;
		}
		// --- 最初のみ RootSignature, PipelineState を切り替える ---
		if (!lineStateSet) {
			// GPUProfiler: 新しい区間のスタート
			PSOKey psoKey = PSOManager::GetPSOKey(DrawCategory::Line, ShadingType::Unlit);
			scopeIndex = GPUProfiler::Begin(cmdList, PSOManager::GetStateName(psoKey));
			// RootSignature
			const RootSignatureInfo& rs = PSOManager::GetRootSignatureInfo(PSOManager::GetShaderProgramID(DrawCategory::Line));
			cmdList->SetGraphicsRootSignature(rs.rootSignature.Get());
			// カメラ
			auto camSlot = rs.slotOf.find(RootBind::Camera);
			if (cameraCB && camSlot != rs.slotOf.end()) {
				cmdList->SetGraphicsRootConstantBufferView(camSlot->second, cameraCB);
			}
			// PSO
			setLinePSO(req.depthMode);
			currentDepth = req.depthMode;
			lineStateSet = true;
		} else if (req.depthMode != currentDepth) {
			setLinePSO(req.depthMode);
			currentDepth = req.depthMode;
		}

		// Draw Callする関数へ
		RenderContext::DrawLines(req);
	}
	// GPUProfiler: 計測終了
	if (scopeIndex != UINT32_MAX) {
		GPUProfiler::End(cmdList, scopeIndex);
	}
}


//=============================================================================
// 2dの描画を並び変えて発行
//=============================================================================
namespace {
// ===== 2Dの描画で、今コマンドリストにセットしている物（同じ間はセットし直さない）=====
struct State2d {
	bool isSet = false;                                // まだ何もセットしていなければ false
	DrawCategory category = DrawCategory::Sprite;      // シェーダー（スプライト・文字・図形）
	BlendMode blend = BlendMode::Normal;               // PSO のブレンド
	RasterizerType raster = RasterizerType::SolidBack; // PSO のラスタライザ
	StencilMode stencil = StencilMode::None;           // PSO のステンシル（UI のマスク）
	bool isStencilRefSet = false;                      // ステンシルの参照値をセットしたか
	uint32_t stencilRef = 0;                           // セットしたステンシルの参照値
};

// ===== 2Dのリクエストの描き分けの情報（どの種類も Draw2dTarget を受け継ぐので、std::visit で1つにする）=====
const Draw2dTarget& TargetOf(const RenderQueue::Request2d& request) {
	return std::visit([](const auto& r) -> const Draw2dTarget& { return r; }, request);
}

// ===== 2Dのリクエストの重ねる順 =====
const UIDrawKey& KeyOf(const RenderQueue::Request2d& request) { return TargetOf(request).drawKey; }

// ===== 描く物に合わせて RootSignature・PSO・ステンシルの参照値をセットする（スプライト・文字・図形が交互に来ても、変わった所だけセットし直す）=====
void Bind2d(ID3D12GraphicsCommandList* cmdList, D3D12_GPU_DESCRIPTOR_HANDLE heapStart, State2d& current, DrawCategory category, BlendMode blend, RasterizerType raster, const Draw2dTarget& target) {
	// --- シェーダーが変わったら、RootSignature とテクスチャの表を付け替える ---
	const bool isCategoryChanged = !current.isSet || current.category != category;
	if (isCategoryChanged) {
		const RootSignatureInfo& rs = PSOManager::GetRootSignatureInfo(PSOManager::GetShaderProgramID(category));
		cmdList->SetGraphicsRootSignature(rs.rootSignature.Get());
		auto texSlot = rs.slotOf.find(RootBind::BindlessTexture);
		if (texSlot != rs.slotOf.end()) {
			cmdList->SetGraphicsRootDescriptorTable(texSlot->second, heapStart);
		}
	}
	// --- PSO（2dは深度無効なので DepthMode::Disable 固定。ステンシルはマスクの使い方で変わる）---
	if (isCategoryChanged || current.blend != blend || current.raster != raster || current.stencil != target.stencil) {
		cmdList->SetPipelineState(PSOManager::GetPSO(PSOManager::GetPSOKey(category, ShadingType::Unlit, blend, raster, DepthMode::Disable, target.stencil)).Get());
	}
	// --- ステンシルの参照値（PSO ではなくコマンドリストに持つ値。マスクの入れ子の段で変わる）---
	if (target.stencil != StencilMode::None && (!current.isStencilRefSet || current.stencilRef != target.stencilRef)) {
		cmdList->OMSetStencilRef(target.stencilRef);
		current.isStencilRefSet = true;
		current.stencilRef = target.stencilRef;
	}
	current.isSet = true;
	current.category = category;
	current.blend = blend;
	current.raster = raster;
	current.stencil = target.stencil;
}
} // namespace

void RenderQueue::Flush2d(const std::wstring& windowTitle) {
	auto* cmdList = DirectXCommon::GetCommandList();
	// SRVヒープセット
	const D3D12_GPU_DESCRIPTOR_HANDLE heapStart = DirectXCommon::BindSRVHeap(cmdList);

	// drawKey の順（奥から）に並べる。ウィンドウの数だけ呼ばれるが、2回目からは並んでいるので順番は変わらない
	// 同じ drawKey（文字の影と文字など）は積んだ順のまま（stable_sort）
	std::ranges::stable_sort(instance_->requests2d_, [](const UIDrawKey& a, const UIDrawKey& b) { return a < b; }, KeyOf);

	State2d current;
	for (const Request2d& request : instance_->requests2d_) {
		// このビューで描かない物は飛ばす（別のウィンドウ・切ってある層・目で隠した UI）
		if (!IsDrawnInView(TargetOf(request), windowTitle)) {
			continue;
		}
		// --- スプライト ---
		if (const SpriteRequest* sprite = std::get_if<SpriteRequest>(&request)) {
			Bind2d(cmdList, heapStart, current, DrawCategory::Sprite, sprite->blendMode, sprite->rasterizerType, *sprite);
			RenderContext::DrawSprite(*sprite);
			continue;
		}
		// --- 文字（両面を描く。回して裏返っても消えないように）---
		if (const TextRequest* text = std::get_if<TextRequest>(&request)) {
			Bind2d(cmdList, heapStart, current, DrawCategory::Text, text->blendMode, RasterizerType::SolidNone, *text);
			RenderContext::DrawText2d(*text);
			continue;
		}
		// --- 図形（両面を描く。回して裏返っても消えないように）---
		if (const VectorShapeRequest* shape = std::get_if<VectorShapeRequest>(&request)) {
			Bind2d(cmdList, heapStart, current, DrawCategory::VectorShape, shape->blendMode, RasterizerType::SolidNone, *shape);
			RenderContext::DrawVectorShape(*shape);
			continue;
		}
		// --- 折れ線（三角形の向きはそろえていないので、両面を描く）---
		if (const PolylineRequest* polyline = std::get_if<PolylineRequest>(&request)) {
			Bind2d(cmdList, heapStart, current, DrawCategory::Polyline, polyline->blendMode, RasterizerType::SolidNone, *polyline);
			RenderContext::DrawPolyline(*polyline);
		}
	}
}