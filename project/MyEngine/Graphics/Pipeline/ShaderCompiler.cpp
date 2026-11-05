#include "MyEngine/Graphics/Pipeline/ShaderCompiler.h"
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/Graphics/GPU/DirectXCommon.h"
#include "MyEngine/String/ConvertString.h"
#include <algorithm>
#include <array>
#include <format>

// 静的メンバ変数
std::unique_ptr<ShaderCompiler> ShaderCompiler::instance_ = nullptr;


//=============================================================================
// 名前で探す
//=============================================================================
// ===== 定数バッファの中の変数 =====
const ShaderConstantVariable* ShaderConstantBuffer::FindVariable(std::string_view variableName) const {
	const auto it = std::ranges::find(variables, variableName, &ShaderConstantVariable::name);
	return (it != variables.end()) ? &*it : nullptr;
}

// ===== 定数バッファ =====
const ShaderConstantBuffer* ShaderReflection::FindConstantBuffer(std::string_view bufferName) const {
	const auto it = std::ranges::find(constantBuffers, bufferName, &ShaderConstantBuffer::name);
	return (it != constantBuffers.end()) ? &*it : nullptr;
}


//=============================================================================
// 初期化 / 解放
//=============================================================================
// ===== 初期化 =====
void ShaderCompiler::Initialize() {
	MY_ASSERT_MSG(instance_ == nullptr, "Initializeが2回以上呼び出されています");
	instance_ = std::make_unique<ShaderCompiler>(ConstructorKey());
	LogManager::Log("Initialized");
}

// ===== 解放 =====
void ShaderCompiler::Release() {
	instance_.reset();
	LogManager::Log("Released");
}


//=============================================================================
// シェーダーファイルを取得
//=============================================================================
const ShaderReflection& ShaderCompiler::CompileShaderReflection(const std::wstring& path, const std::wstring& profile, const std::wstring& entry) {
	auto& cache = instance_->cache_;
	// 既にあるならキャッシュ
	if (auto it = cache.find(path); it != cache.end()) {
		return it->second;
	}
	// コンパイルしてルートシグニチャなど取得
	Microsoft::WRL::ComPtr<IDxcResult> result = CompileShader(kShaderGeneratedDir + path, profile, entry);
	ShaderReflection refl;
	refl.blob = GetShaderBlob(result.Get()); // エラーチェック込み
	// リフレクションは1回だけ作り、3つの情報を読む
	Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection = CreateReflection(result.Get());
	refl.resources = MakeShaderResourceBinding(reflection.Get());
	refl.inputs = MakeShaderInputParameter(reflection.Get());
	refl.constantBuffers = MakeShaderConstantBuffers(reflection.Get(), refl.resources);
	// CSの[numthreads(x, y, z)]。VS・PSでは 0 が入る
	reflection->GetThreadGroupSize(&refl.threadGroupSize[0], &refl.threadGroupSize[1], &refl.threadGroupSize[2]);
	return cache.emplace(path, std::move(refl)).first->second;
}


//=============================================================================
// シェーダーコンパイル
//=============================================================================
Microsoft::WRL::ComPtr<IDxcResult> ShaderCompiler::CompileShader(const std::wstring& fullPath, const std::wstring& profile, const std::wstring& entry) {
	auto utils = DirectXCommon::GetDxcUtils();
	auto compiler = DirectXCommon::GetDxcCompiler();
	auto includeHandler = DirectXCommon::GetIncludeHandler();

	// --- hlslを読む ---
	Microsoft::WRL::ComPtr<IDxcBlobEncoding> source;
	HRESULT hr = utils->LoadFile(fullPath.c_str(), nullptr, &source);
	if (FAILED(hr)) {
		LogManager::Error(std::format("Error Code: 0x{:08X}", (uint32_t)hr));
		MY_ASSERT_MSG(false, "シェーダーファイルの読み込みに失敗しました");
	}
	DxcBuffer buffer{};
	buffer.Ptr = source->GetBufferPointer();
	buffer.Size = source->GetBufferSize();
	buffer.Encoding = DXC_CP_UTF8;

	// --- コンパイル引数 ---
	auto args = std::to_array<LPCWSTR>({
	    fullPath.c_str(),       // コンパイル対象
	    L"-E", entry.c_str(),   // エントリーポイントの指定。基本的にmain以外にはしない
	    L"-T", profile.c_str(), // ShaderProfileの設定
	    L"-I", kShaderGeneratedDir, // 生成先＝検索パス。常に一致する
	    L"-Zi",                 // 
	    L"-Qembed_debug",       // デバック用の情報を埋め込む
	    L"-Od",                 // 最適化を外しておく
	    L"-Zpr",                // メモリレイアウトは行優先
	});
	Microsoft::WRL::ComPtr<IDxcResult> result;
	hr = compiler->Compile(&buffer, args.data(), static_cast<UINT32>(args.size()), includeHandler, IID_PPV_ARGS(&result));
	if (FAILED(hr)) {
		LogManager::Error(std::format("Error Code: 0x{:08X}", (uint32_t)hr));
		MY_ASSERT_MSG(false, "Compile呼び出しに失敗しました");
	}

	return result;
}


