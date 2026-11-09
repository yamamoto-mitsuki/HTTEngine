#pragma once


/// <summary>
/// 1フレームの統計（GPU の時間・FPS・メモリ・Draw Call など）を Profiler に書く
/// </summary>
namespace FrameStats {

/// <summary>
/// 統計を Profiler に書いて、フレームごとの数え直しをする。描画の後に1回呼ぶ（USE_IMGUI のときだけ）
/// </summary>
void Report();

} // namespace FrameStats
