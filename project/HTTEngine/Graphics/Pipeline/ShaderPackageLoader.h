#pragma once
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "HTTEngine/Graphics/Pipeline/RenderStates.h"
#include "HTTEngine/Graphics/Pipeline/ShaderCompiler.h"
#include "HTTEngine/Graphics/Pipeline/ShaderProperty.h"

// #META（全 .shader 共通）
struct ShaderMeta {
	enum class Stage { VS, PS, CS, Include };
	Stage stage{};
	std::wstring path;     // 作る .hlsl の場所（Generated からの相対）。.shader の場所から決める（#META には書かない）
	std::wstring profile;  // Include は空
	std::wstring entry = L"main";
};
// 描くときの状態の既定（#PROGRAM の blend・rasterizer・depth。.material で上書きできる）
struct ProgramStates {
	BlendMode blend = BlendMode::Normal;                   // 重ね方
	RasterizerType rasterizer = RasterizerType::SolidBack; // 裏面
	DepthMode depth = DepthMode::TestWrite;                // 深度
};
// #PROGRAM（描画PSのみ。無ければ has=false）
struct ProgramDef {
	bool has = false;
	DrawCategory category{};
	std::optional<ShadingType> shading; // 書いた物だけ (category, shading) で引ける（エンジンの決まった描き方）。書かない物はパスで引く
	std::string vsName;
	ProgramStates states;               // 描くときの状態の既定
};
// 1つの .shader を解析した結果（一時オブジェクト）
struct ShaderDefinition {
	ShaderMeta meta;
	ProgramDef program;
	std::vector<ShaderPropertyDef> properties; // #PROPERTIES（マテリアルの値の意味）
	std::string hlslBody;
	std::string source;    // 読んだ .shader のファイル名（エラーに出す）
	std::string assetPath; // .material が指すパス（"HTTEngine/Shader/Data/Model/Unlit.PS.shader"・"resources/shaders/Floor.PS.shader"）
	std::string name;      // 名前（エンジンの物は "UnlitPS"、ゲームの物は "Project/shaders/FloorPS"）
};
// レジストリ：1コンパイル単位（名前引き）
struct ShaderEntry {
	ShaderMeta::Stage stage{};
	std::wstring path, profile, entry;
	std::string assetPath; // どの .shader から作ったか（名前がぶつかったときに出す）
};
// レジストリ：1描画プログラム（(cat,shading)引き・パス引き）
struct ProgramEntry {
	std::string name;
	DrawCategory category{};
	std::optional<ShadingType> shading;           // エンジンの決まった描き方なら、その種類
	std::string vsName, psName;
	std::string assetPath;                        // PS の .shader のパス（.material が指すパス）
	ProgramStates states;                         // 描くときの状態の既定
	std::vector<ShaderPropertyDef> propertyDefs;  // #PROPERTIES
};

// ゲームの .shader の名前・作る .hlsl の頭に付ける言葉（エンジンの物と、名前・場所がぶつからないように）
inline constexpr std::string_view kProjectShaderNamePrefix = "Project/";


/// <summary>
/// .shaderを読む、見るクラス
/// </summary>
class ShaderPackageLoader {
public:
	// ===== Passkey Idiom =====
	class ConstructorKey {
	private:
		ConstructorKey() = default;
		friend class ShaderPackageLoader;
	};
	explicit ShaderPackageLoader(ConstructorKey) {} // 鍵を受け取るコンストラクタ


	static void Initialize();
	static void Release();

	/// <summary>
	/// エンジンの .shader を全部 読み込み→作成する（Shader/Generated に作成される）
	/// </summary>
	/// <param name="dataDir">FindDataDir の戻り値（エンジンの Shader/Data）</param>
	static void LoadAll(const std::filesystem::path& dataDir);

	/// <summary>
	/// ゲームの .shader を全部 読み込み→作成する（Shader/Generated/Project に作成される）。LoadAll の後に呼ぶ（エンジンの .hlsli を include できるように）
	/// <para>フォルダが無ければ何もしない</para>
	/// </summary>
	/// <param name="projectDir">ゲームのリソースのフォルダ（kProjectShaderDir）</param>
	static void LoadProject(const std::filesystem::path& projectDir);

	/// <summary>
	/// .shader を読むフォルダを決める
	/// <para>Release 以外：エンジンのフォルダ（HTTEngine.props の HTTENGINE_ROOT_DIR）を直接読む。.shader を直したら、次の起動で反映される</para>
	/// <para>Release・エンジンのフォルダが無い PC：exe の隣にコピーされた HTTEngine/Shader/Data</para>
	/// </summary>
	static std::filesystem::path FindDataDir();

	/// <summary>
	/// 名前からShaderのResource, Input, コンパイル結果を取得
	/// </summary>
	static const ShaderReflection& GetShaderReflection(const std::string& name);

	/// <summary>
	/// 描画設定からシェーダーキャッシュを取り出す
	/// </summary>
	static size_t GetProgramIndex(DrawCategory category, ShadingType type);

	/// <summary>
	/// PS の .shader のパスから描画プログラムの番号を探す（.material が Shader を引く。#PROGRAM の無い .shader・知らないパスなら無し）
	/// </summary>
	/// <param name="assetPath">.shader のパス（"HTTEngine/Shader/Data/〜"・"resources/〜"。区切りは / でも \ でもよい）</param>
	static std::optional<size_t> FindProgramByPath(std::string_view assetPath);

