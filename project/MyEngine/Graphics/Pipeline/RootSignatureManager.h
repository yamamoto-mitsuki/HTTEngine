#pragma once
#include <span>
#include <array>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <d3d12.h>
#include <wrl.h>
#include <externals/magic_enum/magic_enum.hpp>
#include "MyEngine/Graphics/Pipeline/RenderStates.h"
#include "MyEngine/Graphics/Pipeline/ShaderCompiler.h"


// レジスタに送るリソースの役割
enum class RootBind {
	ObjectTransform,      // ObjectTransformData (VS)
	Material,             // Material3dData / Material2dData / MaterialLineData (PS)
	DirectionalLight,     // DirectionalLightData (PS)
	PointLights,          // PointLight（PS）
	SpotLights,           // SpotLights（PS）
	Camera,               // CameraData (PS)
	Particles,            // Particle
	Skybox,               // 天球
	IBL,                  // Image Base Light（PS）
	Bloom,                // ブルームのパラメータ（PS）
	Lens,                 // レンズ効果（PS）
	DepthOfField,         // 被写界深度のパラメータ（合成の PS）
	BindlessTexture,      // バインドレステクスチャ
	BindlessTextureCube,  // バインドレスキューブテクスチャ
	Decal,                // デカールの箱（ワールド → 箱の中の行列・深度の番号）。全部のデカールで共通（PS）
	DrawData,             // 描き手が描く物ごとに送る値（形・進み具合など。中身は描き手ごとの構造体。PS）
};

// マージ後の1リソース（reflection情報 + どのステージから見えるか）
struct MergedBind {
	ShaderResourceBinding bind;         // シェーダーの1つのResource情報
	D3D12_SHADER_VISIBILITY visibility; // VS, PSなどどこで使うか
};

// ルートシグニチャとルートパラメーターの番号をまとめたもの
struct RootSignatureInfo {
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
	std::unordered_map<RootBind, UINT> slotOf; // ルートパラメータ番号
};

// ルートパラメータ1つ分（番号とHLSLでの種類）
struct RootParameterInfo {
	UINT slot;                  // ルートパラメータ番号
	D3D_SHADER_INPUT_TYPE type; // CBUFFER, STRUCTURED, UAV_RWSTRUCTURED など
};

// CS用のルートシグニチャと、HLSLの変数名からルートパラメータを引く表
// 描画用（RootSignatureInfo）と違い、役割（RootBind）は決めない。CSは呼ぶ側がHLSLの変数名を知っているので、名前で引く
struct ComputeRootSignatureInfo {
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature;
	std::unordered_map<std::string, RootParameterInfo> parameterOf; // "gOutput" → {番号, 種類}
};


/// <summary>
/// RootSignatureの生成、登録、管理をするクラス
/// </summary>
class RootSignatureManager {
public:
	// ===== Passkey Idiom =====
	class ConstructorKey {
	private:
		ConstructorKey() = default;
		friend class RootSignatureManager;
	};
	explicit RootSignatureManager(ConstructorKey) {} // 鍵を受け取るコンストラクタ

	static void Initialize();
	static void Release();

	/// <summary>
	/// vs, psなどペアのシェーダーを結びつける
	/// </summary>
	static std::vector<MergedBind> MergeStages(const std::vector<ShaderResourceBinding>& vs, const std::vector<ShaderResourceBinding>& ps);

	/// <summary>
	/// 自動でRootSignature作成
	/// </summary>
	static RootSignatureInfo MakeRootSignatureInfo(const std::vector<MergedBind>& binds);

	/// <summary>
	/// CS 1本のリフレクションから、CS用のRootSignatureを自動で作る
	/// </summary>
	/// <param name="binds">CSのリソース情報（ShaderReflection::resources）</param>
	static ComputeRootSignatureInfo MakeComputeRootSignatureInfo(const std::vector<ShaderResourceBinding>& binds);

private:
	/// <summary>
	/// 描画用・CS用で共通の作り方。bindsを順にRootParameter（Sampler は StaticSampler）にしてRootSignatureを作る
	/// </summary>
	/// <param name="flags">描画用は IA(頂点バッファ)を使うなど、用途で変わる</param>
	/// <param name="outParameterOf">HLSLの変数名 → ルートパラメーター（Samplerは番号を使わないので入らない）</param>
	static Microsoft::WRL::ComPtr<ID3D12RootSignature> BuildRootSignature(const std::vector<MergedBind>& binds, 
		D3D12_ROOT_SIGNATURE_FLAGS flags, std::unordered_map<std::string, RootParameterInfo>& ourParameterOf);

	/// <summary>
	/// シェーダーのResource名とRootBindを結びつける
	/// </summary>
	/// <param name="name">シェーダーのResource名</param>
	static std::optional<RootBind> NameToRole(std::string_view name);

   
    static D3D12_ROOT_PARAMETER1 MakeRootParameter(const MergedBind& m, std::vector<D3D12_DESCRIPTOR_RANGE1>& ranges);

	static D3D12_STATIC_SAMPLER_DESC MakeStaticSampler(const MergedBind& m);

	/// <summary>
	/// 引数の設定を元に RootSignature を作成する。
	/// </summary>
	static Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateRootSignature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC& desc);

	// インスタンス
	friend struct std::default_delete<RootSignatureManager>;
	static std::unique_ptr<RootSignatureManager> instance_;
};