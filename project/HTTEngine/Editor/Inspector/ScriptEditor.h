#pragma once
#include <span>
#include <vector>
#include <string>
#include <utility>
#include <cstddef>
#include <typeindex>

#include "HTTEngine/Editor/Inspector/ComponentEditor.h"

/// <summary>
/// Script 1種類分の「エディタでの扱い方」（Inspector の区画・Add Component・Undo）
/// <para>SCRIPT(...) で登録した種類ごとに、ScriptSystem::RegisterType が1つ作る。ゲームは直接触らない</para>
/// <para>データの Component（TypedComponentEditor）との違いは、中身の写し方だけ：バイト列ではなく、保存と同じ JSON の文字にする
/// （Script は仮想関数や std::vector を持つので、バイト列のまま写すと壊れる）</para>
/// </summary>
class ScriptEditor final : public ComponentEditor {
public:
	// name・sourceFile はコンパイラが決めた文字列（SCRIPT(...) の型の名前・ファイル）なので、プログラムの終わりまで消えない
	// category は "Gameplay" か "Gameplay/サブカテゴリ"（COMPONENT(...) と同じ所に並ぶ）
	ScriptEditor(const char* name, std::type_index type, const char* sourceFile, std::string category) : name_(name), type_(type), sourceFile_(sourceFile), category_(std::move(category)) {}

	// ===== 種類の情報 =====
	const char* GetName() const override { return name_; }
	const char* GetCategory() const override { return "Scripts"; }
	const char* GetSourceFile() const override { return sourceFile_; }
	bool IsScript() const override { return true; }

	// ===== 実体の出し入れ =====
	void* Get(Handle<Entity> handle) const override;
	// Script は予約せず、その場で付ける（呼ばれるのは EditorHistory の Flush の中＝フレームの境目だけ）
	void RequestAdd(Handle<Entity> handle, std::span<const std::byte> initial) const override;
	// その場で外す（同上）
	void RequestRemove(Handle<Entity> handle) const override;

	// ===== 中身の写し（JSON の文字）=====
	std::vector<std::byte> Save(const void* component) const override;
	void Load(void* component, std::span<const std::byte> data) const override;

	// ===== Inspectorの中身（Enabled → Script::Fields）=====
	void Draw(void* component) const override;

private:
	const char* name_;       // Inspector の見出し・Add Component の表示名（＝型の名前）
	std::type_index type_;   // どの型の Script か
	const char* sourceFile_; // SCRIPT(...) を書いたファイル（見出しにマウスを乗せると出る・Edit Script で開く）
	std::string category_;   // Add Component のカテゴリ（"Gameplay/サブカテゴリ"）
};