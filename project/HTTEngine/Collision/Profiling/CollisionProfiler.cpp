#include "HTTEngine/Collision/Profiling/CollisionProfiler.h"

#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Editor/Profiler.h"
#include "HTTEngine/Editor/ImGuiManager.h"

// 静的メンバ変数
std::unique_ptr<CollisionProfiler> CollisionProfiler::instance_ = nullptr;

void CollisionProfiler::Initialize() { 
	HTT_ASSERT_MSG(instance_ == nullptr, "Initialize()が2回以上呼び出されています");
	instance_ = std::make_unique<CollisionProfiler>(ConstructorKey());
}

void CollisionProfiler::Release() { 
	HTT_ASSERT_MSG(instance_ != nullptr, "Initialize()を呼び出していません");
	instance_.reset();
}

void CollisionProfiler::Record(const char* funcName, double elapsedMs) {
	auto& s = instance_->stats_[funcName];
	s.callCount++;
	s.totalMs += elapsedMs;
	instance_->totalCalls_++;
}

void CollisionProfiler::Reset() { 
	instance_->stats_.clear();
	instance_->totalCalls_ = 0;
}

uint64_t CollisionProfiler::GetTotalCalls() { return instance_->totalCalls_; }

const std::unordered_map<std::string, CollisionProfiler::FunctionStats>& CollisionProfiler::GetAllStats() { return instance_->stats_; }

#ifdef USE_IMGUI
void CollisionProfiler::Report() {
	// このカテゴリ・グループに各関数の統計を並べる
	auto group = Profiler::Category(ProfCategory::Physics).Group("Collision");
	for (const auto& [name, s] : instance_->stats_) {
		group.Text(name, std::format("{} calls  avg:{:.4f} ms", s.callCount, s.AvgMs()));
	}
	Profiler::Category(ProfCategory::Physics).Value("Total", instance_->totalCalls_);
}
#endif