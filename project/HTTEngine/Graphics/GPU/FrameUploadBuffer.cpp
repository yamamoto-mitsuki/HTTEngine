#include "FrameUploadBuffer.h"

#include <format>

#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Graphics/GPU/DirectXCommon.h"
#include "HTTEngine/Graphics/GPU/GpuResources.h"
#include "HTTEngine/String/ConvertString.h"


//=============================================================================
// 初期化 / 解放
//=============================================================================
void FrameUploadBuffer::Initialize(size_t capacity, const std::string& name) {
	HTT_ASSERT_MSG(!buffer_, "FrameUploadBuffer::Initialize が2回呼ばれました");
	buffer_ = GpuResources::CreateMappedUploadBuffer(capacity, reinterpret_cast<void**>(&mappedPtr_));
	buffer_->SetName(ConvertString(name).c_str());
	gpuBase_ = buffer_->GetGPUVirtualAddress();
	capacity_ = capacity;
	offset_ = 0;
	LogManager::Log(std::format("{} : {} MB", name, capacity / (1024 * 1024)));
}

void FrameUploadBuffer::Release() {
	buffer_ = nullptr; // Mapしたまま解放してよい（リソースが消えるときに外れる）
	mappedPtr_ = nullptr;
	gpuBase_ = 0;
	capacity_ = 0;
	offset_ = 0;
}


//=============================================================================
// 確保
//=============================================================================
FrameUploadBuffer::Allocation FrameUploadBuffer::Allocate(size_t size, size_t alignment) {
	HTT_ASSERT_MSG(mappedPtr_, "FrameUploadBuffer::Initialize を先に呼んでください");
	HTT_ASSERT_MSG(alignment != 0 && (alignment & (alignment - 1)) == 0, "alignmentは2のべき乗にしてください");

	size_t start = AlignUp(offset_, alignment); // 前のデータの続きを、alignmentの倍数にそろえた位置から使う
	HTT_ASSERT_MSG(start + size <= capacity_, std::format("FrameUploadBufferの容量が足りません（{} / {} バイト）。RenderContext::kFrameUploadBytes を増やしてください", start + size, capacity_));
	offset_ = start + size; // 次はこの後ろから

	Allocation allocation;
	allocation.cpuAddress = mappedPtr_ + start;
	allocation.gpuAddress = gpuBase_ + start;
	allocation.size = size;
	return allocation;
}


//=============================================================================
// フレームの終わり
//=============================================================================
void FrameUploadBuffer::Reset() {
	lastFrameUsedBytes_ = offset_; // 使った量を残してから戻す
	offset_ = 0;
}