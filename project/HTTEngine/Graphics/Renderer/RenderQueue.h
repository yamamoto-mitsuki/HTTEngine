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

	// ---  発行（2Dはスプライト・文字・図形・折れ線を、drawKey の順に混ぜて描く。マスクの子は、マスクの始まりと終わりの間）---
	static void Flush2d(const std::wstring& windowTitle);
	static void FlushTransparentMesh(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB);
	static void FlushOpaqueMesh(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB);
	// isBeforeDepthOfField：true なら被写界深度の前に描く粒（Use Depth Of Field）だけ、false なら後に描く粒だけ
	static void FlushParticle(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB, bool isBeforeDepthOfField);
	static void FlushLine(const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB);
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
	static void FlushMeshList(const std::vector<MeshRequest>& meshes, const std::wstring& windowTitle, D3D12_GPU_VIRTUAL_ADDRESS cameraCB, const char* name);
	// 今描いているビュー（windowTitle のウィンドウ・SetLayerVisible などで決めた見せ方）で描く物か。Flush の全部が使う
	static bool IsDrawnInView(const DrawTarget& target, const std::wstring& windowTitle);

	// シングルトン
	friend struct std::default_delete<RenderQueue>;
	static std::unique_ptr<RenderQueue> instance_;

	std::vector<MeshRequest> transparentMeshRequests_;
	std::vector<MeshRequest> opaqueMeshRequests_;
	std::vector<Request2d> requests2d_;
	std::vector<ParticleRequest> particleRequests_;
	std::vector<LineRequest> lineRequests_;
	std::vector<DecalRequest> decalRequests_;
	std::array<bool, magic_enum::enum_count<DrawLayer>()> isLayerVisible_{}; // 層ごとに、今のビューで描くか（番号は DrawLayer の順。magic_enum::enum_count で層の数を数える）
	bool isSceneHiddenVisible_ = true; // 目で隠した物を、今のビューで描くか（Scene ビューだけ false）
};