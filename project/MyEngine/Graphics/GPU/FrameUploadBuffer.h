#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>

#include <d3d12.h>
#include <wrl.h>


/// <summary>
/// 1フレームだけ使うGPU用のデータ（定数・頂点・インデックスなど）を、1つのUploadバッファに先頭から詰めて書く
/// <para>書き込み位置は offset_ の1つだけ。型ごとにカウンタを持たないので、場所がぶつからない</para>
/// <para>フレームの終わりに Reset で先頭に戻す（GPUの処理が終わるのを待ってから次のフレームを書く前提）</para>
/// </summary>
class FrameUploadBuffer {
public:
	FrameUploadBuffer() = default;
	// コピー禁止。コピーすると offset_ が別々に進み、同じ場所に書いてしまう
	FrameUploadBuffer(const FrameUploadBuffer&) = delete;
	FrameUploadBuffer& operator=(const FrameUploadBuffer&) = delete;

	// 確保した場所
	struct Allocation {
		uint8_t* cpuAddress = nullptr;            // CPUから書き込む先
		D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0; // GPUに渡す場所（CBV / VBV / IBV / SRVに使う）
		size_t size = 0;                          // 確保したバイト数
	};

	/// <summary>
	/// バッファを作ってMapしたままにする
	/// </summary>
	void Initialize(size_t capacity, const std::string& name);

	/// <summary>
	/// バッファを解放する
	/// </summary>
	void Release();

	/// <summary>
	/// sizeバイトを確保する。開始位置をalignmentの倍数にそろえる（alignmentは2のべき乗）
	/// </summary>
	Allocation Allocate(size_t size, size_t alignment);

	/// <summary>
	/// 定数バッファ1個分を書き込み、GPUに渡す場所を返す
	/// <para>開始位置も大きさも256バイトの倍数にする（HLSL側が少し大きくても次のデータを読まない）</para>
	/// </summary>
	template<class T> 
	D3D12_GPU_VIRTUAL_ADDRESS PushConstant(const T& data) {
		Allocation allocation = Allocate(AlignUp(sizeof(T), kConstantBufferAlignment), kConstantBufferAlignment);
		std::memcpy(allocation.cpuAddress, &data, sizeof(T));
		return allocation.gpuAddress;
	}

	/// <summary>
	/// 定数バッファ1個分を、バイトの並びのまま書き込み、GPUに渡す場所を返す（中身の型が実行中に決まる物。マテリアルの値など）
	/// <para>開始位置も大きさも256バイトの倍数にする（PushConstant と同じ）</para>
	/// </summary>
	D3D12_GPU_VIRTUAL_ADDRESS PushConstantBytes(std::span<const std::byte> bytes) {
		Allocation allocation = Allocate(AlignUp((std::max)(bytes.size(), size_t{1}), kConstantBufferAlignment), kConstantBufferAlignment);
		if (!bytes.empty()) {
			std::memcpy(allocation.cpuAddress, bytes.data(), bytes.size());
		}
		return allocation.gpuAddress;
	}

	/// <summary>
	/// 配列（頂点・インデックス・パーティクルなど）を書き込み、確保した場所を返す
	/// </summary>
	template<class T> 
	Allocation PushArray(const T* data, size_t count) {
		Allocation allocation = Allocate(sizeof(T) * count, alignof(T));
		if (count > 0) {
			std::memcpy(allocation.cpuAddress, data, sizeof(T) * count);
		}
		return allocation;
	}

	/// <summary>
	/// 書き込み位置を先頭に戻す。フレームの終わりに1回呼ぶ
	/// </summary>
	void Reset();

	// 今のフレームで使っているバイト数
	size_t GetUsedBytes() const { return offset_; }
	// 前のフレームで使ったバイト数（Resetの直前の値。Profiler用）
	size_t GetLastFrameUsedBytes() const { return lastFrameUsedBytes_; }
	// 容量
	size_t GetCapacity() const { return capacity_; }
	// GPUページフォルトの調査用
	ID3D12Resource* GetResource() const { return buffer_.Get(); }


private:
	static constexpr size_t kConstantBufferAlignment = 256;

	// valueを、alignmentの倍数に切り上げる
	static size_t AlignUp(size_t value, size_t alignment) { return (value + alignment - 1) & ~(alignment - 1); }

	Microsoft::WRL::ComPtr<ID3D12Resource> buffer_ = nullptr;
	uint8_t* mappedPtr_ = nullptr;          // 永続Mapしている先頭
	D3D12_GPU_VIRTUAL_ADDRESS gpuBase_ = 0; // GPUから見た先頭
	size_t capacity_ = 0;                   // 容量（バイト）
	size_t offset_ = 0;                     // 次に書き込む位置（バイト）。書き込み位置はこれ1つだけ
	size_t lastFrameUsedBytes_ = 0;         // 前のフレームで使ったバイト数
};