#pragma once
#include <array>
#include <string>
#include <memory>
#include <vector>
#include <variant>

#include <externals/magic_enum/magic_enum.hpp>

#include "HTTEngine/Graphics/Renderer/DrawRequest.h"

// 前方宣言
class RenderWindow;
struct PSOKey;


/// <summary>
/// 描画順の並び替え、リクエストの発行
/// </summary>
class RenderQueue {
public:
	// ===== Passkey Idiom =====
	class ConstructorKey {
	private:
		ConstructorKey() = default;
		friend class RenderQueue;
	};
	explicit RenderQueue(ConstructorKey) {} // 鍵を受け取るコンストラクタ

	RenderQueue(const RenderQueue&) = delete;
	RenderQueue& operator=(const RenderQueue&) = delete;

	// 2Dのリクエスト1つ（スプライト・文字・図形・折れ線のどれか）
	using Request2d = std::variant<SpriteRequest, TextRequest, VectorShapeRequest, PolylineRequest>;

	// 初期化・解放
	static void Initialize();
	static void Release();

	// --- リクエストに積む ---
	static void Request(MeshRequest&& req);
	static void Request(LineRequest&& req);
	static void Request(SpriteRequest&& req);
	static void Request(TextRequest&& req);
	static void Request(VectorShapeRequest&& req);
	static void Request(PolylineRequest&& req);
	static void Request(ParticleRequest&& req);
	static void Request(DecalRequest&& req);

	// ---  発行 ---
	static void Flush2d(const std::wstring& windowTitle);
	static void FlushTransparentMesh(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB, D3D12_GPU_VIRTUAL_ADDRESS shadowCB);
	static void FlushOpaqueMesh(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB, D3D12_GPU_VIRTUAL_ADDRESS shadowCB);
	// 深度プリパス: 不透明な物（深度を書く物）の深度だけを描く。色を塗る前に呼ぶ（描画先は深度だけを結んでおく）
	static void FlushDepthPrepass(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB);
	// 影の深度: 影を落とす不透明な物の深度だけを、ライトから見て描く（描画先はShadowMap）
	static void FlushShadowCaster(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS lightCameraCB);
	// isBeforeDepthOfField：true なら被写界深度の前に描く粒（Use Depth Of Field）だけ、false なら後に描く粒だけ
	static void FlushParticle(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB, bool isBeforeDepthOfField);
	static void FlushLine(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB);
	// ギズモの線（LineRequest::isGizmo）。ポストエフェクトの後の絵に描く。depthIndex：3D の絵の深度（gTextures[] の番号。物の後ろかを比べる）
	static void FlushGizmoLine(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB, uint32_t depthIndex);
	// デカール（不透明な物の後。深度は読むだけの窓口で結び、depthIndex の SRV で読む）
	static void FlushDecal(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB, uint32_t depthIndex);
	// デカールを積んだか（無ければ、深度を読む状態に切り替えなくてよい）
	static bool HasDecal();
	
	// フレーム末にクリア
	static void Clear();
	// この後に描くビューで、その層（DrawLayer）の物を描くか（エディタのグリッドを Game ビューだけ消す、など）
	// SceneRenderer が、ビューを描く前に毎回決める
	static void SetLayerVisible(DrawLayer layer, bool isVisible);
	// この後に描くビューで、Hierarchy の目で隠した物（DrawTarget::isSceneHidden）を描くか（Scene ビューだけ false）
	static void SetSceneHiddenVisible(bool isVisible);

private:
	~RenderQueue() = default;
	// MeshのRootSignature, PSO切り替え
	static void FlushMeshList(const std::vector<MeshRequest>& meshes, const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB, 
		D3D12_GPU_VIRTUAL_ADDRESS shadowCB, MeshPass pass, const char* name);
	// そのパスで描く物か（深度だけのパスは、深度を書く物だけ。影の深度は、さらに影を落とす物だけ）
	static bool IsDrawnInPass(const MeshRequest& req, MeshPass pass);
	// そのパスのPSOKey（深度だけのパスは、ブレンド・ステンシルを使わない形に揃える）
	static PSOKey MakeMeshPSOKey(const MeshRequest& req, MeshPass pass);
	// 不透明な物を、状態の切り替えが少ない順（sortKey）に並べる
	static void SortOpaqueMeshes();

	// 今描いているビュー（windowTitle のウィンドウ・SetLayerVisible などで決めた見せ方）で描く物か。Flush の全部が使う
	static bool IsDrawnInView(const DrawTarget& target, const std::wstring& windowTitle);

	// シングルトン
	friend struct std::default_delete<RenderQueue>;
	static std::unique_ptr<RenderQueue> instance_;

	std::vector<MeshRequest> transparentMeshRequests_;
	std::vector<MeshRequest> opaqueMeshRequests_;
	bool isOpaqueSorted_ = false; // opaqueMeshRequests_ を並べ終えたか
	std::vector<Request2d> requests2d_;
	std::vector<ParticleRequest> particleRequests_;
	std::vector<LineRequest> lineRequests_;
	std::vector<LineRequest> gizmoLineRequests_; // ギズモの線（ポストエフェクトの後に描く）
	std::vector<DecalRequest> decalRequests_;
	std::array<bool, magic_enum::enum_count<DrawLayer>()> isLayerVisible_{}; // 層ごとに、今のビューで描くか（番号は DrawLayer の順。magic_enum::enum_count で層の数を数える）
	bool isSceneHiddenVisible_ = true; // 目で隠した物を、今のビューで描くか（Scene ビューだけ false）
};