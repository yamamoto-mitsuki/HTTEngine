#pragma once
#include <cstdint>
#include <d3d12.h>
#include <wrl.h>
#include "MyEngine/Graphics/Renderer/GameScreen.h"
#include "MyEngine/Graphics/RenderTarget/RenderTexture.h"
#include "MyEngine/Graphics/Pipeline/RootSignatureManager.h"

/// <summary>
/// トーンマップ（HDR の明るさを、画面に出せる 0〜1 に収める曲線）。シェーダーは Post/ToneMap.hlsli
/// </summary>
enum class ToneMapMode {
	None,     // 1 で切るだけ（今までの見た目。1 を超えた所は白く飛ぶ）
	Reinhard, // x / (1 + x)。どこまで明るくても白く飛ばない。全体が暗く・淡くなる
	ACES,     // 映画の色の標準の近似。明るい所は白へ柔らかく飽和し、暗い所は締まる。色が少し濃くなる
	GT,       // グランツーリスモ（内村式）。真ん中の明るさはそのまま、明るい所だけ柔らかく白へ（色がずれにくい。アニメ調に向く）
};


/// <summary>
/// 最後のパス（HDR の絵 → 画面）。レンズの効果（歪み・色収差・放射ブラー）→ 露出・彩度・トーンマップ → ビネットの順に掛ける
/// <para>レンズを切っていても、露出とトーンマップだけ掛けて通す（3D の絵は HDR なので、どの道もここを通って画面へ出る）</para>
/// </summary>
class LensPass {
public:
	// 1フレームに描くビュー数の上限（Game と Scene で2回通る）
	static constexpr uint32_t kMaxPassPerFrame = 8;

	// --- 露出・トーンマップ（GlobalVariables から入る。レンズを切っていても、いつも掛ける）---
	ToneMapMode toneMap = ToneMapMode::None; // 曲線（None なら今までの見た目）
	float kExposure = 1.0f;                  // 露出：トーンマップの前に掛ける倍率（2 で1段明るい・0.5 で1段暗い。ブルームの光り方は変わらない）

	// --- 色の調整（GlobalVariables から入る。レンズを切っていても掛ける。ポストエフェクトを全部切ったビューだけ外す）---
	float kSaturation = 1.0f; // 彩度：1 でそのまま・0 で白黒（同じ明るさの灰色）・1 より大きいと色が濃くなる

	// --- 画面へ書くとき（GlobalVariables から入る。どのビューでも掛ける）---
	bool dither = true; // 画面の 256 段に丸める前に、1 段より小さい粒を足して、なめらかな明るさの変化が帯（等高線）に見えないようにする

	// --- 調整項目（GlobalVariables から入る基準値）---
	float kDistortion = 0.0f; // 画面端の歪み。0.2で気づく程度
	float kAberration = 0.0f; // 色収差。0.02で十分
	float kRadialBlur = 0.0f; // 中心へ向かって流れるブラー。0.10で強め
	float kVignette = 0.35f;  // 端の暗さ。常時少し掛けておくと締まる

	// --- 演出用の上乗せ。毎フレーム書かないと0に戻る ---
	// 掛けっぱなしになる事故を防ぐため、Render() の最後に自分で消す
	float addDistortion = 0.0f;
	float addAberration = 0.0f;
	float addRadialBlur = 0.0f;
	float addVignette = 0.0f;

	// --- 風の線（画面全体に、風の向きへ流れる白い線）。WindZoneSystem が毎フレーム書く（止めると 0 を書く）ので、上乗せと違って自分では消さない ---
	float windLines = 0.0f;                   // 濃さ（0〜1。0 なら線を描かない）
	Vector2 windLineDirection = {1.0f, 0.0f}; // 流れる向き（画面の右・上が＋。長さ1）
	float windLineTime = 0.0f;                // 流れる時間（秒）

	void Initialize();

	/// <summary>
	/// source を読んで、今バインドされている描画先へ描く
	/// </summary>
	/// <param name="useLens">false ならレンズの効果（歪み・色収差・放射ブラー・ビネット・風の線）を掛けない</param>
	/// <param name="useColorAdjust">false なら色の調整（彩度）を掛けない。露出・トーンマップはどちらでも掛ける</param>
	void Render(RenderTexture& source, const ViewRect& rect, bool useLens, bool useColorAdjust);

	/// <summary>
	/// フレーム末にスロットを戻す
	/// </summary>
	void ResetSlot() { slot_ = 0; }

private:
	// シェーダーの LensParam と同じ並びにすること
	struct Param {
		float distortion; // 画面端の歪み。正で外へ膨らむ
		float aberration; // 色収差。端ほどRGBがずれる
		float radialBlur; // 中心へ向かって流れるブラー
		float vignette;   // 端の暗さ
		uint32_t srcIndex = 0;
		float windLines = 0.0f;                   // 風の線の濃さ
		float windLineTime = 0.0f;                // 風の線の流れる時間（秒）
		float aspect = 1.0f;                      // 画面の横 ÷ 縦（線が斜めでも同じ太さに見えるように）
		Vector2 windLineDirection = {1.0f, 0.0f}; // 風の線の流れる向き
		float exposure = 1.0f;                    // 露出（トーンマップの前に掛ける倍率）
		uint32_t toneMap = 0;                     // トーンマップの種類（ToneMapMode の番号）
		float saturation = 1.0f;                  // 彩度（1 でそのまま・0 で白黒）
		uint32_t dither = 1;                      // 1 なら画面へ書く前に粒を足す
	};
	static_assert(sizeof(Param) == 56, "シェーダーの LensParam（16 バイト × 3 ＋ 8 バイト）と大きさをそろえる");
	static constexpr uint32_t kSlotSize = 256;

	void CreateRootSignature();
	void CreatePSO();

	RootSignatureInfo rsInfo_{};
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pso_;
	Microsoft::WRL::ComPtr<ID3D12Resource> cb_;
	uint8_t* cbMapped_ = nullptr;
	uint32_t slot_ = 0;
};