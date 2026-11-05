#pragma once
#include <array>
#include <memory>
#include <vector>
#include <unordered_map>
#include <string>
#include <string_view>

#include <d3d12shader.h>
#include <dxcapi.h>
#include <wrl.h>

#include <externals/magic_enum/magic_enum.hpp>

// 生成 .hlsl の置き場（Loader と Compiler で共有）
inline constexpr const wchar_t* kShaderGeneratedDir = L"MyEngine/Shader/Generated/";
// .shader の置き場（エンジンのフォルダからの相対。ゲームでは exe の隣にコピーされた物もこの形）
inline constexpr const wchar_t* kShaderDataDir = L"MyEngine/Shader/Data";
// ゲームの .shader を探すフォルダ（起動するフォルダからの相対。Project ウィンドウ・アセットの一覧と同じ resources）
inline constexpr const wchar_t* kProjectShaderDir = L"resources";
// ゲームの .shader から作る .hlsl の置き場（kShaderGeneratedDir の下。エンジンの .hlsl と場所がぶつからないように分ける）
inline constexpr const wchar_t* kProjectGeneratedSubDir = L"Project";

// シェーダーから取り出した1つのResource情報
struct ShaderResourceBinding {
	std::string name;                // 変数名 "gCamera"
	D3D_SHADER_INPUT_TYPE type;      // 型 CBUFFER / TEXTURE / SAMPLER / STRUCTURED ...
	UINT registerNumber;             // register番号 (b2 の 2)
	UINT count;                      // 配列要素数。0 = 無制限(bindless)
	UINT space;                      // register space (space1 など)
	D3D_SRV_DIMENSION texSDimension; // テクスチャの形状（2D, Cubeなど）
};

// シェーダーから取り出した1つの入力パラメータ
struct ShaderInputParameter {
	std::string semanticName;                  // セマンティクス名 "POSITION"
	D3D_NAME systemValueType;                  // SV_VertexIDなどシステム値
	UINT semanticIndex;                        // セマンティクスのインデックス "TEXCOORD0"の0
	UINT registerIndex;                        // 入力スロット内でのパラメータ順序（レジスタ位置）
	D3D_REGISTER_COMPONENT_TYPE componentType; // float, int などの型
	BYTE mask;                                 // 0x0F(xyzw),0x07(xyz)など成分数を示すビットマスク
	BYTE readWriteMask;                        // float3でもシェーダーがxyしか使ってないなど判断してくれる
};

// 定数バッファの中の変数1つ（ConstantBuffer<構造体> の構造体のメンバー1つ）
struct ShaderConstantVariable {
	std::string name;                                     // 変数名 "baseColor"（入れ子の構造体は "outer.inner"）
	UINT offset = 0;                                      // 定数バッファの先頭からのバイト
	UINT size = 0;                                        // バイト数
	D3D_SHADER_VARIABLE_CLASS typeClass = D3D_SVC_SCALAR; // 形：SCALAR（float）・VECTOR（float4）・MATRIX_ROWS（float4x4）など
	D3D_SHADER_VARIABLE_TYPE type = D3D_SVT_FLOAT;        // 中身の型：FLOAT・UINT・INT・BOOL など
	UINT rows = 1;                                        // 行の数（行列以外は 1）
	UINT columns = 1;                                     // 列の数（float3 なら 3）
	UINT elements = 0;                                    // 配列の要素の数（配列でなければ 0）
};

// 定数バッファ1つの中身（変数名・場所・大きさ）
struct ShaderConstantBuffer {
	std::string name;                              // 変数名 "gMaterial"
	UINT size = 0;                                 // 全体のバイト数（16 の倍数）
	std::vector<ShaderConstantVariable> variables; // 中の変数（場所の順）

	// 名前で変数を探す（無ければ nullptr）
	const ShaderConstantVariable* FindVariable(std::string_view variableName) const;
};

