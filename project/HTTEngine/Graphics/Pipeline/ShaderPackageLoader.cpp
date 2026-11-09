#include "ShaderPackageLoader.h"

#include <charconv>
#include <format>
#include <fstream>
#include <unordered_map>

#include <externals/magic_enum/magic_enum.hpp>

#include "HTTEngine/Core/AssetMoveRegistry.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/String/ConvertString.h"

// 静的メンバ変数
std::unique_ptr<ShaderPackageLoader> ShaderPackageLoader::instance_ = nullptr;

namespace {
constexpr std::wstring_view kShaderExtension = L".shader";     // 読むファイルの拡張子
constexpr std::wstring_view kHlslExtension = L".hlsl";         // 作るファイルの拡張子（Include 以外）

// 種類の型と、定数バッファの変数の型が合うか（成分の数は components に入れる）
bool MatchPropertyType(ShaderPropertyType type, const ShaderConstantVariable& variable, uint32_t& components) {
	// 配列・行列は値1つとして扱えない
	if (variable.elements != 0 || variable.rows != 1) {
		return false;
	}
	components = variable.columns;
	const bool isFloat = (variable.type == D3D_SVT_FLOAT);
	const bool isInteger = (variable.type == D3D_SVT_UINT || variable.type == D3D_SVT_INT || variable.type == D3D_SVT_BOOL);
	switch (type) {
	case ShaderPropertyType::Float:
	case ShaderPropertyType::Range:
		return isFloat && components == 1;
	case ShaderPropertyType::Toggle:
		return isInteger && components == 1;
	case ShaderPropertyType::Vector:
		return isFloat && components >= 1 && components <= 4;
	case ShaderPropertyType::Color:
		return isFloat && (components == 3 || components == 4);
	case ShaderPropertyType::Texture:
		return (variable.type == D3D_SVT_UINT) && components == 1;
	}
	return false;
}
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
// ===== 初期化 =====
void ShaderPackageLoader::Initialize() {
	instance_ = std::make_unique<ShaderPackageLoader>(ConstructorKey());
	AssetMoveRegistry::AddListener(&OnAssetMoved);
	LogManager::Log("Initialized");
}

// ===== 名前を変えた・動かした .shader =====
void ShaderPackageLoader::OnAssetMoved(const MovedPath& moved) {
	if (instance_ == nullptr) {
		return;
	}
	moved.ApplyToKeys(instance_->programIndexByPath_);
	for (ProgramEntry& program : instance_->programs_) {
		moved.Apply(program.assetPath);
	}
	for (auto& [name, shader] : instance_->shadersByName_) {
		moved.Apply(shader.assetPath);
	}
}

// ===== 解放 =====
void ShaderPackageLoader::Release() {
	instance_.reset();
	LogManager::Log("Released");
}


//=============================================================================
// ゲッター
//=============================================================================
size_t ShaderPackageLoader::GetProgramIndex(DrawCategory c, ShadingType s) {
	auto it = instance_->programIndexByCatShading_.find(DrawKey(c, s));
	HTT_ASSERT_MSG(it != instance_->programIndexByCatShading_.end(), std::format("program ({}/{}) が未登録", magic_enum::enum_name(c), magic_enum::enum_name(s)));
	return it->second;
}

std::optional<size_t> ShaderPackageLoader::FindProgramByPath(std::string_view assetPath) {
	// 区切り（/ と \）・「./」の違いをそろえてから引く（登録も同じ ToPathKey の形）
	const auto it = instance_->programIndexByPath_.find(ToPathKey(ToPath(std::string(assetPath))));
	if (it == instance_->programIndexByPath_.end()) {
		return std::nullopt;
	}
	return it->second;
}

uint32_t ShaderPackageLoader::FindProgramHandle(std::string_view assetPath) {
	const std::optional<size_t> index = FindProgramByPath(assetPath);
	return index ? static_cast<uint32_t>(*index) + 1 : 0;
}

const std::string& ShaderPackageLoader::GetProgramPath(uint32_t handle) {
	static const std::string kEmpty; // 見つからないときに返す空文字（参照で返すので static にする）
	if (handle == 0 || handle > instance_->programs_.size()) {
		return kEmpty;
	}
	return instance_->programs_[handle - 1].assetPath;
}

const ShaderReflection& ShaderPackageLoader::GetShaderReflection(const std::string& name) {
	auto it = instance_->shadersByName_.find(name);
	HTT_ASSERT_MSG(it != instance_->shadersByName_.end(), std::format("shader '{}' が未登録です", name));
	const ShaderEntry& s = it->second;
	return ShaderCompiler::CompileShaderReflection(s.path, s.profile, s.entry);
}

const ShaderPropertyLayout& ShaderPackageLoader::GetPropertyLayout(size_t index) {
	auto& layouts = instance_->layouts_;
	if (auto it = layouts.find(index); it != layouts.end()) {
		return it->second;
	}
	return layouts.emplace(index, MakePropertyLayout(instance_->programs_[index])).first->second;
}


//=============================================================================
// .shader → .hlsl作成
//=============================================================================
// ===== エンジン =====
void ShaderPackageLoader::LoadAll(const std::filesystem::path& dataDir) {
	// .material が指すパスは、読む場所（エンジンのフォルダか exe の隣か）に関係なく "HTTEngine/Shader/Data/〜" にそろえる
	FolderOrigin origin;
	origin.assetPrefix = ToUtf8(std::filesystem::path(kShaderDataDir)) + "/";
	LoadFolder(dataDir, origin);
}

// ===== ゲーム =====
void ShaderPackageLoader::LoadProject(const std::filesystem::path& projectDir) {
	std::error_code error;
	if (!std::filesystem::is_directory(projectDir, error)) {
		return; // ゲームのリソースのフォルダが無い（エンジンだけで起動した）
	}
	FolderOrigin origin;
	origin.generatedPrefix = std::wstring(kProjectGeneratedSubDir) + L"/";
	origin.assetPrefix = ToUtf8(projectDir) + "/";
	origin.namePrefix = std::string(kProjectShaderNamePrefix);
	origin.keepFolderInName = true;
	LoadFolder(projectDir, origin);
}

// ===== フォルダの下の .shader を全部 =====
void ShaderPackageLoader::LoadFolder(const std::filesystem::path& dir, const FolderOrigin& origin) {
	std::vector<ShaderDefinition> defs;
	// 再帰的にフォルダを見て .shader だけ拾う
	// std::filesystem::recursive_directory_iterator は、指定したディレクトリ以下を再帰的にすべて探索する
	for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
		if (entry.is_regular_file() && entry.path().extension() == kShaderExtension) {
			ShaderDefinition def = ParseFile(entry.path());
			ApplyLocation(def, entry.path().lexically_relative(dir), origin);
			defs.push_back(std::move(def));
		}
	}
	for (auto& d : defs) {
		Generate(d); // hlsl作成
	}
	// PS以外（VS・CS・Include）を先に登録
	for (auto& d : defs) {
		if (d.meta.stage != ShaderMeta::Stage::PS) {
			Register(d);
		}
	}
	// PS
	for (auto& d : defs) {
		if (d.meta.stage == ShaderMeta::Stage::PS) {
			Register(d);
		}
	}
	LogManager::Log(std::format("[shader] {}: {} shaders / {} programs", ToUtf8(dir), instance_->shadersByName_.size(), instance_->programs_.size()));
}


