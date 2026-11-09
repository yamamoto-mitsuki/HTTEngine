#include "RootSignatureManager.h"
#include <format>
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Graphics/GPU/DirectXCommon.h"
#include "HTTEngine/Graphics/Pipeline/VertexFormat.h"

// 静的メンバ変数
std::unique_ptr<RootSignatureManager> RootSignatureManager::instance_ = nullptr;


//=============================================================================
// 初期化 / 解放
//=============================================================================
// ===== 初期化 =====
void RootSignatureManager::Initialize() {
	instance_ = std::make_unique<RootSignatureManager>(ConstructorKey());
	LogManager::Log("Initialized");
}

// ===== 解放 =====
void RootSignatureManager::Release() {
	instance_.reset();
	instance_ = nullptr;
	LogManager::Log("Released");
}


//=============================================================================
// vs, psなどペアのシェーダーを結びつける
//=============================================================================
std::vector<MergedBind> RootSignatureManager::MergeStages(const std::vector<ShaderResourceBinding>& vs, const std::vector<ShaderResourceBinding>& ps) {
	std::vector<MergedBind> out;
	out.reserve(vs.size() + ps.size());
	// VS側は全部 VERTEX として入れる
	for (const auto& v : vs) {
		out.push_back({v, D3D12_SHADER_VISIBILITY_VERTEX});
	}
	// PS側は、同じリソース(type/register/space が一致)があれば ALL に昇格、
	// 無ければ PS専用として追加
	for (const auto& p : ps) {
		auto it = std::find_if(out.begin(), out.end(), [&](const MergedBind& m) { 
			return m.bind.name == p.name &&                     // 変数名が一致しているか
				   m.bind.type == p.type &&                     // CBufferなど型が一致しているか
				   m.bind.registerNumber == p.registerNumber && // レジスタ番号が一致しているか
				   m.bind.space == p.space; });                 // space番号が一致しているか
		if (it != out.end()) {
			it->visibility = D3D12_SHADER_VISIBILITY_ALL; // VSにもPSにもある
		} else {
			out.push_back({p, D3D12_SHADER_VISIBILITY_PIXEL}); // PS専用
		}
	}
	return out;
}


//=============================================================================
// hlslの変数名を RootBindに変換
//=============================================================================
std::optional<RootBind> RootSignatureManager::NameToRole(std::string_view name) {
	static const std::unordered_map<std::string_view, RootBind> table = {
	    {"gObjectTransform",      RootBind::ObjectTransform     },
	    {"gMaterial",             RootBind::Material            },
	    {"gDirectionalLight",     RootBind::DirectionalLight    },
	    {"gCamera",               RootBind::Camera              },
	    {"gPointLights",          RootBind::PointLights         },
	    {"gSpotLights",           RootBind::SpotLights          },
	    {"gParticles",            RootBind::Particles           },
	    {"gSkybox",               RootBind::Skybox              },
	    {"gIBL",	              RootBind::IBL                 },
	    {"gBloom",                RootBind::Bloom               },
	    {"gLens",                 RootBind::Lens                },
	    {"gDof",                  RootBind::DepthOfField        },
	    {"gTextures",             RootBind::BindlessTexture     },
	    {"gTexturesCube",         RootBind::BindlessTextureCube },
	    {"gDecal",                RootBind::Decal               },
	    {"gDrawData",             RootBind::DrawData            },
	};

	if (auto it = table.find(name); it != table.end()) {
		return it->second;
	}
	return std::nullopt; // 役割不明（slotOfに載せない）
}


