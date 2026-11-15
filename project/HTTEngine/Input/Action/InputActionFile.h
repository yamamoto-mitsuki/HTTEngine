#pragma once
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

#include "HTTEngine/Input/Action/InputActionMap.h"

/// <summary>
/// ゲームの Action の一覧と既定の割り当てのファイル（Parameters/Input/InputActions.json。git に入れて、チームで1つ）
/// <para>Action 1つの決まり（InputActionDesc）を、並び（画面に出す順）のまま読み書きする。割り当ては段階1の文字（"Key.Space"・"Key.W:+Y"）</para>
/// <para>書いていない項目は既定の値（label が無ければ名前、type が無ければ Button）。Save は既定の値の項目を書かない（ファイルを短く、差分を読みやすく）</para>
/// <para>表（InputActionMap）はファイルを知らない。ゲームの表（GameInput）と、エディタの Input ページ（GameInputPage）がこれで読む</para>
/// </summary>
/// <example>
/// {
///     "version": 1,
///     "actions": [
///         { "name": "Jump", "label": "ジャンプ", "group": "動き", "bindings": ["Key.Space", "Pad.A"] },
///         { "name": "Move", "type": "Vector2", "clampLength": true, "bindings": ["Key.W:+Y", "Key.S:-Y", "Key.A:-X", "Key.D"] }
///     ]
/// }
/// </example>
namespace InputActionFile {
// 読む（ファイルが無い・読めなければ nullopt。読めないときだけログに出す）
// 名前が空・重なっている Action、知らない種類・読めない割り当ては飛ばしてログに出す（落とさない）
std::optional<std::vector<InputActionDesc>> Load(const std::filesystem::path& path);
// 書く（フォルダが無ければ作る）。書けなければ false
bool Save(const std::filesystem::path& path, std::span<const InputActionDesc> actions);
} // namespace InputActionFile
