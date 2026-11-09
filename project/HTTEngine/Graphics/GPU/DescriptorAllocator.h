#pragma once
#include <string>
#include <vector>
#include <cstdint>

#include <d3d12.h>
#include <wrl.h>


/// <summary>
/// シェーダーから見えるディスクリプタヒープ（CBV・SRV・UAV 用）を1つ持ち、その番号（スロット）を配る・返してもらう
/// <para>テクスチャ・RenderTexture など、SRVを置きたい物は、ここから番号をもらう</para>
/// <para>返してもらった番号は、そのフレームのGPUの処理が終わってから（RecycleFreed の後に）配り直す</para>
/// </summary>
class DescriptorAllocator {
public:
	/// <summary>
	/// ヒープを作る
	/// </summary>
	/// <param name="capacity">ヒープの大きさ（番号の数）</param>
	/// <param name="reservedCount">先頭の何個を配らずに取っておくか（0番は ImGui 用なので 1）</param>
	/// <param name="name">PIX・エラーのときに出る名前</param>
	DescriptorAllocator(uint32_t capacity, uint32_t reservedCount, const std::wstring& name);

	// コピー禁止
	DescriptorAllocator(const DescriptorAllocator&) = delete;
	DescriptorAllocator& operator=(const DescriptorAllocator&) = delete;

	/// <summary>
	/// 番号を1つ配る。返してもらった番号があれば、そちらを先に使う
	/// </summary>
	uint32_t Allocate();

	/// <summary>
	/// 番号を返す。すぐには配り直さず、RecycleFreed まで取っておく
	/// <para>取っておいた番号（0番など、「まだもらっていない」の意味で使われる）を渡したときは何もしない</para>
	/// </summary>
	void Free(uint32_t slot);

	/// <summary>
	/// このフレームに返してもらった番号を、次から配れるようにする
	/// <para>フレームの終わりに WaitForGPU した後に1回呼ぶ（GPUがもう読まないと確かなときだけ）</para>
	/// </summary>
	void RecycleFreed();

	// 今配っている番号の数（取っておいた番号は数えない。Profilerの表示用）
	uint32_t GetUsedCount() const;
	// 配れる番号の最大数（取っておいた番号は数えない）
	uint32_t GetCapacity() const { return capacity_ - reservedCount_; }
	// ヒープ（SetDescriptorHeaps・ハンドルの計算に使う）
	ID3D12DescriptorHeap* GetHeap() const { return heap_.Get(); }

private:
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap_; // シェーダーから見えるヒープ
	uint32_t capacity_ = 0;                             // ヒープの大きさ
	uint32_t reservedCount_ = 0;                        // 先頭の配らない番号の数
	uint32_t next_ = 0;                                 // まだ1回も配っていない番号の先頭
	std::vector<uint32_t> free_;                        // 返してもらって、また配れる番号（Allocate はここから先に配る）
	std::vector<uint32_t> pendingFree_;                 // このフレームに返してもらった番号（GPUを待った後で free_ へ移す）
};