//=============================================================================
// .shader を読むフォルダを決める
//=============================================================================
std::filesystem::path ShaderPackageLoader::FindDataDir() {
	std::filesystem::path dataDir = kShaderDataDir; // 見つからないときはexeの隣をコピー
#ifdef HTTENGINE_ROOT_DIR
	// Release以外: エンジンのフォルダの.shaderを直接読む
	const std::filesystem::path engineDataDir = std::filesystem::path(HTTENGINE_ROOT_DIR) / kShaderDataDir;
	std::error_code error;
	if (std::filesystem::is_directory(engineDataDir, error)) {
		dataDir = engineDataDir;
	}
#endif
	LogManager::Log(std::format("[shader] data: {}", ToUtf8(dataDir)));
	return dataDir;
}


//=============================================================================
// 場所から .hlsl の場所・名前・パスを決める
//=============================================================================
// "Model/Unlit.PS.shader"（エンジン）     → .hlsl "Model/Unlit.PS.hlsl"          名前 "UnlitPS"               パス "HTTEngine/Shader/Data/Model/Unlit.PS.shader"
// "shaders/Floor.PS.shader"（ゲーム）     → .hlsl "Project/shaders/Floor.PS.hlsl" 名前 "Project/shaders/FloorPS" パス "resources/shaders/Floor.PS.shader"
// "Buffers/Camera.hlsli.shader"（Include）→ .hlsl ではなく "Buffers/Camera.hlsli"（include で書く名前のまま）
void ShaderPackageLoader::ApplyLocation(ShaderDefinition& def, const std::filesystem::path& relativePath, const FolderOrigin& origin) {
	// --- 作る .hlsl の場所（.shader を外し、Include 以外は .hlsl を付ける）---
	std::filesystem::path generated = relativePath;
	generated.replace_extension(); // "Model/Unlit.PS"・"Buffers/Camera.hlsli"
	std::wstring generatedPath = origin.generatedPrefix + generated.generic_wstring();
	if (def.meta.stage != ShaderMeta::Stage::Include) {
		generatedPath += kHlslExtension;
	}
	def.meta.path = std::move(generatedPath);

	// --- 名前（ファイル名の「.」を外す。ゲームの物はフォルダも入れて、頭に Project/ を付ける）---
	std::string stem = ToUtf8(generated.filename()); // "Unlit.PS"
	std::erase(stem, '.');
	const std::string folder = origin.keepFolderInName && generated.has_parent_path() ? ToUtf8(generated.parent_path()) + "/" : std::string();
	def.name = origin.namePrefix + folder + stem;

	// --- .material が指すパス ---
	def.assetPath = ToPathKey(ToPath(origin.assetPrefix + ToUtf8(relativePath)));
}