// 1つのシェーダに入ってる情報をまとめたもの
struct ShaderReflection {
	std::vector<ShaderResourceBinding> resources;      // 全Resource情報
	std::vector<ShaderInputParameter> inputs;          // 全Input情報
	std::vector<ShaderConstantBuffer> constantBuffers; // 全定数バッファの中身（使われていない定数バッファはコンパイルで消えるので入らない）
	Microsoft::WRL::ComPtr<IDxcBlob> blob;             // コンパイル結果
	std::array<UINT, 3> threadGroupSize{};             // CSの [numthreades(x, y, z)]。CS以外は0のまま

	// 名前で定数バッファを探す（無い・使われていないなら nullptr）
	const ShaderConstantBuffer* FindConstantBuffer(std::string_view bufferName) const;
};


/// <summary>
/// シェーダーのコンパイルと保管を管理するクラス
/// </summary>
class ShaderCompiler {
public:
	// ===== Passkey Idiom =====
	class ConstructorKey {
	private:
		ConstructorKey() = default;
		friend class ShaderCompiler;
	};
	explicit ShaderCompiler(ConstructorKey) {} // 鍵を受け取るコンストラクタ

	static void Initialize();
	static void Release();

	/// <summary>
	/// Shader/Generatedルートの相対パスでコンパイル&reflection取得（キャッシュ付き）
	/// </summary>
	static const ShaderReflection& CompileShaderReflection(const std::wstring& path, const std::wstring& profile, const std::wstring& entry = L"main");

private:
	/// <summary>
	/// シェーダーのコンパイルを行う
	/// </summary>
	static Microsoft::WRL::ComPtr<IDxcResult> CompileShader(const std::wstring& fullPath, const std::wstring& profile, const std::wstring& entry);

	/// <summary>
	/// シェーダーコンパイル結果からShaderBlobを取得する
	/// </summary>
	/// <param name="result">CompileShader関数の戻り値</param>
	static Microsoft::WRL::ComPtr<IDxcBlob> GetShaderBlob(IDxcResult* result);

	/// <summary>
	/// シェーダーコンパイルの結果から、中身を調べる道具（リフレクション）を作る
	/// </summary>
	/// <param name="result">CompileShader関数の戻り値</param>
	static Microsoft::WRL::ComPtr<ID3D12ShaderReflection> CreateReflection(IDxcResult* result);

	/// <summary>
	/// リフレクションからレジスタなど情報を取得する
	/// </summary>
	/// <param name="reflection">CreateReflection関数の戻り値</param>
	static std::vector<ShaderResourceBinding> MakeShaderResourceBinding(ID3D12ShaderReflection* reflection);

	/// <summary>
	/// リフレクションから入力パラメータを取得する
	/// </summary>
	/// <param name="reflection">CreateReflection関数の戻り値</param>
	static std::vector<ShaderInputParameter> MakeShaderInputParameter(ID3D12ShaderReflection* reflection);

	/// <summary>
	/// リフレクションから定数バッファの中身（変数名・場所・大きさ）を取得する
	/// </summary>
	/// <param name="reflection">CreateReflection関数の戻り値</param>
	/// <param name="binds">MakeShaderResourceBinding関数の戻り値（定数バッファの名前を、ここから拾う）</param>
	static std::vector<ShaderConstantBuffer> MakeShaderConstantBuffers(ID3D12ShaderReflection* reflection, const std::vector<ShaderResourceBinding>& binds);

	/// <summary>
	/// 変数の型を見て、中の変数を out に足す（構造体なら、メンバーを1つずつ。入れ子の構造体も下まで）
	/// </summary>
	/// <param name="baseOffset">この型の先頭の、定数バッファの先頭からのバイト</param>
	/// <param name="prefix">名前の頭に付ける言葉（入れ子の構造体の名前。一番外は空）</param>
	static void AppendConstantVariables(ID3D12ShaderReflectionType* type, UINT baseOffset, const std::string& prefix, std::vector<ShaderConstantVariable>& out);


	~ShaderCompiler() = default;

	// シングルトン
	friend struct std::default_delete<ShaderCompiler>;
	static std::unique_ptr<ShaderCompiler> instance_;
	std::unordered_map<std::wstring, ShaderReflection> cache_;
};