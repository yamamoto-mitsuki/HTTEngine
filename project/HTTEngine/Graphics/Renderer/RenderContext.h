#pragma once
#include <memory>
#include <cstdint>

#include <d3d12.h>
#include <wrl.h>

#include "HTTEngine/Graphics/GPU/FrameUploadBuffer.h"
#include "HTTEngine/Graphics/Renderer/DrawRequest.h"

// 前方宣言
class RenderWindow;


/// <summary>
/// 描画1回分のデータをGPU用のバッファに書き、DrawCallを発行するだけの実行層
/// <para>状態切替（RootSignature/PSO）はRenderQueueの仕事</para>
/// </summary>
class RenderContext {
public:
	// ===== Passkey Idiom =====
	class ConstructorKey {
	private:
		ConstructorKey() = default;
		friend class RenderContext;
	};
	explicit RenderContext(ConstructorKey) {} // 鍵を受け取るコンストラクタ

	// 1フレームに書けるGPU用データの合計（頂点・インデックス・定数・パーティクルの合計）
	static constexpr size_t kFrameUploadBytes = 64 * 1024 * 1024;

	RenderContext(const RenderContext&) = delete;
	RenderContext& operator=(const RenderContext&) = delete;
	// 初期化・解放
	static void Initialize();
	static void Release();

	/// <summary>
	/// 3Dメッシュを1つ描画する（動的=Primitive / 静的=Model の両対応）
	/// </summary>
	static void DrawMesh(const MeshRequest& req);

	/// <summary>
	/// デカールを1つ描画する（1辺 1 の箱を描き、PS が深度から塗る所を決める）
	/// </summary>
	/// <param name="depthIndex">不透明な物の深度の SRV の番号（gDecal に入れる）</param>
	static void DrawDecal(const DecalRequest& req, uint32_t depthIndex);

	/// <summary>
	/// 2Dスプライトを1つ描画する
	/// </summary>
	static void DrawSprite(const SpriteRequest& req);

	/// <summary>
	/// 2Dの文字を描画する（1回の描画で、全部の文字を出す）
	/// <para>名前を DrawText にしないのは、windows.h に同じ名前のマクロ（DrawTextW に置き換わる）があるため</para>
	/// </summary>
	static void DrawText2d(const TextRequest& req);

	/// <summary>
	/// 2Dの図形を描く（四角形の板一枚。形はシェーダーが計算する）
	/// </summary>
	static void DrawVectorShape(const VectorShapeRequest& req);

	/// <summary>
	/// 2Dの折れ線を描く（PolylineSystem が作ったメッシュの、indexStart から indexCount 個の番号）
	/// </summary>
	static void DrawPolyline(const PolylineRequest& req);

	/// <summary>
	/// パーティクルを描画する
	/// </summary>
	static void DrawParticles(const ParticleRequest& req);

	/// <summary>
	/// 3Dライン群を描画する（LINELIST。奇数個の頂点は最後を切り捨て）
	/// </summary>
	static void DrawLines(const LineRequest& req);

	/// <summary>
	/// GPUページフォルトのアドレスがどのバッファ内かログに出力する
	/// </summary>
	static void LogFaultResource(D3D12_GPU_VIRTUAL_ADDRESS faultVA);

	/// <summary>
	/// このフレームのGPU用データの書き込み位置を先頭に戻す。フレームの終わりに1回呼ぶ
	/// </summary>
	static void ResetFrame();

	/// <summary>
	/// このフレームのライトを書き込む。全描画で同じ場所を結ぶ
	/// </summary>
	static void SetFrameLights(const DirectionalLightData& directionalLight, const PointLightListData& pointLights, const SpotLightListData& spotLights);

	/// <summary>
	/// このフレームだけ使う配列（スキンの行列パレットなど）を、GPU用のバッファに書いて場所を返す
	/// <para>フレームの終わり（ResetFrame）まで使える。DrawMesh以外からも書けるように出している</para>
	/// </summary>
	template<class T> 
	static D3D12_GPU_VIRTUAL_ADDRESS PushFrameArray(const T* data, size_t count) { 
		return instance_->frameUploadBuffer_.PushArray(data, count).gpuAddress;
	}

	/// <summary>
	/// このフレームだけ使う定数を、GPU用のバッファに書いて場所を返す（256バイトの倍数にそろう）
	/// </summary>
	template<class T> 
	static D3D12_GPU_VIRTUAL_ADDRESS PushFrameConstant(const T& data) { return instance_->frameUploadBuffer_.PushConstant(data); }


	// 前のフレームでGPU用に書いたデータの量（バイト）。Profiler用
	static size_t GetLastFrameUploadBytes() { return instance_->frameUploadBuffer_.GetLastFrameUsedBytes(); }
	// 1フレームに書ける量（バイト）
	static size_t GetFrameUploadCapacity() { return instance_->frameUploadBuffer_.GetCapacity(); }


private:
	~RenderContext() = default;

	// シングルトン
	friend struct std::default_delete<RenderContext>;
	static std::unique_ptr<RenderContext> instance_;

	void InitInternal();

	// --- パーティクル用の共通Quad（最初に1回だけ書く） ---
	Microsoft::WRL::ComPtr<ID3D12Resource> particleQuadVB_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> particleQuadIB_ = nullptr;
	D3D12_VERTEX_BUFFER_VIEW particleQuadVBV_{};
	D3D12_INDEX_BUFFER_VIEW particleQuadIBV_{};

	// --- 描画ごとに増えるデータ（定数・頂点・インデックス・パーティクル）。全部ここに先頭から詰めて書く ---
	FrameUploadBuffer frameUploadBuffer_;

	// --- ライト（1フレームに1個。全描画で共有する） ---
	Microsoft::WRL::ComPtr<ID3D12Resource> frameDirectionalLightBuffer_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> framePointLightsBuffer_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> frameSpotLightsBuffer_ = nullptr;
	DirectionalLightData* frameDirectionalLightMappedPtr_ = nullptr;
	PointLightListData* framePointLightsMappedPtr_ = nullptr;
	SpotLightListData* frameSpotLightsMappedPtr_ = nullptr;
};