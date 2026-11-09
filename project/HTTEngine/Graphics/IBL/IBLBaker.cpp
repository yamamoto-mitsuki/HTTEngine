#include "IBLBaker.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Diagnostics/LogManager.h"

std::unique_ptr<IBLBaker> IBLBaker::instance_ = nullptr;


//=============================================================================
// 初期化・解放
//=============================================================================
// ===== 初期化 =====
void IBLBaker::Initialize() {
	HTT_ASSERT_MSG(instance_ == nullptr, "Initialize()を2回以上呼んでいます");
	instance_ = std::make_unique<IBLBaker>(ConstructorKey());

	instance_->equirectPass_.Initialize("EquirectToCubePS");
	instance_->irradiancePass_.Initialize("IrradianceConvolutionPS");
	instance_->prefilterPass_.Initialize();
    instance_->brdfLutPass_.Initialize();

	LogManager::Log("IBLBaker Initialized");
}

// ===== 解放 =====
void IBLBaker::Finalize() {
	HTT_ASSERT_MSG(instance_ != nullptr, "Initialize()より先にFinalize()が呼ばれています");
	instance_.reset();
	LogManager::Log("IBLBaker Finalized");
}