	// ===== アセットの番号（AssetType::Shader・.material の Shader）。番号＝描画プログラムの番号 + 1（0 は未選択）=====
	// パスから番号（#PROGRAM の無い .shader・知らないパスなら 0）
	static uint32_t FindProgramHandle(std::string_view assetPath);
	// 番号からパス（0・知らない番号なら空）
	static const std::string& GetProgramPath(uint32_t handle);

	/// <summary>
	/// シェーダーキャッシュからシェーダー情報を取り出す
	/// </summary>
	/// <param name="index">GetProgramIndexの戻り値</param>
	static const ProgramEntry& GetProgramAt(size_t index) { return instance_->programs_[index]; }

	/// <summary>
	/// 描画プログラムのマテリアルの値の並び（#PROPERTIES を gMaterial の中の場所に結んだ物）。初めて聞かれたときに作る
	/// <para>#PROPERTIES の名前が gMaterial に無い・型が合わないときは止める（.shader の書き間違い）</para>
	/// </summary>
	static const ShaderPropertyLayout& GetPropertyLayout(size_t index);

	// シェーダーのペア（vs, psなど）の総数
	static size_t GetProgramCount() { return instance_->programs_.size(); }


private:
	// 読むフォルダの違い（エンジンかゲームか）
	struct FolderOrigin {
		std::wstring generatedPrefix; // 作る .hlsl の場所の頭（エンジンは空、ゲームは "Project/"）
		std::string assetPrefix;      // .material が指すパスの頭（"HTTEngine/Shader/Data/"・"resources/"）
		std::string namePrefix;       // 名前の頭（エンジンは空、ゲームは "Project/"）
		bool keepFolderInName = false; // 名前にフォルダを入れるか（ゲームは入れる。エンジンは今までどおりファイル名だけ）
	};

	static void LoadFolder(const std::filesystem::path& dir, const FolderOrigin& origin); // フォルダの下の .shader を全部読む
	static bool Generate(const ShaderDefinition& def); // outRoot/path に書き出す。中身が同じならスキップ
	static ShaderDefinition ParseFile(const std::filesystem::path& path); // ファイルを開いて全部読んでParseText関数に送る
	static ShaderDefinition ParseText(std::string_view text, std::string_view sourceName); // 中身を1行づつ読んでShaderDefinitionに格納
	static void ApplyLocation(ShaderDefinition& def, const std::filesystem::path& relativePath, const FolderOrigin& origin); // 場所から .hlsl の場所・名前・パスを決める
	static uint32_t DrawKey(DrawCategory c, ShadingType s) { return (uint32_t(c) << 8) | uint32_t(s); } // 描画設定をキーにする
	static ShaderMeta ParseMeta(const std::vector<std::string>& metaLines, std::string_view src); // .shaderの#METAを解析
	static ProgramDef ParseProgram(const std::vector<std::string>& lines, std::string_view src);  // .shaderの#PROGRAM解析
	static std::vector<ShaderPropertyDef> ParseProperties(const std::vector<std::string>& lines, std::string_view src); // .shaderの#PROPERTIES解析
	static ShaderPropertyDef ParsePropertyLine(std::string_view line, std::string_view src); // #PROPERTIES の1行
	static ShaderPropertyLayout MakePropertyLayout(const ProgramEntry& program); // #PROPERTIES を gMaterial の中の場所に結ぶ
	static void Register(const ShaderDefinition& def);
	// 描画プログラムを1つ登録（同じ組み合わせは上書き）
	static void AddProgram(const ShaderDefinition& def);
	// 名前を変えた・動かした .shader：パスで引く表と、覚えているパスを付け替える（番号はそのまま。AssetMoveRegistry から呼ばれる）
	static void OnAssetMoved(const struct MovedPath& moved);
	static std::string_view Trim(std::string_view s);  // 空白を削除
	static std::string Require(const std::unordered_map<std::string, std::string>& kv, std::string_view key, std::string_view src); // 一致しているか
	static std::wstring DefaultProfile(ShaderMeta::Stage s); // ShaderのProfileのデフォルト設定
	static std::unordered_map<std::string, std::string> ToKeyValues(const std::vector<std::string>&, std::string_view);
	template<class E> static E ParseEnum(std::string_view s, std::string_view key, std::string_view src);

	// シングルトン
	friend struct std::default_delete<ShaderPackageLoader>;
	~ShaderPackageLoader() = default;
	ShaderPackageLoader(const ShaderPackageLoader&) = delete;
	ShaderPackageLoader& operator=(const ShaderPackageLoader&) = delete;
	static std::unique_ptr<ShaderPackageLoader> instance_;
	std::unordered_map<std::string, ShaderEntry> shadersByName_;
	std::deque<ProgramEntry> programs_;  // 描画の際に使うShaderファイルの組み合わせを保存する箱
	std::unordered_map<uint32_t, size_t> programIndexByCatShading_;
	std::unordered_map<std::string, size_t> programIndexByPath_;   // PS の .shader のパス（ToPathKey の形）→ 番号
	std::unordered_map<size_t, ShaderPropertyLayout> layouts_;     // 番号 → マテリアルの値の並び（初めて聞かれたときに作る）
};