//=============================================================================
// outRoot/path に書き出す。中身が前回と同じならスキップ
//=============================================================================
bool ShaderPackageLoader::Generate(const ShaderDefinition& def) {
	std::filesystem::path outPath = std::filesystem::path(kShaderGeneratedDir) / def.meta.path;
	// --- .hlsl と中身が同じならスキップ ---
	if (std::filesystem::exists(outPath)) {
		std::ifstream in(outPath, std::ios::binary);
		std::string prev((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		if (prev == def.hlslBody) {
			return false;
		}
	}
	// 生成
	if (outPath.has_parent_path()) {
		std::filesystem::create_directories(outPath.parent_path());
	}
	std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
	out << def.hlslBody;
	return true;
}


//=============================================================================
// 追加登録
//=============================================================================
void ShaderPackageLoader::Register(const ShaderDefinition& def) {
	// Includeはコンパイルしないので名前登録しない
	if (def.meta.stage != ShaderMeta::Stage::Include) {
		// 同じ名前が2つあると、後の方が前の方を隠してしまう（どちらが使われるか分からない）ので止める
		const auto existing = instance_->shadersByName_.find(def.name);
		HTT_ASSERT_MSG(existing == instance_->shadersByName_.end(),
		    std::format("Shader の名前 '{}' が2つあります（{} と {}）。ファイル名を変えてください", def.name, existing != instance_->shadersByName_.end() ? existing->second.assetPath : "", def.assetPath));
		instance_->shadersByName_.emplace(def.name, ShaderEntry{def.meta.stage, def.meta.path, def.meta.profile, def.meta.entry, def.assetPath});
		LogManager::Log(std::format("ShaderName: {}", def.name));
	}
	// #PROGRAM があれば描画プログラム
	if (def.program.has) {
		AddProgram(def);
	}
}

// ===== 描画プログラムを1つ登録 =====
void ShaderPackageLoader::AddProgram(const ShaderDefinition& def) {
	ProgramEntry p;
	p.category = def.program.category;
	p.shading = def.program.shading;
	p.vsName = def.program.vsName;
	p.psName = def.name;
	p.assetPath = def.assetPath;
	p.states = def.program.states;
	p.propertyDefs = def.properties;
	// 名前（PSO・GPU の計測に出す）：決まった描き方は "ModelUnlit"、パスで引く物は Shader の名前
	p.name = p.shading ? std::format("{}{}", magic_enum::enum_name(p.category), magic_enum::enum_name(*p.shading)) : def.name;

	// --- 番号を決める（同じ (cat,shading) は上書きして番号を変えない。新しい物は末尾＝今までの番号は動かない）---
	size_t index = instance_->programs_.size();
	if (p.shading) {
		const uint32_t key = DrawKey(p.category, *p.shading);
		if (auto e = instance_->programIndexByCatShading_.find(key); e != instance_->programIndexByCatShading_.end()) {
			index = e->second;
		}
		instance_->programIndexByCatShading_[key] = index;
	}
	if (index == instance_->programs_.size()) {
		instance_->programIndexByPath_[p.assetPath] = index;
		instance_->programs_.push_back(std::move(p));
	} else {
		// 上書きした物のパスでは、もう引けないようにする（同じ番号を2つのパスが指さないように）
		instance_->programIndexByPath_.erase(instance_->programs_[index].assetPath);
		instance_->programIndexByPath_[p.assetPath] = index;
		instance_->programs_[index] = std::move(p);
	}
}


//=============================================================================
// #PROPERTIES を gMaterial の中の場所に結ぶ
//=============================================================================
ShaderPropertyLayout ShaderPackageLoader::MakePropertyLayout(const ProgramEntry& program) {
	ShaderPropertyLayout layout;
	// --- gMaterial を探す（ふつうは PS。PS が読まず VS だけが読む Shader もあるので、無ければ VS）---
	const ShaderConstantBuffer* buffer = GetShaderReflection(program.psName).FindConstantBuffer(kMaterialConstantBufferName);
	if (buffer == nullptr) {
		buffer = GetShaderReflection(program.vsName).FindConstantBuffer(kMaterialConstantBufferName);
	}
	if (buffer == nullptr) {
		if (!program.propertyDefs.empty()) {
			LogManager::Warning(std::format("{}: #PROPERTIES がありますが、シェーダーが {} を読んでいないので、値は送りません", program.assetPath, kMaterialConstantBufferName));
		}
		return layout;
	}
	layout.bufferSize = buffer->size;

	// --- #PROPERTIES の1行ずつ、場所と成分の数を決める（名前が無い・型が合わないのは .shader の書き間違いなので止める）---
	for (const ShaderPropertyDef& def : program.propertyDefs) {
		const ShaderConstantVariable* variable = buffer->FindVariable(def.name);
		HTT_ASSERT_MSG(variable != nullptr, std::format("{}: #PROPERTIES の '{}' が {} の中にありません", program.assetPath, def.name, kMaterialConstantBufferName));
		uint32_t components = 1;
		HTT_ASSERT_MSG(MatchPropertyType(def.type, *variable, components),
		    std::format("{}: #PROPERTIES の '{}'（{}）と、{} の中の型が合いません（Float・Range は float、Toggle は uint、Vector は float〜float4、Color は float3・float4、Texture は uint）", program.assetPath, def.name,
		        magic_enum::enum_name(def.type), kMaterialConstantBufferName));
		layout.properties.push_back({def, variable->offset, components});
	}
	return layout;
}


//=============================================================================
// .shaderの内容を見る
//=============================================================================
ShaderDefinition ShaderPackageLoader::ParseFile(const std::filesystem::path& path) {
	// バイナリモードで開く
	std::ifstream ifs(path, std::ios::binary);
	HTT_ASSERT_MSG(ifs.is_open(), std::format(".shaderを開けません: {}", ToUtf8(path)));
	// ファイルを先頭から終端まで見る
	std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
	return ParseText(content, ToUtf8(path.filename()));
}


//=============================================================================
// .shaderの内容を解析
//=============================================================================
ShaderDefinition ShaderPackageLoader::ParseText(std::string_view text, std::string_view sourceName) {
	ShaderDefinition def;
	def.source = std::string(sourceName);
	std::vector<std::string> metaLines, programLines, propertyLines;
	std::string hlslBody;
	enum class Block { None, Meta, Program, Properties, Hlsl } block = Block::None;

	// --- 全文解析 ---
	size_t pos = 0;
	while (pos < text.size()) {
		// 空白無視
		size_t eol = text.find('\n', pos);
		std::string_view line = text.substr(pos, (eol == std::string_view::npos ? text.size() : eol) - pos);
		pos = (eol == std::string_view::npos) ? text.size() : eol + 1;
		if (!line.empty() && line.back() == '\r') {
			line.remove_suffix(1);
		}
		// #～を分類分け
		std::string_view t = Trim(line);
		if (t == "#META") {
			block = Block::Meta;
			continue;
		}
		if (t == "#META_END") {
			block = Block::None;
			continue;
		}
		if (t == "#PROGRAM") {
			block = Block::Program;
			continue;
		}
		if (t == "#PROGRAM_END") {
			block = Block::None;
			continue;
		}
		if (t == "#PROPERTIES") {
			block = Block::Properties;
			continue;
		}
		if (t == "#PROPERTIES_END") {
			block = Block::None;
			continue;
		}
		if (t == "#HLSL") {
			block = Block::Hlsl;
			continue;
		}
		if (t == "#HLSL_END") {
			block = Block::None;
			continue;
		}
		// 分類分け結果を1行づつ見る
		switch (block) {
		case Block::Meta:
			metaLines.emplace_back(line);
			break;
		case Block::Program:
			programLines.emplace_back(line);
			break;
		case Block::Properties:
			propertyLines.emplace_back(line);
			break;
		case Block::Hlsl:
			hlslBody += line;
			hlslBody += '\n';
			break;
		default:
			break;
		}
	}
	def.meta = ParseMeta(metaLines, sourceName);
	def.program = ParseProgram(programLines, sourceName); // 空なら has=false
	def.properties = ParseProperties(propertyLines, sourceName);
	def.hlslBody = std::move(hlslBody);
	return def;
}


//=============================================================================
// .shaderの #～ を解析
//=============================================================================
// ===== #META =====
ShaderMeta ShaderPackageLoader::ParseMeta(const std::vector<std::string>& lines, std::string_view src) {
	auto kv = ToKeyValues(lines, src);
	// 作る .hlsl の場所は .shader の場所から決める（2か所に書くと、片方だけ直したときにずれる）
	HTT_ASSERT_MSG(!kv.contains("path"), std::format("{}: #META の path: はもう使いません。行を消してください（.hlsl の場所は .shader の場所から決めます）", src));
	ShaderMeta m;
	m.stage = ParseEnum<ShaderMeta::Stage>(Require(kv, "stage", src), "stage", src);

	auto e = kv.find("entry");
	if (e != kv.end()) {
		m.entry = ConvertString(e->second);
	}

	if (m.stage != ShaderMeta::Stage::Include) {
		m.profile = (kv.contains("profile")) ? ConvertString(kv.at("profile")) : DefaultProfile(m.stage);
	}

	return m;
}

// ===== #PROGRAM =====
ProgramDef ShaderPackageLoader::ParseProgram(const std::vector<std::string>& lines, std::string_view src) {
	ProgramDef p;
	// #PROGRAM 無し
	if (lines.empty()) {
		return p;
	}

	auto kv = ToKeyValues(lines, src);
	p.has = true;
	p.category = ParseEnum<DrawCategory>(Require(kv, "category", src), "category", src);
	p.vsName = Require(kv, "vs", src);
	if (auto s = kv.find("shading"); s != kv.end()) {
		p.shading = ParseEnum<ShadingType>(s->second, "shading", src);
	}
	// --- 描くときの状態の既定（書かなければ Normal・SolidBack・TestWrite）---
	if (auto b = kv.find("blend"); b != kv.end()) {
		p.states.blend = ParseEnum<BlendMode>(b->second, "blend", src);
	}
	if (auto r = kv.find("rasterizer"); r != kv.end()) {
		p.states.rasterizer = ParseEnum<RasterizerType>(r->second, "rasterizer", src);
	}
	if (auto d = kv.find("depth"); d != kv.end()) {
		p.states.depth = ParseEnum<DepthMode>(d->second, "depth", src);
	}
	return p;
}

// ===== #PROPERTIES =====
std::vector<ShaderPropertyDef> ShaderPackageLoader::ParseProperties(const std::vector<std::string>& lines, std::string_view src) {
	std::vector<ShaderPropertyDef> defs;
	for (const std::string& raw : lines) {
		const std::string_view line = Trim(raw);
		if (line.empty() || line.starts_with("//")) {
			continue;
		}
		ShaderPropertyDef def = ParsePropertyLine(line, src);
		const bool isDuplicate = std::ranges::any_of(defs, [&def](const ShaderPropertyDef& other) { return other.name == def.name; });
		HTT_ASSERT_MSG(!isDuplicate, std::format("{}: #PROPERTIES の '{}' が2つあります", src, def.name));
		defs.push_back(std::move(def));
	}
	return defs;
}

// ===== #PROPERTIES の1行 =====
// 変数名: 種類 "表示名" "説明（省略できる）" 値
//   fillColor:  Color   "Fill Color" "塗りの色" 1, 0.2, 0.1, 0.4   … Color は 3つか4つ（sRGB）
//   edgeWidth:  Range   "Edge Width" 0.15, 0, 1                    … Range は 既定・下限・上限
//   pulseSpeed: Float   "Pulse Speed" 2                            … Float は1つ（書かなければ 0）
//   useTexture: Toggle  "Use Texture" 1                            … Toggle は 0 か 1
//   offset:     Vector  "Offset" 0, 0                              … Vector は 1〜4 個
//   mainTex:    Texture "Texture"                                  … Texture は値を書かない（選ばなければ白）
ShaderPropertyDef ShaderPackageLoader::ParsePropertyLine(std::string_view line, std::string_view src) {
	ShaderPropertyDef def;
	// --- 変数名 ---
	const size_t colon = line.find(':');
	HTT_ASSERT_MSG(colon != std::string_view::npos, std::format("{}: #PROPERTIES に ':' がありません -> '{}'", src, std::string(line)));
	def.name = std::string(Trim(line.substr(0, colon)));
	std::string_view rest = Trim(line.substr(colon + 1));

	// --- 種類（次の空白まで）---
	const size_t typeEnd = rest.find_first_of(" \t");
	def.type = ParseEnum<ShaderPropertyType>(rest.substr(0, typeEnd), "#PROPERTIES の種類", src);
	rest = (typeEnd == std::string_view::npos) ? std::string_view() : Trim(rest.substr(typeEnd));

	// --- "表示名" と "説明"（" で囲んだ物。説明は無くてもよい）---
	const auto takeQuoted = [&rest, src](std::string& out) {
		if (!rest.starts_with('"')) {
			return false;
		}
		const size_t close = rest.find('"', 1);
		HTT_ASSERT_MSG(close != std::string_view::npos, std::format("{}: #PROPERTIES の \" が閉じていません", src));
		out = std::string(rest.substr(1, close - 1));
		rest = Trim(rest.substr(close + 1));
		return true;
	};
	HTT_ASSERT_MSG(takeQuoted(def.displayName), std::format("{}: #PROPERTIES の '{}' に \"表示名\" がありません", src, def.name));
	takeQuoted(def.tooltip);

	// --- 値（, か空白で区切った数）---
	std::vector<float> numbers;
	size_t pos = 0;
	while (pos < rest.size()) {
		const size_t begin = rest.find_first_not_of(", \t", pos);
		if (begin == std::string_view::npos) {
			break;
		}
		const size_t end = rest.find_first_of(", \t", begin);
		const std::string_view token = rest.substr(begin, end == std::string_view::npos ? std::string_view::npos : end - begin);
		float value = 0.0f;
		// std::from_chars：文字列から数を読む（例外を投げず、ロケールにも左右されない）。読めた所までのポインタと、エラーを返す
		const auto [last, result] = std::from_chars(token.data(), token.data() + token.size(), value);
		HTT_ASSERT_MSG(result == std::errc() && last == token.data() + token.size(), std::format("{}: #PROPERTIES の '{}' の値 '{}' が数ではありません", src, def.name, std::string(token)));
		numbers.push_back(value);
		pos = (end == std::string_view::npos) ? rest.size() : end;
	}

	// --- 種類ごとの値の数 ---
	const auto require = [&](bool isValid, std::string_view expected) {
		HTT_ASSERT_MSG(isValid, std::format("{}: #PROPERTIES の '{}'（{}）の値は {} にしてください", src, def.name, magic_enum::enum_name(def.type), expected));
	};
	switch (def.type) {
	case ShaderPropertyType::Float:
		require(numbers.size() <= 1, "1つ（書かなければ 0）");
		def.defaultValue.x = numbers.empty() ? 0.0f : numbers[0];
		break;
	case ShaderPropertyType::Range:
		require(numbers.size() == 3 && numbers[1] < numbers[2], "既定・下限・上限の3つ（下限 < 上限）");
		def.defaultValue.x = numbers[0];
		def.min = numbers[1];
		def.max = numbers[2];
		break;
	case ShaderPropertyType::Toggle:
		require(numbers.size() <= 1, "0 か 1");
		def.defaultValue.x = (!numbers.empty() && numbers[0] != 0.0f) ? 1.0f : 0.0f;
		break;
	case ShaderPropertyType::Vector:
		require(numbers.size() <= 4, "1〜4 個");
		for (size_t i = 0; i < numbers.size(); ++i) {
			(&def.defaultValue.x)[i] = numbers[i]; // Vector4 は x・y・z・w が並んでいるので、番号で書ける
		}
		break;
	case ShaderPropertyType::Color:
		require(numbers.empty() || numbers.size() == 3 || numbers.size() == 4, "3つか4つ（書かなければ白）");
		def.defaultValue = {1.0f, 1.0f, 1.0f, 1.0f};
		for (size_t i = 0; i < numbers.size(); ++i) {
			(&def.defaultValue.x)[i] = numbers[i];
		}
		break;
	case ShaderPropertyType::Texture:
		require(numbers.empty(), "書かない（選ばなければ白）");
		break;
	}
	return def;
}


//=============================================================================
// ヘルパー
//=============================================================================
std::string_view ShaderPackageLoader::Trim(std::string_view s) {
	constexpr const char* ws = " \t\r\n";
	size_t b = s.find_first_not_of(ws);
	if (b == std::string_view::npos) {
		return {};
	}
	return s.substr(b, s.find_last_not_of(ws) - b + 1);
}

std::string ShaderPackageLoader::Require(const std::unordered_map<std::string, std::string>& kv, std::string_view key, std::string_view src) {
	auto it = kv.find(std::string(key));
	HTT_ASSERT_MSG(it != kv.end(), std::format("{}: 必須フィールド '{}' がありません", src, std::string(key)));
	return it->second;
}

std::wstring ShaderPackageLoader::DefaultProfile(ShaderMeta::Stage s) {
	switch (s) {
	case ShaderMeta::Stage::VS:
		return L"vs_6_0";
	case ShaderMeta::Stage::PS:
		return L"ps_6_0";
	case ShaderMeta::Stage::CS:
	default:
		return L""; // Include はコンパイルしない
	}
}

template<class E> E ShaderPackageLoader::ParseEnum(std::string_view s, std::string_view key, std::string_view src) {
	auto e = magic_enum::enum_cast<E>(s);
	HTT_ASSERT_MSG(e.has_value(), std::format("{}: {}='{}' は無効な値です", src, std::string(key), std::string(s)));
	return *e;
}

std::unordered_map<std::string, std::string> ShaderPackageLoader::ToKeyValues(const std::vector<std::string>& lines, std::string_view src) {
	std::unordered_map<std::string, std::string> kv;
	for (const auto& raw : lines) {
		std::string_view line = Trim(raw);
		if (line.empty() || line.starts_with("//")) {
			continue;
		}

		size_t colon = line.find(':');
		HTT_ASSERT_MSG(colon != std::string_view::npos, std::format("{}: ':' がありません -> '{}'", src, std::string(line)));
		kv[std::string(Trim(line.substr(0, colon)))] = std::string(Trim(line.substr(colon + 1)));
	}
	return kv;
}
