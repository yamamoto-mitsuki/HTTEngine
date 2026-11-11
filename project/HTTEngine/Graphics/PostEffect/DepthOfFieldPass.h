#pragma once
#include <cstdint>
#include <d3d12.h>
#include <memory>
#include <wrl.h>
#include "HTTEngine/Graphics/Compute/ComputeTexture.h"
#include "HTTEngine/Graphics/Pipeline/RootSignatureManager.h"
#include "HTTEngine/Graphics/PostEffect/PostOverride.h"
#include "HTTEngine/Graphics/RenderTarget/RenderTexture.h"
#include "HTTEngine/Math/Vector2.h"
#include "HTTEngine/Math/Vector3.h"

// 前方宣言
class Camera;

// 被写界深度の値（DepthOfFieldSystem が、シーンの Depth Of Field Component から毎フレーム作る）
struct DepthOfFieldSettings {
	bool enabled = false;        // false ならぼかさない（Depth Of Field Component が無い・切ってある）
	bool hasFocusPoint = false;  // true：focusPoint の奥行きにピントを合わせる（ビューごとに、そのカメラからの奥行きを計算する）
	Vector3 focusPoint = {};     // ピントを合わせる点（ワールド座標。Camera Follow・Camera Orbit の Target の位置）
	float focusDistance = 10.0f; // hasFocusPoint が false のときの、ピントの合う奥行き（m）
	float focusRange = 3.0f;     // ピントの前後この奥行き（m）までは、ぼけない
	float farTransition = 40.0f; // その奥から、一番ぼけるまでの奥行き（m）
	float nearTransition = 4.0f; // その手前から、一番ぼけるまでの奥行き（m。0 なら手前はぼかさない）
	float maxBlur = 8.0f;        // 一番ぼけた所の、ぼけの半径（ピクセル。高さ 720 の画面で）
	PostOverride focusOverride;  // カメラごとのピントの奥行きの上書き（focusPoint はビューごとに奥行きが変わるので、測った後に混ぜる）
};

/// <summary>
/// 被写界深度（ピントの合った所だけはっきり見え、手前と奥がぼける）
/// <para>CS 1：半分の大きさに縮めて、ピクセルごとのぼけの大きさ（深度から計算）を A に入れる</para>
/// <para>CS 2：ぼけの大きさだけ、まわりの色を混ぜてぼかす</para>
/// <para>PS  ：元の絵と、ぼかした絵を、ぼけの大きさで混ぜる</para>
/// </summary>
class DepthOfFieldPass {
public:
	static constexpr uint32_t kMaxPassPerFrame = 8; // 1フレームに通るビューの上限（Game と Scene で2回通る）
	static constexpr float kMaxBlurLimit = 32.0f;   // ぼけの半径の上限（ピクセル。大きいほど重くなる）

	void Initialize(uint32_t width, uint32_t height);

	/// <summary>
	/// scene の色と深度から、ぼかした絵を作る（CS 2本）
	/// <para>scene は深度を読める RenderTexture（isDepthReadable）。この後に Composite を呼ぶ</para>
	/// </summary>
	void Record(RenderTexture& scene, const Camera& camera, const DepthOfFieldSettings& settings);

	/// <summary>
	/// 今バインドされている描画先へ、元の絵とぼかした絵を混ぜて描く（Record の後）
	/// </summary>
	void Composite(float width, float height);

	/// <summary>
	/// フレーム末に定数バッファの参照位置を戻す
	/// </summary>
	void ResetSlot() { slot_ = 0; }

private:
	// シェーダーの DofParam と同じ並びにすること
	struct Param {
		uint32_t fullWidth = 0; // 元の絵の大きさ
		uint32_t fullHeight = 0;
		uint32_t halfWidth = 0; // 半分の大きさ（CS のはみ出したスレッドを止める）
		uint32_t halfHeight = 0;
		Vector2 halfTexelSize = {};  // 1 / 半分の大きさ（UV の計算用）
		float projectionA = 0.0f;    // 投影行列の m[2][2]（深度 → 奥行き(m) に戻すのに使う）
		float projectionB = 0.0f;    // 投影行列の m[3][2]
		float focusDistance = 0.0f;  // ピントの合う奥行き（m）
		float focusRange = 0.0f;     // 前後この奥行きまではぼけない（m）
		float farTransition = 1.0f;  // 奥：一番ぼけるまでの奥行き（m）
		float nearTransition = 0.0f; // 手前：一番ぼけるまでの奥行き（m。0 なら手前はぼかさない）
		float maxRadius = 0.0f;      // 一番ぼけた所の半径（半分の大きさのピクセル）
		float radiusStep = 0.0f;     // ぼかすときに読む点の間隔（半分の大きさのピクセル）
		uint32_t colorIndex = 0;     // 元の絵の色（SRV の番号）
		uint32_t depthIndex = 0;     // 元の絵の深度（SRV の番号）
		uint32_t halfIndex = 0;      // 縮めた色＋ぼけの大きさ（SRV の番号）
		uint32_t blurIndex = 0;      // ぼかした色（SRV の番号）
		float padding[2] = {};       // 16 バイトの区切りにそろえる
	};
	static_assert(sizeof(Param) == 80, "シェーダーの DofParam（16 バイト × 5）と大きさをそろえる");
	static constexpr uint32_t kSlotSize = 256;

	void CreateRootSignature();
	void CreatePSO();
	// 定数（ビューのカメラで、ピントの奥行きと深度の戻し方が変わる）
	Param MakeParam(RenderTexture& scene, const Camera& camera, const DepthOfFieldSettings& settings) const;
	// CS を1本動かして、output に書く
	void Dispatch(const char* csName, ComputeTexture& output, D3D12_GPU_DESCRIPTOR_HANDLE heapStart) const;

	RootSignatureInfo rsInfo_{}; // 合成の PS 用
	Microsoft::WRL::ComPtr<ID3D12PipelineState> compositePSO_;
	std::unique_ptr<ComputeTexture> half_; // 半分の大きさの色（RGB）＋ぼけの大きさ（A。マイナスが手前）
	std::unique_ptr<ComputeTexture> blur_; // ぼかした色（RGB）＋手前のぼけのはみ出し（A）
	Microsoft::WRL::ComPtr<ID3D12Resource> cb_;
	uint8_t* cbMapped_ = nullptr;
	uint32_t slot_ = 0;
	D3D12_GPU_VIRTUAL_ADDRESS currentCB_ = 0; // Record で書いた定数バッファ（Composite でも同じ物を使う）
	RenderTexture* scene_ = nullptr;          // Record で読んだ絵（Composite の後に、読む状態から戻す）
};