#include "HTTEngine/ECS/GameComponent.h"

#include <vector>

namespace {
// 登録待ちの表。関数の中のstaticで持つので、他のstatic変数との初期化の順番に左右されない
std::vector<std::function<void()>>& PendingComponents() {
	static std::vector<std::function<void()>> pending;
	return pending;
}

// 毎フレーム呼ぶ処理の表
struct RegisteredSystem {
	const char* name;               // SYSTEM(関数名) の関数名（確認用）
	std::function<void(float)> run; // Entityを回して関数を呼ぶ処理
};
std::vector<RegisteredSystem>& Systems() {
	static std::vector<RegisteredSystem> systems;
	return systems;
}

size_t appliedComponentCount = 0; // 登録済みのComponentの数
} // namespace

//=============================================================================
// COMPONENT / SYSTEM から呼ばれる（mainより前）
//=============================================================================
void ComponentRegistryDetail::AddPending(std::function<void()> apply) { PendingComponents().push_back(std::move(apply)); }

void ComponentRegistryDetail::AddSystem(const char* name, std::function<void(float)> run) { Systems().push_back({name, std::move(run)}); }

//=============================================================================
// 登録待ちをまとめて登録する（EntityManager::Initialize の後）
//=============================================================================
void GameComponentRegistry::ApplyAll() {
	// 表を取り出してから回す（2回呼ばれても二重に登録しない）
	std::vector<std::function<void()>> pending;
	pending.swap(PendingComponents());
	for (const std::function<void()>& apply : pending) {
		apply();
	}
	appliedComponentCount += pending.size();
}

//=============================================================================
// SYSTEM(...) で登録した処理を全部呼ぶ
//=============================================================================
void GameComponentRegistry::UpdateAll(float deltaTime) {
	for (const RegisteredSystem& system : Systems()) {
		system.run(deltaTime);
	}
}

//=============================================================================
// 数（確認用）
//=============================================================================
size_t GameComponentRegistry::GetComponentCount() { return appliedComponentCount; }

size_t GameComponentRegistry::GetSystemCount() { return Systems().size(); }