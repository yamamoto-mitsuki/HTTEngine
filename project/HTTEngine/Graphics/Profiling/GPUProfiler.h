#pragma once
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

#include <wrl.h>
#include <d3d12.h>

/// <summary>
/// GPUが各処理にかけた時間を測るプロファイラ
/// </summary>
class GPUProfiler {
public:
	// ===== Passkey Idiom =====
	class ConstructorKey {
	private:
		ConstructorKey() = default;
		friend class GPUProfiler;
	};
	explicit GPUProfiler(ConstructorKey) {} // 鍵を受け取るコンストラクタ

	// 1区間の計測結果
	struct Result {
		std::string name;
		double ms;
	};

	/// <summary>
	/// 初期化
	/// </summary>
	/// <param name = "maxScopes">1フレームで測れる区間の最大数</param>
	static void Initialize(UINT maxScopes = 128);
	// 解放
	static void Release();

	// フレームの最初に呼ぶ。使った区間数のカウンタを0に戻す。
	static void BeginFrame();

	/// <summary>
	/// 区間の開始
	/// </summary>
	static uint32_t Begin(ID3D12GraphicsCommandList* cmd, const char* name);
	
	// 区間の終了
	static void End(ID3D12GraphicsCommandList* cmd, uint32_t scopeIndex);
	
	// 記録したtickを ReadbackBuffer へコピーする
	// コマンドリストをCloseする前に呼ぶこと
	static void Resolve(ID3D12GraphicsCommandList* cmd);
	
	// GPU完了後（WaitForGPU）に呼ぶ
	// msに変換してresults_にいれる
	static void Readback();

	static const std::vector<Result>& GetResults() { return instance_->results_; };

private:
	// シングルトン
	friend struct std::default_delete<GPUProfiler>;
	~GPUProfiler() = default;
	GPUProfiler(const GPUProfiler&) = delete;
	GPUProfiler& operator=(const GPUProfiler&) = delete;
	static std::unique_ptr<GPUProfiler> instance_;

	Microsoft::WRL::ComPtr<ID3D12QueryHeap> queryHeap_;     // GPUのtickを記録する
	Microsoft::WRL::ComPtr<ID3D12Resource> readbackBuffer_; // クエリ結果（tick群）を読むバッファ
	UINT64 frequency_ = 0; // GPUのtickが1秒に何回進むか
	UINT maxScopes_ = 0;   // 測れる区間の最大数
	UINT scopeCount_ = 0;  // このフレームで使った区間数
	std::vector<std::string> names_; // 各区間の名前
	std::vector<Result> results_;    // 換算後の結果一覧

	UINT64* mappedTicks_ = nullptr; // ReadbackBufferを永続マップしたポインタ
};