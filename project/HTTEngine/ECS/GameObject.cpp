#include "GameObject.h"

#include <vector>

#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Scene/Prefab/PrefabManager.h"

//=============================================================================
// 作る・探す
//=============================================================================
GameObject GameObject::Create(const std::string& name, GameObject parent) { return {EntityManager::Create(name, parent.handle)}; }
GameObject GameObject::Find(const std::string& name) { return {EntityManager::FindByName(name)}; }
GameObject GameObject::Instantiate(uint32_t prefab, const Vector3& position, GameObject parent) { return {PrefabManager::Instantiate(prefab, position, parent.handle)}; }
GameObject GameObject::FindChild(const std::string& name) const {
	// 子を並べて、名前が同じ物を探す（孫までは見ない）
	std::vector<Handle<Entity>> children;
	EntityManager::GetChildren(handle, children);
	for (Handle<Entity> child : children) {
		const Entity* entity = EntityManager::Get(child);
		if (entity != nullptr && entity->name == name) {
			return {child};
		}
	}
	return {}; // 無い（if (found) が false になる）
}

//=============================================================================
// 有効・無効
//=============================================================================
void GameObject::SetActive(bool isActive) const {
	if (Entity* entity = EntityManager::Get(handle)) {
		entity->isActive = isActive;
	}
}

bool GameObject::IsActiveSelf() const {
	const Entity* entity = EntityManager::Get(handle);
	return entity != nullptr && entity->isActive;
}

bool GameObject::IsActiveInHierarchy() const { return EntityManager::IsActiveInHierarchy(handle); }

//=============================================================================
// レイヤー
//=============================================================================
int GameObject::GetLayer() const {
	const Entity* entity = EntityManager::Get(handle);
	return (entity != nullptr) ? entity->layer : 0;
}

void GameObject::SetLayer(int newLayer) const {
	Entity* entity = EntityManager::Get(handle);
	if (entity != nullptr && newLayer >= 0 && newLayer < 32) {
		entity->layer = static_cast<uint8_t>(newLayer);
	}
}

//=============================================================================
// 位置・名前
//=============================================================================
TransformComponent& GameObject::GetTransform() const {
	// Transform は全部の Entity が必ず持っている（外せない）ので、無いのは「もう消えた GameObject」のときだけ
	TransformComponent* transform = EntityManager::Get<TransformComponent>(handle);
	HTT_ASSERT_MSG(transform != nullptr, "消えた GameObject の transform を読みました（if (gameObject) で確かめてから読んでください）");
	return *transform;
}

std::string GameObject::GetName() const {
	const Entity* entity = EntityManager::Get(handle);
	return entity ? entity->name : std::string();
}

void GameObject::SetName(const std::string& newName) const {
	if (Entity* entity = EntityManager::Get(handle)) {
		entity->name = newName;
	}
}