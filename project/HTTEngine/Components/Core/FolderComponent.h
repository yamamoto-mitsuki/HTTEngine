#pragma once
#include <type_traits>

// Hierarchy を整理するためのフォルダ。Hierarchy の ＋ → Folder で作る（Add Component には出さない）
// 中身は無い「印」。この印が付いた Entity は、Hierarchy でフォルダの絵で描き、Inspector では Transform・Add Component を出さない
// 中に入れた物（子）の位置が変わらないように、フォルダの Transform はいつも原点・回転なし・大きさ1のまま使う
// → 動かすギズモを出さない・フォルダを入れられるのは一番上か、ほかのフォルダの中だけ
// 有効・無効（Inspector の左上のチェック）は、普通の Entity と同じく中身ごと切り替わる（Unity の空の GameObject と同じ）

/// <summary>
/// フォルダの印
/// </summary>
struct FolderComponent {};

static_assert(std::is_trivially_copyable_v<FolderComponent>);