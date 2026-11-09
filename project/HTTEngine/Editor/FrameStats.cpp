#include "HTTEngine/Editor/FrameStats.h"

#ifdef USE_IMGUI
#include <cstdint>

#include <Windows.h>
#include <psapi.h>

#include <externals/imgui/imgui.h>

#include "HTTEngine/Collision/Profiling/CollisionProfiler.h"
#include "HTTEngine/Editor/Profiler.h"
#include "HTTEngine/Graphics/GPU/DirectXCommon.h"
#include "HTTEngine/Graphics/Profiling/GPUProfiler.h"
#include "HTTEngine/Graphics/Renderer/RenderContext.h"
#include "HTTEngine/Graphics/Renderer/SkinCache.h"
#include "HTTEngine/Time/EngineTime.h"

#pragma comment(lib, "psapi.lib")

namespace {
//=============================================================================
// 項目ごとの統計
//=============================================================================
// ===== GPU の時間と FPS =====
void ReportTimes() {
	// 処理時間の結果を保存（GPU）
	GPUProfiler::Readback();
	for (const GPUProfiler::Result& r : GPUProfiler::GetResults()) {
		Profiler::Category(ProfCategory::GPU).Group("Times").Value(r.name, r.ms, "ms");
	}
	// Frameの結果
	Profiler::Category(ProfCategory::Frame).Value("FPS", ImGui::GetIO().Framerate);
	Profiler::Category(ProfCategory::Frame).Value("Frame Time", Time::GetUnscaleDeltaTime() * 1000.0f, "ms");
}

// ===== VRAM とメモリ =====
void ReportMemory() {
	// VRAM（IDXGIAdapter3経由で取得しているのでゲーム実行中だけではなく、システム全体の量）
	IDXGIAdapter4* adapter = DirectXCommon::GetAdapter4();
	if (adapter) {
		DXGI_QUERY_VIDEO_MEMORY_INFO info{};
		adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info);
		float vramGB = info.CurrentUsage / (1024.0f * 1024.0f * 1024.0f);
		Profiler::Category(ProfCategory::Memory).Value("System VRAM", vramGB, "GB");
	}

	// メモリ使用量（Windows API経由で取得しているのでゲーム実行中だけではなく、システム全体の量）
	PROCESS_MEMORY_COUNTERS pmc{};
	if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
		float memMB = pmc.WorkingSetSize / (1024.0f * 1024.0f);
		Profiler::Category(ProfCategory::Memory).Value("System WorkingSet", memMB, "MB");
	}
}

// ===== GPU の使用量 =====
void ReportGPUStats() {
	// DrawCall数の確定とリセット
	Profiler::Category(ProfCategory::GPU).Group("Stats").Value("Draw Calls", DirectXCommon::GetDrawCallCount());
	DirectXCommon::ResetDrawCallCount();

	// CS で曲げた頂点のバッファの数（スキンのあるモデルを置いた数。Play / Stop を繰り返しても増え続けなければよい）
	Profiler::Category(ProfCategory::GPU).Group("Stats").Value("Skin Buffers", static_cast<uint32_t>(SkinCache::GetBufferCount()));

	// 1フレームにGPU用に書いたデータの量（RenderContextのFrameUploadBuffer）
	constexpr float kBytesToMB = 1.0f / (1024.0f * 1024.0f);
	float uploadMB = static_cast<float>(RenderContext::GetLastFrameUploadBytes()) * kBytesToMB;
	float uploadCapacityMB = static_cast<float>(RenderContext::GetFrameUploadCapacity()) * kBytesToMB;
	Profiler::Category(ProfCategory::GPU).Group("Stats").Bar("Frame Upload", uploadMB, uploadCapacityMB, "MB");

	// SRVスロットの使っている数（Play / Stop を何回繰り返しても、ここが増え続けなければよい）
	float usedSrvSlots = static_cast<float>(DirectXCommon::GetUsedSRVSlotCount());
	float srvSlotCapacity = static_cast<float>(DirectXCommon::kSRVDescriptorHeap - 1); // 0番はImGui用
	Profiler::Category(ProfCategory::GPU).Group("Stats").Bar("SRV Slots", usedSrvSlots, srvSlotCapacity);
}
} // namespace


//=============================================================================
// 統計を書く
//=============================================================================
void FrameStats::Report() {
	ReportTimes();
	// 当たり判定
	CollisionProfiler::Report();
	CollisionProfiler::Reset();
	ReportMemory();
	ReportGPUStats();
}
#endif
