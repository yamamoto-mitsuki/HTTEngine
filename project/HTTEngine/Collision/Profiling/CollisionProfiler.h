#pragma once
#include <memory>
#include <string>
#include <cstdint>
#include <unordered_map>

// 当たり判定のプロファイルを管理
class CollisionProfiler {
public:
	// ===== Passkey Idiom =====
	class ConstructorKey {
	private:
		ConstructorKey() = default;
		friend class CollisionProfiler;
	};
	explicit CollisionProfiler(ConstructorKey) {} // 鍵を受け取るコンストラクタ

	struct FunctionStats {
		uint64_t callCount = 0;
		double totalMs = 0.0f;
		double AvgMs() const { return callCount > 0 ? totalMs / callCount : 0.0f; }
	};

	/// <summary>
	/// インスタンス生成。一度だけ呼び出す
	/// </summary>
	static void Initialize();

	// 解放
	static void Release();

	/// <summary>
	/// <param name = "funcName">カウントを増やす対象の関数名</param>
	/// <param name = "elapsedMs"></param>
	/// </summary>
	static void Record(const char* funcName, double elapsedMs);

	static void Reset();

#ifdef USE_IMGUI
	static void Report();
#endif

	static uint64_t GetTotalCalls();
	static const std::unordered_map<std::string, FunctionStats>& GetAllStats();

private:
	// シングルトン
	friend struct std::default_delete<CollisionProfiler>;
	~CollisionProfiler() = default;
	CollisionProfiler(const CollisionProfiler&) = delete;
	CollisionProfiler& operator=(const CollisionProfiler&) = delete;
	static std::unique_ptr<CollisionProfiler> instance_;
	std::unordered_map<std::string, FunctionStats> stats_;
	uint64_t totalCalls_ = 0; // 鍵を受け取るコンストラクタは 0 で埋めてくれないので、初期値を書く
};