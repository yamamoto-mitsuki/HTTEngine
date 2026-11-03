#include "MotionQuery.h"

#include <memory>
#include <vector>

#include "MyEngine/Diagnostics/MyAssert.h"

namespace {
/// <summary>
/// MotionQuery が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct MotionQueryState {
	std::vector<MotionQuery::Getter> getters; // 答え方（足した順）
};
std::unique_ptr<MotionQueryState> state;
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
void MotionQuery::Initialize() {
	MY_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<MotionQueryState>();
}

void MotionQuery::Release() { state.reset(); }

void MotionQuery::Register(Getter getter) {
	MY_ASSERT_MSG(state != nullptr, "MotionQuery::Initialize()を先に呼んでください");
	MY_ASSERT_MSG(getter != nullptr, "答え方が空です");
	state->getters.push_back(getter);
}


//=============================================================================
// 聞く
//=============================================================================
bool MotionQuery::TryGet(Handle<Entity> entity, MotionState& out) {
	MY_ASSERT_MSG(state != nullptr, "MotionQuery::Initialize()を先に呼んでください");
	for (const Getter getter : state->getters) {
		if (getter(entity, out)) {
			return true;
		}
	}
	return false;
}

bool MotionQuery::Has(Handle<Entity> entity) {
	MotionState unused;
	return TryGet(entity, unused);
}

Vector3 MotionQuery::GetVelocity(Handle<Entity> entity) {
	MotionState motion;
	return TryGet(entity, motion) ? motion.velocity : Vector3{0.0f, 0.0f, 0.0f};
}

bool MotionQuery::IsGrounded(Handle<Entity> entity) {
	MotionState motion;
	return TryGet(entity, motion) && motion.isGrounded;
}
