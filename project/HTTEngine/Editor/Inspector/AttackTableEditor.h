#pragma once

/// <summary>
/// .attacktable のファイルを Inspector で直す係（AssetInspector に登録する）。エディタだけ
/// <para>行の足し引きがあるので、Physic Material と違って ImGui を直接使う（1行の中身は AttackTableManager::Describe〜 に任せる）</para>
/// </summary>
class AttackTableEditor {
public:
	// AssetInspector に登録する
	static void Initialize();
};