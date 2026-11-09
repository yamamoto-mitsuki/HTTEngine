#include "RenderTextureManager.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Diagnostics/LogManager.h"

std::unique_ptr<RenderTextureManager> RenderTextureManager::instance_ = nullptr;


//=============================================================================
// 初期化・解放
//=============================================================================
// ===== 初期化 =====
void RenderTextureManager::Initialize() { 
	HTT_ASSERT_MSG(instance_ == nullptr, "Initialize()を2回以上呼んでいます");
	instance_ = std::make_unique<RenderTextureManager>(ConstructorKey());
}

// ===== 解放 =====
void RenderTextureManager::Finalize() {
	HTT_ASSERT_MSG(instance_ != nullptr, "Initialize()より先にFinalize()が呼ばれています");
	instance_->renderTextures_.clear();
	instance_.reset();
}


//=============================================================================
// 生成
//=============================================================================
RenderTextureManager::Handle RenderTextureManager::Create(uint32_t width, uint32_t height, RenderTextureFormat format, bool useDepth) {
	HTT_ASSERT_MSG(instance_, "Initialize()を先に呼んでください");

	uint32_t index = static_cast<uint32_t>(instance_->renderTextures_.size()); // 現在のrenderTexturesのサイズを index に
	instance_->renderTextures_.push_back(std::make_unique<RenderTexture>(width, height, format, useDepth));

	return Handle{index};
}


//=============================================================================
// 取得
//=============================================================================
RenderTexture* RenderTextureManager::Get(Handle handle) {
	HTT_ASSERT_MSG(instance_, "Initialize()を先に呼んでください");
	HTT_ASSERT_MSG(handle.IsValid(), "無効なHandleです");
	HTT_ASSERT_MSG(handle.index < instance_->renderTextures_.size(), "Handleが範囲外です");

	RenderTexture* rt = instance_->renderTextures_[handle.index].get();
	HTT_ASSERT_MSG(rt != nullptr, "そのHandleのRenderTextureは既に破棄されています");
	return rt;
}