//=============================================================================
// Resourceから RootParameter 作成
//=============================================================================
D3D12_ROOT_PARAMETER1 RootSignatureManager::MakeRootParameter(const MergedBind& m, std::vector<D3D12_DESCRIPTOR_RANGE1>& ranges) {
	const ShaderResourceBinding& b = m.bind;
	D3D12_ROOT_PARAMETER1 param{};
	param.ShaderVisibility = m.visibility;

	if (b.type == D3D_SIT_CBUFFER) {
		// --- ルートCBV ---
		param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		param.Descriptor.ShaderRegister = b.registerNumber;
		param.Descriptor.RegisterSpace = b.space;
	} else if (b.type == D3D_SIT_STRUCTURED && b.count != 0) {
		// --- 単体SRV（配列でないStructuredBuffer, gParticles）---
		param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
		param.Descriptor.ShaderRegister = b.registerNumber;
		param.Descriptor.RegisterSpace = b.space;
	} else if (b.type == D3D_SIT_UAV_RWSTRUCTURED && b.count != 0) {
		// --- 単体UAV（配列ではないRWStructuredBuffer。CSが書き込む先）---
		param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
		param.Descriptor.ShaderRegister = b.registerNumber;
		param.Descriptor.RegisterSpace = b.space;
	} else {
		// ByteAddressBuffer などはまだ作っていない（下の範囲に入れると、違う種類で結んでしまう）
		HTT_ASSERT_MSG(
		    b.type == D3D_SIT_TEXTURE || b.type == D3D_SIT_STRUCTURED || b.type == D3D_SIT_UAV_RWTYPED,
		    std::format("'{}' の種類（D3D_SHADER_INPUT_TYPE = {}）は、まだルートシグネチャに入れられません", b.name, static_cast<int>(b.type)));

		// --- テクスチャ = DescriptorTable（読むテクスチャの配列 gTextures[]（bindless）か、CS が書く RWTexture2D）---
		// ルートの SRV・UAV（上の2つ）はバッファしか結べないので、テクスチャはヒープに置いた窓口を表（テーブル）で指す
		const bool isWritable = (b.type == D3D_SIT_UAV_RWTYPED);
		D3D12_DESCRIPTOR_RANGE1& range = ranges.emplace_back();
		range.RangeType = isWritable ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV : D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		range.NumDescriptors = (b.count == 0) ? UINT_MAX : b.count; // 0=無制限
		range.BaseShaderRegister = b.registerNumber;
		range.RegisterSpace = b.space;
		// 書く物は、中身が CS の中で変わる（DATA_VOLATILE）とドライバーに伝える
		range.Flags = isWritable ? (D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE) : D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
		range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		param.DescriptorTable.NumDescriptorRanges = 1;
		param.DescriptorTable.pDescriptorRanges = &range; // ranges.reserve済み前提
	}
	return param;
}


//=============================================================================
// Resourceから Sampler 作成
//=============================================================================
D3D12_STATIC_SAMPLER_DESC RootSignatureManager::MakeStaticSampler(const MergedBind& m) {
	D3D12_STATIC_SAMPLER_DESC s{};
	s.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR; // フィルタはreflectionで取れないので規約

	// --- UV がはみ出したときの読み方（これも reflection で取れないので、変数名で決める規約）---
	// gSamplerClamp：端の色で止める。画面の絵を読むポストエフェクト用（WRAP だと画面の反対の端の色が混ざる）
	// それ以外      ：繰り返す。モデルのテクスチャをタイル状に並べる用
	// 変数名は Resources/Texture.hlsli と合わせる
	const bool isClamp = (m.bind.name == "gSamplerClamp");
	const D3D12_TEXTURE_ADDRESS_MODE addressMode = isClamp ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	s.AddressU = s.AddressV = s.AddressW = addressMode;

	s.MaxLOD = D3D12_FLOAT32_MAX;
	s.ShaderRegister = m.bind.registerNumber;
	s.RegisterSpace = m.bind.space;
	s.ShaderVisibility = m.visibility;
	return s;
}


