#include "HTTEngine/Graphics/Profiling/GPUProfiler.h"
#include <algorithm>
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Graphics/GPU/DirectXCommon.h"
#include "HTTEngine/Graphics/GPU/GpuResources.h"

// 静的メンバ変数
std::unique_ptr<GPUProfiler> GPUProfiler::instance_ = nullptr;


//=============================================================================
// 初期化
//=============================================================================
void GPUProfiler::Initialize(UINT maxScopes) { 
	HTT_ASSERT_MSG(instance_ == nullptr, "Initialize()が2回以上呼ばれています");
	instance_ = std::make_unique<GPUProfiler>(ConstructorKey());

	auto* device = DirectXCommon::GetDevice();
	instance_->maxScopes_ = maxScopes; 
	instance_->names_.resize(maxScopes);
	// クエリヒープ
	D3D12_QUERY_HEAP_DESC queryDesc{};
	queryDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	queryDesc.Count = maxScopes * 2;
	device->CreateQueryHeap(&queryDesc, IID_PPV_ARGS(&instance_->queryHeap_));
	instance_->queryHeap_->SetName(L"GPUProfiler_TimestampQueryHeap");
	// ReadbackBuffer
	instance_->readbackBuffer_ = GpuResources::CreateReadbackBuffer(maxScopes * 2 * sizeof(UINT64));
	instance_->readbackBuffer_->SetName(L"GPUProfiler_ReadbackBuffer");
	instance_->readbackBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&instance_->mappedTicks_));
	// 換算係数（1秒あたりのtick数）
	DirectXCommon::GetCommandQueue()->GetTimestampFrequency(&instance_->frequency_);
	LogManager::Log("Initialized");
}

//=============================================================================
// 解放
//=============================================================================
void GPUProfiler::Release() { 
	instance_->queryHeap_.Reset();
	instance_->readbackBuffer_.Reset();
	instance_.reset();
}

//=============================================================================
// フレームの最初に呼ぶ
//=============================================================================
void GPUProfiler::BeginFrame() { instance_->scopeCount_ = 0; }

//=============================================================================
// 区間開始
//=============================================================================
uint32_t GPUProfiler::Begin(ID3D12GraphicsCommandList* cmd, const char* name) { 
	uint32_t index = instance_->scopeCount_++; 
	if (index >= instance_->maxScopes_) {
		return index;
	}
	instance_->names_[index] = name;
	// 開始tick
	cmd->EndQuery(instance_->queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index * 2);
	return index;
}

//=============================================================================
// 区間終了
//=============================================================================
void GPUProfiler::End(ID3D12GraphicsCommandList* cmd, uint32_t scopeIndex) { 
	if (scopeIndex >= instance_->maxScopes_) {
		return;
	}
	// 終了tick
	cmd->EndQuery(instance_->queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, scopeIndex * 2 + 1);
}

//=============================================================================
// 記録したtickを ReadbackBufferへコピー
//=============================================================================
void GPUProfiler::Resolve(ID3D12GraphicsCommandList* cmd) {
	// 早期リターン
	if (instance_->scopeCount_ == 0) {
		return;
	}
	UINT count = std::min(instance_->scopeCount_, instance_->maxScopes_) * 2;
	// Queryヒープの内容をReadbackバッファへコピー
	cmd->ResolveQueryData(instance_->queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, count, instance_->readbackBuffer_.Get(), 0);
}

//=============================================================================
// msに変換してresults_にいれる
//=============================================================================
void GPUProfiler::Readback() {
	instance_->results_.clear(); 
	UINT count = std::min(instance_->scopeCount_, instance_->maxScopes_);
	// 早期リターン
	if (count == 0 || instance_->frequency_ == 0) {
		return;
	}
	// 永続ポインタから読む
	for (UINT i = 0; i < count; ++i) {
		UINT64 begin = instance_->mappedTicks_[i * 2];
		UINT64 end = instance_->mappedTicks_[i * 2 + 1];
		double ms = (end > begin) ? double(end - begin) * 1000.0 / double(instance_->frequency_) : 0.0;
		instance_->results_.push_back({instance_->names_[i], ms});
	}
}