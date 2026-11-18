#include "AttackPart.h"

#include <map>
#include <format>

#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Systems/Combat/HitSystem.h"

namespace {
// ===== 表の1行 =====
struct PartEntry {
	AttackPartRegistry::CreateFunction create; // 作り方
	std::string description;                   // 部品の欄に出す割合
};

// ===== 名前 → 作り方と説明の表 =====
std::map<std::string, PartEntry, std::less<>>& PartTable() { 
	static std::map<std::string, PartEntry, std::less<>> table;
	return table;
}
} // namespace


//=============================================================================
// 登録・作る
//=============================================================================
void AttackPartRegistry::Register(const char* name, CreateFunction create, const char* description) {
	// 同じ名前は先に登録した方を使う
	if (PartTable().contains(name)) {
		LogManager::Warning(std::format("攻撃部品 \"{}\" はもう登録されています。先に登録した方（ゲームの部品）を使います", name));
		return;
	}
	PartTable()[name] = {create, description};
}

std::unique_ptr<AttackPart> AttackPartRegistry::Create(std::string_view name) {
	const auto found = PartTable().find(name);
	return (found != PartTable().end()) ? found->second.create() : nullptr;
}

std::vector<std::string> AttackPartRegistry::GetNames() {
	std::vector<std::string> names;
	names.reserve(PartTable().size());
	for (const auto& [name, entry] : PartTable()) {
		names.push_back(name);
	}
	return names;
}

const std::string& AttackPartRegistry::GetDescription(std::string_view name) {
	static const std::string kEmpty; // 見つからないときに返す空文字（参照で返すので static にする）
	const auto found = PartTable().find(name);
	return (found != PartTable().end()) ? found->second.description : kEmpty;
}


//=============================================================================
// ステップへの頼み
//=============================================================================
// ===== 頼みを読む（読んだら無しに戻す）=====
AttackStepRequest AttackPart::TakeStepRequest() {
	const AttackStepRequest request = stepRequest_;
	stepRequest_ = AttackStepRequest::None;
	return request;
}

// ===== ステップを終える =====
void AttackPart::FinishStep() {
	// 同じフレームに FailStep も呼んでいたら、失敗の方を残す
	if (stepRequest_ != AttackStepRequest::Fail) {
		stepRequest_ = AttackStepRequest::Finish;
	}
}

// ===== ステップを失敗で終える =====
void AttackPart::FailStep() { stepRequest_ = AttackStepRequest::Fail; }


//=============================================================================
// 攻撃部品の道具
//=============================================================================
// ===== Hitbox の窓を、今のステップの種類に合わせる =====
void AttackPart::SyncHitbox(GameObject hitbox, const AttackPartContext& context) { 
	const float remaining = (std::max)(context.stepDuration - context.stepTime, 0.0f); // 今のステップの残り時間 
	switch (context.stage) { 
	case AttackStage::Windup:
		HitSystem::Warn(hitbox.handle, remaining); // 予告
		break;
	case AttackStage::Active:
		HitSystem::Activate(hitbox.handle, remaining);
		break;
	case AttackStage::Recovery:
	case AttackStage::None:
		HitSystem::Deactivate(hitbox.handle);
		break;
	}
}