//=============================================================================
// MergeBind から RootSignature 作成（描画用・CS用で共通）
//=============================================================================
Microsoft::WRL::ComPtr<ID3D12RootSignature>
    RootSignatureManager::BuildRootSignature(const std::vector<MergedBind>& binds, D3D12_ROOT_SIGNATURE_FLAGS flags, std::unordered_map<std::string, RootParameterInfo>& outParameterOf) {
	std::vector<D3D12_ROOT_PARAMETER1> params;
	std::vector<D3D12_DESCRIPTOR_RANGE1> ranges;
	ranges.reserve(binds.size()); // 再確保でポインタが飛ばないよう予約
	std::vector<D3D12_STATIC_SAMPLER_DESC> samplers;

	// ===== MergedBind を部品に振り分け =====
	for (const auto& m : binds) {
		if (m.bind.type == D3D_SIT_SAMPLER) {
			samplers.push_back(MakeStaticSampler(m)); // StaticSamplerはスロットを消費しない
			continue;
		}
		const UINT slot = static_cast<UINT>(params.size());
		params.push_back(MakeRootParameter(m, ranges));
		outParameterOf[m.bind.name] = RootParameterInfo{slot, m.bind.type};
	}

	// ===== シリアライズして作成 =====
	D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc{};
	desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
	desc.Desc_1_1.NumParameters = static_cast<UINT>(params.size());
	desc.Desc_1_1.pParameters = params.data();
	desc.Desc_1_1.NumStaticSamplers = static_cast<UINT>(samplers.size());
	desc.Desc_1_1.pStaticSamplers = samplers.data();
	desc.Desc_1_1.Flags = flags;
	return CreateRootSignature(desc);
}


//=============================================================================
// MergeBind から RootSignature 作成（描画用）
//=============================================================================
RootSignatureInfo RootSignatureManager::MakeRootSignatureInfo(const std::vector<MergedBind>& binds) {
	// 描画用：IA（頂点バッファ）を使う ＋ ヒープを番号で直接引ける
	const D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED; // 手書き版に合わせる
	std::unordered_map<std::string, RootParameterInfo> parameterOf;
	RootSignatureInfo result;
	result.rootSignature = BuildRootSignature(binds, flags, parameterOf);

	// --- HLSLの変数名 → 役割（RootBind）に置き換える ---
	for (const auto& [name, parameter] : parameterOf) {
		auto role = NameToRole(name);
		HTT_ASSERT_MSG(role.has_value(), std::format("シェーダーのリソース名 '{}' が NameToRole に未登録です", name));
		result.slotOf[*role] = parameter.slot;
	}
	return result;
}


//=============================================================================
// CSのリフレクションから RootSignature 作成（CS用）
//=============================================================================
ComputeRootSignatureInfo RootSignatureManager::MakeComputeRootSignatureInfo(const std::vector<ShaderResourceBinding>& binds) {
	// --- CSは1本だけなので、ステージを混ぜる必要は無い ---
	// CSから見えるのは ShaderVisibility が ALL のルートパラメータだけ（VERTEX・PIXEL にすると CS から読めない）
	std::vector<MergedBind> merged;
	merged.reserve(binds.size());
	for (const ShaderResourceBinding& b : binds) {
		merged.push_back({b, D3D12_SHADER_VISIBILITY_ALL});
	}

	// --- 作る（CSは頂点バッファを使わないので、IAのフラグは要らない）---
	ComputeRootSignatureInfo result;
	result.rootSignature = BuildRootSignature(merged, D3D12_ROOT_SIGNATURE_FLAG_NONE, result.parameterOf);
	return result;
}


//=============================================================================
// Desc から RootSignature作成
//=============================================================================
Microsoft::WRL::ComPtr<ID3D12RootSignature> RootSignatureManager::CreateRootSignature(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC& desc) {
	// シリアライズしてバイナリに
	Microsoft::WRL::ComPtr<ID3DBlob> blob, error;
	HRESULT hr = D3D12SerializeVersionedRootSignature(&desc, &blob, &error);
	if (FAILED(hr)) {
		if (error) {
			LogManager::Error(reinterpret_cast<char*>(error->GetBufferPointer()));
		}
		HTT_ASSERT_MSG(false, "RootSignatureのシリアライズ失敗");
	}

	// バイナリを元に生成
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature = nullptr;
	hr = DirectXCommon::GetDevice()->CreateRootSignature(
	    0,                        // nodeMask: GPUが1台なら0
	    blob->GetBufferPointer(), // 作ったバイナリの先頭
	    blob->GetBufferSize(),    // そのバイト数
	    IID_PPV_ARGS(&rootSignature));
	if (FAILED(hr)) {
		LogManager::Error(std::format("Error Code: 0x{:08X}", static_cast<uint32_t>(hr)));
		HTT_ASSERT_MSG(false, "RootSignatureの生成失敗");
	}

	return rootSignature;
}