//=============================================================================
// シェーダーコンパイル結果からShaderBlobを取得する
//=============================================================================
Microsoft::WRL::ComPtr<IDxcBlob> ShaderCompiler::GetShaderBlob(IDxcResult* result) {
	HRESULT hr;
	// 警告・エラーチェック
	Microsoft::WRL::ComPtr<IDxcBlobUtf8> error;
	result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&error), nullptr);
	if (error != nullptr && error->GetStringLength() != 0) {
		LogManager::Error(error->GetStringPointer());
		MY_ASSERT_MSG(false, "シェーダーコンパイルエラー");
	}
	// 結果取得
	Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob;
	hr = result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	if (FAILED(hr)) {
		LogManager::Error(std::format("Error Code: 0x{:08X}", (uint32_t)hr));
		MY_ASSERT_MSG(false, "シェーダーバイナリの取得に失敗しました");
	}

	return shaderBlob;
}


//=============================================================================
// シェーダーコンパイルの結果から、中身を調べる道具（リフレクション）を作る
//=============================================================================
Microsoft::WRL::ComPtr<ID3D12ShaderReflection> ShaderCompiler::CreateReflection(IDxcResult* result) {
	// コンパイル結果から、リフレクション用の部分を取り出す
	Microsoft::WRL::ComPtr<IDxcBlob> reflBlob;
	HRESULT hr = result->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(&reflBlob), nullptr);
	MY_ASSERT_MSG(SUCCEEDED(hr) && reflBlob != nullptr, "リフレクション情報の取得に失敗（直前のシェーダーコンパイルエラーを確認）");
	// DxcBuffer に包んで、読める形（ID3D12ShaderReflection）にする
	DxcBuffer buf{reflBlob->GetBufferPointer(), reflBlob->GetBufferSize(), DXC_CP_ACP};
	Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
	hr = DirectXCommon::GetDxcUtils()->CreateReflection(&buf, IID_PPV_ARGS(&reflection));
	MY_ASSERT_MSG(SUCCEEDED(hr), "リフレクションの作成に失敗しました");
	return reflection;
}


//=============================================================================
// リフレクションからレジスタなど情報を取得する
//=============================================================================
std::vector<ShaderResourceBinding> ShaderCompiler::MakeShaderResourceBinding(ID3D12ShaderReflection* reflection) {
	// Shader情報取得
	D3D12_SHADER_DESC shaderDesc{};
	reflection->GetDesc(&shaderDesc);

	// --- 取得した情報を ShaderResourceBinding にいれる ---
	std::vector<ShaderResourceBinding> binds;
	// Bind情報の数分ループ
	for (UINT i = 0; i < shaderDesc.BoundResources; ++i) {
		D3D12_SHADER_INPUT_BIND_DESC bindDesc{};
		reflection->GetResourceBindingDesc(i, &bindDesc);
		binds.push_back({
		    bindDesc.Name,      // シェーダー内の変数名（例: gCamera）
		    bindDesc.Type,      // リソース種別（例: CBV, SRV, SAMPLER...）
		    bindDesc.BindPoint, // レジスタ番号（例: t0 → 0を返す）
		    bindDesc.BindCount, // バインド数（例: Texture2D tex[]なら0, ないなら1）
		    bindDesc.Space,     // レジスタスペース番号（例： space0なら0）
		    bindDesc.Dimension, // テクスチャの形状（2D, Cubeなど）
		});
		LogManager::Log(std::format("{} type={} reg={} space={} count={}", bindDesc.Name, (int)bindDesc.Type, bindDesc.BindPoint, bindDesc.Space, bindDesc.BindCount));
	}

	return binds;
}


//=============================================================================
// リフレクションから入力パラメータを取得する
//=============================================================================
std::vector<ShaderInputParameter> ShaderCompiler::MakeShaderInputParameter(ID3D12ShaderReflection* reflection) {
	// Shader情報取得
	D3D12_SHADER_DESC shaderDesc{};
	reflection->GetDesc(&shaderDesc);

	// --- 取得した情報を ShaderInputParameter にいれる ---
	std::vector<ShaderInputParameter> inputs;
	// 入力パラメータ分ループ
	for (UINT i = 0; i < shaderDesc.InputParameters; ++i) {
		D3D12_SIGNATURE_PARAMETER_DESC paramDesc{};
		reflection->GetInputParameterDesc(i, &paramDesc);

		inputs.push_back({
		    paramDesc.SemanticName,    // POSITION
		    paramDesc.SystemValueType, // SV_Positionなど
		    paramDesc.SemanticIndex,   // TEXCOORD0 の 0
		    paramDesc.Register,        // Register番号
		    paramDesc.ComponentType,   // Float32など
		    paramDesc.Mask,            // xyzw
		    paramDesc.ReadWriteMask    // 実際に使用する成分
		});
		LogManager::Log(std::format("{} ", paramDesc.SemanticName));
	}

	return inputs;
}

