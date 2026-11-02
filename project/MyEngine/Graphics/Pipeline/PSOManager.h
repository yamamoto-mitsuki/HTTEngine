#pragma once
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <wrl.h>
#include <d3d12.h>
#include <dxcapi.h>
#include "MyEngine/Graphics/Compute/ComputePipeline.h"
#include "MyEngine/Graphics/Pipeline/RenderStates.h"
#include "MyEngine/Graphics/Pipeline/VertexFormat.h"
#include "MyEngine/Graphics/Pipeline/ShaderCompiler.h"
#include "MyEngine/Graphics/Pipeline/ShaderConstants.h"
#include "MyEngine/Graphics/Pipeline/RootSignatureManager.h"

// 入力されたプリミティブをどう解釈するか
enum class TopologyID {
	Triangle,
	Line,
};

// PSOの一意キー
struct PSOKey {
	uint32_t shaderProgramID;
	BlendMode blendMode;
	RasterizerType rasterizerType;
	DepthMode depthMode;
	bool operator==(const PSOKey&) const = default;
};
struct PSOHash {
	size_t operator()(const PSOKey& k) const { 
		size_t h = static_cast<size_t>(k.shaderProgramID);
		h = h * 31 + static_cast<size_t>(k.blendMode);
		h = h * 31 + static_cast<size_t>(k.rasterizerType);
		h = h * 31 + static_cast<size_t>(k.depthMode);
		return h;
	}
};


/// <summary>
/// PSO・RootSignatureを一元管理するクラス
/// </summary>
class PSOManager {
public:
	// ===== Passkey Idiom =====
	class ConstructorKey {
	private:
		ConstructorKey() = default;
		friend class PSOManager;
	};
	explicit PSOManager(ConstructorKey) {} // 鍵を受け取るコンストラクタ

	// コピー・ムーブ禁止
	PSOManager(const PSOManager&) = delete;
	PSOManager& operator=(const PSOManager&) = delete;
	PSOManager(PSOManager&&) = delete;
	PSOManager& operator=(PSOManager&&) = delete;

	static void Initialize();
	static void Release();

	//==========================================
	// Get～
	//==========================================

	/// <summary>
	/// 描画情報から使用する Shader が分かるIDを取得
	/// </summary>
	/// <param name="drawCategory">描画したい形状</param>
	/// <param name="shadingType">HalfLambert, Unlitなどの表現したいShading</param>
	/// <returns></returns>
	static uint32_t GetShaderProgramID(DrawCategory drawCategory, ShadingType shadingType = ShadingType::Unlit);

	/// <summary>
	/// 描画情報から TopologyID を入手
	/// </summary>
	/// <param name="category">描画したい形</param>
	static TopologyID GetTopologyID(DrawCategory category);

	/// <summary>
	/// GraphicsPipelineState を取得するための PSOKey を取得する
	/// </summary>
	/// <param name="drawCategory">描画する形</param>
	/// <param name="shadingType">シェーディング設定</param>
	/// <param name="blendMode">ブレンド設定</param>
	/// <param name="rasterizerType">ラスタライザ設定</param>
	/// <param name="depthMode">深度設定</param>
	static PSOKey GetPSOKey(DrawCategory drawCategory, ShadingType shadingType, BlendMode blendMode = BlendMode::Normal, 
		RasterizerType rasterizerType = RasterizerType::SolidBack, DepthMode depthMode = DepthMode::TestWrite);

	/// <summary>
	/// PipelineStateの状態名を取得
	/// </summary>
	static const char* GetStateName(const PSOKey& key);

	/// <summary>
	/// 描画用のソートキーを取得を取得。値が近いほど描画状態の切り替えが少ない
	/// </summary>
	/// <param name="key"></param>
	/// <returns></returns>
	static uint64_t GetSortKey(const PSOKey& key);

	/// <summary>
	/// ShadingTypeの組み合わせからRootSignatureを取得
	/// </summary>
	/// <param name="id"></param>
	/// <returns></returns>
	static const RootSignatureInfo& GetRootSignatureInfo(uint32_t id);

	/// <summary>
	/// PipelineStateを取得
	/// </summary>
	/// <param name="rootSignatureID"></param>
	/// <param name="inputLayoutID"></param>
	/// <param name="ShaderProgramID"></param>
	/// <param name="blendMode"></param>
	/// <param name="topologyID"></param>
	/// <returns></returns>
	static Microsoft::WRL::ComPtr<ID3D12PipelineState> GetPSO(const PSOKey& key);

	/// <summary>
	/// CSの名前から ComputePipeline を取得（初めて使うときに作る）
	/// </summary>
	/// <param name="csName">.shader の path から作った名前（"Compute/Skinning.CS.hlsl" なら "SkinningCS"）</param>
	static const ComputePipeline& GetComputePipeline(const std::string& csName);


	//==========================================
	// PSO作成
	//==========================================

	/// <summary>
	/// CreatePSO に必要な PipelineState設定 を作成する
	/// </summary>
	static D3D12_GRAPHICS_PIPELINE_STATE_DESC MakePSO(const PSOKey& key, std::vector<D3D12_INPUT_ELEMENT_DESC>& outElems);

	/// <summary>
	/// PSOKey を元に ID3D12PipelineState を作成する
	/// </summary>
	/// <param name="key"></param>
	/// <returns></returns>
	static Microsoft::WRL::ComPtr<ID3D12PipelineState> CreatePSO(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& desc);


private:
	/// <summary>
	/// TopologyID から PipelineStateDesc の作成に使う設定を作成
	/// </summary>
	static D3D12_PRIMITIVE_TOPOLOGY_TYPE GetTopologyType(TopologyID id);

	
	~PSOManager() = default;

	// シングルトン
	friend struct std::default_delete<PSOManager>;
	static std::unique_ptr<PSOManager> instance_;

	std::unordered_map<PSOKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>, PSOHash> psoMap_; // PSOをキーで管理
	std::vector<RootSignatureInfo> rootSignatureCache_; // RootSignatureの情報をまとめたもの
	std::unordered_map<std::string, ComputePipeline> computePipelines_; // CSの名前 → ComputePipeline
};