#include "ScriptEditor.h"

#include <string>
#include <string_view>

#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/Script.h"
#include "HTTEngine/Systems/Gameplay/ScriptSystem.h"

namespace {
// バイト列 ↔ 文字（写しの中身は JSON の文字なので、そのまま読み替える）
std::string_view AsText(std::span<const std::byte> data) { return std::string_view(reinterpret_cast<const char*>(data.data()), data.size()); }
} // namespace

//=============================================================================
// 実体の出し入れ
//=============================================================================
// Get が返すのは Script*（void* にしてある）。Draw・Save・Load で Script* に戻す
void* ScriptEditor::Get(Handle<Entity> handle) const { return ScriptSystem::Get(handle, type_); }

void ScriptEditor::RequestAdd(Handle<Entity> handle, std::span<const std::byte> initial) const {
	Script* script = ScriptSystem::Add(handle, type_);
	// 外した Script を Undo で戻すときは、外す前の値も戻す
	if (script != nullptr && !initial.empty()) {
		ScriptSystem::LoadFields(*script, AsText(initial));
	}
}

void ScriptEditor::RequestRemove(Handle<Entity> handle) const { ScriptSystem::RemoveNow(handle, type_); }

//=============================================================================
// 中身の写し
//=============================================================================
std::vector<std::byte> ScriptEditor::Save(const void* component) const {
	// Script::Fields は const ではない関数（読む係が値を書き込むため）。書く係は値を変えないので、const を外して渡してよい
	Script& script = *static_cast<Script*>(const_cast<void*>(component));
	const std::string text = ScriptSystem::SaveFields(script);
	const auto* bytes = reinterpret_cast<const std::byte*>(text.data());
	return std::vector<std::byte>(bytes, bytes + text.size());
}

void ScriptEditor::Load(void* component, std::span<const std::byte> data) const { ScriptSystem::LoadFields(*static_cast<Script*>(component), AsText(data)); }

//=============================================================================
// Inspectorの中身
//=============================================================================
void ScriptEditor::Draw(void* component) const { ScriptSystem::Describe(*static_cast<Script*>(component), GetInspectorUI()); }