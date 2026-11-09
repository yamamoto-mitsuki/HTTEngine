#pragma once
#include <array>
#include <memory>
#include <cstdint>
#include <d3d12.h>
#include <wrl.h>
#include "HTTEngine/Graphics/RenderTarget/RenderTexture.h"
#include "HTTEngine/Graphics/Pipeline/RootSignatureManager.h"

/// <summary>
/// ブルームのぼかし方
/// </summary>
enum class BloomMode {
	Dual,   // デュアルフィルタ式：テントで縮めながらぼかし、小さい段から戻しながら足す（軽い。芯の周りに柔らかく広がる）
	Kawase, // 川瀬式（MGF）：1/4〜1/32 の段ごとに横・縦のガウスでぼかし、全部の段を重みを付けて足す（芯の強い光と、遠くまで届く薄いにじみ）
};


/// <summary>
/// ブルーム。明るい部分を抜き出して段階的にぼかし、元の絵に加算する
/// </summary>
class BloomPass {
public:
	static constexpr uint32_t kLevelCount = 4;       // 縮小の段数（Dual：1/2〜1/16、Kawase：1/4〜1/32）
	static constexpr uint32_t kMaxPassPerFrame = 64; // 1フレームに積むパス数の上限（Kawase は1ビュー14パス。Scene・Game の2ビューで28）

	// --- 調整項目 ---
	BloomMode mode = BloomMode::Dual; // ぼかし方
	float kThreshold = 0.75f;         // これより明るい所だけ光る
	float kKnee = 0.25f;              // しきい値付近の柔らかさ
	float kIntensity = 0.9f;          // 合成の強さ
	std::array<float, kLevelCount> kKawaseWeights = {1.0f, 1.0f, 1.0f, 1.0f}; // 川瀬式の段ごとの重み（1/4・1/8・1/16・1/32。小さい段ほど広く薄く届く）

	float addIntensity = 0.0f;
	float addThreshold = 0.0f; // 負の値でしきい値が下がる＝もっと光る

	void Initialize(uint32_t width, uint32_t height);

	/// <summary>
	/// シーンの絵から、ぼかした光の層を作る
	/// </summary>
	void Record(RenderTexture& scene);

	/// <summary>
	/// 今バインドされている描画先へ、シーンと光を合成して描く
	/// </summary>
	void Composite(RenderTexture& scene, float width, float height);

	/// <summary>
	/// フレーム末に定数バッファの参照位置を戻す
	/// </summary>
	void ResetSlot() { slot_ = 0; }

private:
	// シェーダーの BloomParam と同じ並びにすること
	struct Param {
		float texelSize[2] = {0.0f, 0.0f};
		float threshold = 0.0f;
		float knee = 1.0f;
		float intensity = 1.0f;
		uint32_t srcIndex = 0;
		uint32_t addIndex = 0;
		float padding = 0.0f;
		float direction[2] = {0.0f, 0.0f}; // ガウスでぼかす向き（横 (1,0)・縦 (0,1)）
		float padding2[2] = {0.0f, 0.0f};
		float levelWeights[kLevelCount] = {};    // 川瀬式の合成：段ごとの重み
		uint32_t levelIndices[kLevelCount] = {}; // 川瀬式の合成：段の SRV スロット
	};
	static constexpr uint32_t kSlotSize = 256;

	void CreateRootSignature();
	Microsoft::WRL::ComPtr<ID3D12PipelineState> CreatePSO(const char* psName);
	void BindAndDraw(ID3D12PipelineState* pso, const Param& param);
	void SetTarget(RenderTexture& target); // 描画先を張り、ビューポートをその大きさに合わせる
	void RecordDual();                     // Dual：縮めながらぼかし、戻しながら足す（down_[0] に明るい所が入っている状態で呼ぶ）
	void RecordKawase();                   // Kawase：段ごとに縮めて横・縦にぼかす（down_[0] に明るい所が入っている状態で呼ぶ）

	RootSignatureInfo rsInfo_{};
	Microsoft::WRL::ComPtr<ID3D12PipelineState> brightPSO_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> downPSO_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> upPSO_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> compositePSO_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> gaussianPSO_;        // 川瀬式：1方向のガウス
	Microsoft::WRL::ComPtr<ID3D12PipelineState> kawaseCompositePSO_; // 川瀬式：段を全部足して合成
	std::array<std::unique_ptr<RenderTexture>, kLevelCount> down_;
	std::array<std::unique_ptr<RenderTexture>, kLevelCount> up_;
	std::array<std::unique_ptr<RenderTexture>, kLevelCount> kawaseLevels_; // 川瀬式の段（1/4〜1/32。縮めた絵 → 縦にぼかした結果）
	std::array<std::unique_ptr<RenderTexture>, kLevelCount> kawaseTemp_;   // 川瀬式の段ごとの作業用（横にぼかした結果）
	Microsoft::WRL::ComPtr<ID3D12Resource> cb_;
	uint8_t* cbMapped_ = nullptr;
	uint32_t slot_ = 0;
};