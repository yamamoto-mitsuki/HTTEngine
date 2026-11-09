#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

#include <externals/imgui/imgui.h>

/// <summary>
/// Project ウィンドウに出す、画像ファイルの小さい絵（サムネイル）
/// <para>元の画像を小さく縮めて（長い辺 kThumbnailSize ピクセル）GPU に置き、覚えておく。元の画像のテクスチャは作らない（4K の画像でも軽い）</para>
/// <para>作るのは1フレームに1枚まで（大きい画像を何枚も一度に読むと止まって見えるので）。まだ作っていない間は false を返すので、呼ぶ側はアイコンで代わりに描く</para>
/// </summary>
namespace AssetThumbnails {
// 小さい絵の長い辺のピクセル数
inline constexpr uint32_t kThumbnailSize = 128;

// path（画像ファイル。AssetType::Texture の拡張子）の小さい絵を取る。無ければ作る
// writeTime はファイルの更新時刻（作ったときと違えば、描き直された画像なので作り直す）
// 取れたら texture と size（絵のピクセル数。縦横比を保って描くのに使う）を入れて true
bool Get(const std::string& path, std::filesystem::file_time_type writeTime, ImTextureID& texture, ImVec2& size);

// 全部捨てる（終了時。GPU を待った後に呼ぶ）
void Release();
} // namespace AssetThumbnails