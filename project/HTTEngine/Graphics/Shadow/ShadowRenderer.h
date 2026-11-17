#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include <d3d12.h>

#include "HTTEngine/Graphics/Compute/ComputeTexture.h"
#include "HTTEngine/Graphics/Shadow/ShadowMap.h"
#include "HTTEngine/Graphics/Shadow/ShadowSettings.h"
#include "HTTEngine/Math/MathIncludes.h"

// 前方宣言
class Camera;
class RenderTexture;


/// <summary>
/// 平行光源の影を作るクラス（SceneRendererが1つ持ち、ビューごとに呼ぶ）
/// <para>1ビューの順番: RenderShadowMap →（深度プリパス）→ BuildShadowMask → PushOpaqueReceiver・PushTransparentReceiverを結んで色を塗る</para>
/// </summary>
class ShadowRenderer {
public:
	/// <summary>
	/// 影の量の画面を作る
	/// </summary>
	/// <param name="width">3Dの絵の幅（SceneRendererのpostRT_と同じ）</param>
	/// <param name="height">3Dの絵の高さ</param>
	ShadowRenderer(uint32_t width, uint32_t height);

	// コピー禁止（GPU の物を持つ）
	ShadowRenderer(const ShadowRenderer&) = delete;
	ShadowRenderer& operator=(const ShadowRenderer&) = delete;

	/// <summary>
	/// このビューの影の深度を描く（ビューのカメラから見える位置に合わせて、ライトから見る箱を決める）
	/// <para>ビューを描き始めるときに毎回呼ぶ</para>
	/// </summary>
	void RenderShadowMap(const Camera* camera, const DirectionalShadowSettings& settings, const std::wstring& windowTitle);

	/// <summary>
	/// 影の量の画面を作る。深度プリパスの後・色を塗る前に呼ぶ
	/// <para>3Dの絵の深度から位置を戻し、ライトから見た深度と比べる。RenderShadowMapで描かなかったビューは何もしない</para>
	/// </summary>
	/// <param name="scene">3Dの絵（深度プリパスを描き終えた物）</param>
	/// <param name="cameraCB">このビューのカメラの定数（深度から位置へ戻す）</param>
	void BuildShadowMask(RenderTexture& scene, D3D12_GPU_VIRTUAL_ADDRESS cameraCB);

	/// <summary>
	/// 不透明な物gShadowを、このフレームの定数バッファに書いて場所を返す（影の量の画面）
	/// </summary>
	D3D12_GPU_VIRTUAL_ADDRESS PushOpaqueReceiver() const;
 
	/// <summary>
	/// 半透明な物のgShadowを書いて場所を返す（いつも影なし）
	/// <para>影の量の画面は、一番手前の不透明な物の影なので、その前にある半透明な物には使えない</para>
	/// </summary>
	D3D12_GPU_VIRTUAL_ADDRESS PushTransparentReceiver() const;

private:
	// ===== ライトから見る箱 =====
	struct LightView {
		Matrix4x4 viewProj = MakeIdentity4x4(); // ワールド → ライトから見た画面（x・yが-1~1、zが0~1）
		Vector3 position;                       // 箱の手前の面の真ん中（ライトの位置の代わり）
		Vector3 right;                          // ライトから見た右
		Vector3 up;                             // ライトから見た上
		float depthRange = 1.0f;                // 箱の奥つ城。Biasを深度の0~1に直すのに使う
	};

	/// <summary>
	/// カメラから影を出すまでを包む球を求め、その球をライトの向きから見る箱を作る
	/// </summary>
	/// <param name="mapPixels">影の深度の絵の1辺（箱の位置を1ピクセルずつ動かして、カメラが動いても影の縁がちらつかないようにする）</param>
	static LightView MakeLightView(const Camera& camera, const DirectionalShadowSettings& settinga, uint32_t mapPixels);

	std::unique_ptr<ShadowMap> shadowMap_; // 影の深度の絵
	std::unique_ptr<ComputeTexture> mask_; // 影の量の画面
	DirectionalShadowSettings settings_;   // このビューの影の設定
	LightView lightView_;                  // このビューのライトから見る箱
	bool hasShadowMap_ = false;            // このビューで影の深度を描いたか
	bool hasMask_ = false;                 // このビューで影の量の画面を作ったか
};