//=============================================================================
// リフレクションから定数バッファの中身を取得する
//=============================================================================
// ===== 定数バッファ1つずつ =====
std::vector<ShaderConstantBuffer> ShaderCompiler::MakeShaderConstantBuffers(ID3D12ShaderReflection* reflection, const std::vector<ShaderResourceBinding>& binds) {
	std::vector<ShaderConstantBuffer> buffers;
	for (const ShaderResourceBinding& bind : binds) {
		if (bind.type != D3D_SIT_CBUFFER) {
			continue; // テクスチャ・サンプラーなどは中身が無い
		}
		// --- 定数バッファの大きさと、中の変数の数 ---
		ID3D12ShaderReflectionConstantBuffer* constantBuffer = reflection->GetConstantBufferByName(bind.name.c_str());
		D3D12_SHADER_BUFFER_DESC bufferDesc{};
		if (FAILED(constantBuffer->GetDesc(&bufferDesc))) {
			continue; // 名前の違う物を引いた（ここには来ないはず）
		}
		ShaderConstantBuffer buffer;
		buffer.name = bind.name;
		buffer.size = bufferDesc.Size;
		// --- 中の変数 ---
		// ConstantBuffer<構造体> gMaterial と書くと、中には構造体の変数が1つだけ（名前は gMaterial）入る
		// その1つは名前を付けずに開き、構造体のメンバーを並べる（"gMaterial.baseColor" ではなく "baseColor"）
		for (UINT i = 0; i < bufferDesc.Variables; ++i) {
			ID3D12ShaderReflectionVariable* variable = constantBuffer->GetVariableByIndex(i);
			D3D12_SHADER_VARIABLE_DESC variableDesc{};
			variable->GetDesc(&variableDesc);
			const bool isWrapper = (bufferDesc.Variables == 1 && bind.name == variableDesc.Name);
			AppendConstantVariables(variable->GetType(), variableDesc.StartOffset, isWrapper ? std::string() : std::string(variableDesc.Name), buffer.variables);
		}
		buffers.push_back(std::move(buffer));
	}
	return buffers;
}

// ===== 型の中の変数（構造体は、メンバーを下まで開く）=====
void ShaderCompiler::AppendConstantVariables(ID3D12ShaderReflectionType* type, UINT baseOffset, const std::string& prefix, std::vector<ShaderConstantVariable>& out) {
	D3D12_SHADER_TYPE_DESC typeDesc{};
	type->GetDesc(&typeDesc);
	// --- 構造体：メンバーを1つずつ（名前は "外の名前.メンバー名"。一番外は prefix が空なのでメンバー名だけ）---
	if (typeDesc.Class == D3D_SVC_STRUCT && typeDesc.Elements == 0) {
		for (UINT i = 0; i < typeDesc.Members; ++i) {
			ID3D12ShaderReflectionType* memberType = type->GetMemberTypeByIndex(i);
			D3D12_SHADER_TYPE_DESC memberDesc{};
			memberType->GetDesc(&memberDesc);
			const std::string memberName = type->GetMemberTypeName(i);
			const std::string name = prefix.empty() ? memberName : prefix + "." + memberName;
			AppendConstantVariables(memberType, baseOffset + memberDesc.Offset, name, out);
		}
		return;
	}
	// --- 1つの値（float・float4・float4x4・その配列）---
	// 大きさは「成分の数 × 4 バイト」。配列は要素ごとに 16 バイトにそろうので、最後の要素以外は 16 バイトずつ
	constexpr UINT kComponentBytes = 4;  // float・uint・int・bool は 4 バイト
	constexpr UINT kRegisterBytes = 16;  // 定数バッファの1行（float4）の大きさ
	const UINT valueBytes = typeDesc.Rows * typeDesc.Columns * kComponentBytes;
	ShaderConstantVariable variable;
	variable.name = prefix;
	variable.offset = baseOffset;
	variable.size = (typeDesc.Elements > 0) ? kRegisterBytes * (typeDesc.Elements - 1) + valueBytes : valueBytes;
	variable.typeClass = typeDesc.Class;
	variable.type = typeDesc.Type;
	variable.rows = typeDesc.Rows;
	variable.columns = typeDesc.Columns;
	variable.elements = typeDesc.Elements;
	out.push_back(std::move(variable));
}
