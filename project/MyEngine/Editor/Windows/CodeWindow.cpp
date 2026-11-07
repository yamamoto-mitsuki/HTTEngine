#ifdef USE_IMGUI
#include "CodeWindow.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <Windows.h>
#include <shellapi.h>

#include <externals/imgui/imgui.h>

#include "MyEngine/Core/AssetMoveRegistry.h"
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Editor/Widgets/EditorWidgets.h"
#include "MyEngine/String/ConvertString.h"

namespace {
constexpr float kFontSize = 15.0f;                                       // コードの文字の大きさ（ピクセル。英数字の背の高さが、ふだんの文字（メイリオ 18）とだいたい同じになる）
constexpr float kLineSpacing = 1.3f;                                     // 1行の高さ（文字の高さの何倍か。詰めると、日本語の上の端が前の行にくっつく）
constexpr float kCodeCharWidth = 0.55f;                                  // Consolas の1文字の幅（文字の大きさの何倍か。フォントのファイルに書いてある 1126 / 2048）
constexpr const char* kCodeFontPath = "C:\\Windows\\Fonts\\consola.ttf"; // 等幅のフォント（文字の幅が全部同じなので、字下げがそろう）
constexpr double kReloadCheckSeconds = 1.0;                              // ファイルが書き換わったかを見る間隔（秒）
constexpr int kTabWidth = 4;                                             // タブを空白いくつにするか（Visual Studio と同じ）

/// <summary>
/// 日本語（コメント）用のフォントの候補。Consolas に無い文字だけ、ここから出す
/// </summary>
struct JapaneseFont {
	const char* path; // ファイル
	float sizeScale;  // Consolas と字の大きさをそろえるために、渡す大きさを何倍にするか
};

// 上から順に探して、最初にあった物を使う
constexpr std::array<JapaneseFont, 3> kJapaneseFonts = {
    {
     {"C:\\Windows\\Fonts\\BIZ-UDGothicR.ttc", 1.0f}, // BIZ UDゴシック
        {"C:\\Windows\\Fonts\\msgothic.ttc", 1.0f},   // MS ゴシック
        {"C:\\Windows\\Fonts\\meiryo.ttc", 1.5f},     // メイリオ
    }
};

// 読めるファイルの拡張子
constexpr auto kCodeExtensions = std::to_array<std::string_view>({".cpp", ".h", ".hpp", ".c", ".inl", ".hlsl", ".hlsli", ".shader", ".json", ".txt", ".md", ".ini", ".bat"});

// ===== 色（Visual Studio の暗いテーマに近い色）=====
constexpr ImU32 kTextColor = IM_COL32(220, 220, 220, 255);         // ふつうの文字
constexpr ImU32 kKeywordColor = IM_COL32(86, 156, 214, 255);       // キーワード（class・if・float など）
constexpr ImU32 kCommentColor = IM_COL32(87, 166, 74, 255);        // コメント
constexpr ImU32 kStringColor = IM_COL32(214, 157, 133, 255);       // 文字列 "…"
constexpr ImU32 kNumberColor = IM_COL32(181, 206, 168, 255);       // 数
constexpr ImU32 kPreprocessorColor = IM_COL32(155, 155, 155, 255); // #include・#define など
constexpr ImU32 kLineNumberColor = IM_COL32(110, 118, 129, 255);   // 行番号
constexpr ImU32 kFoundLineColor = IM_COL32(90, 70, 20, 255);       // 検索で見つけた行の帯

/// <summary>
/// 色の種類
/// </summary>
enum class TokenKind {
	Text,         // ふつうの文字・記号
	Keyword,      // キーワード
	Comment,      // コメント
	String,       // 文字列・文字
	Number,       // 数
	Preprocessor, // # で始まる命令
};

/// <summary>
/// 1行の中の、同じ色で描くひとかたまり（行の中の何文字目から、何文字か）
/// </summary>
struct Token {
	size_t begin = 0;
	size_t length = 0;
	TokenKind kind = TokenKind::Text;
};

/// <summary>
/// 1行分
/// </summary>
struct Line {
	std::string text;             // 中身（タブは空白にしてある）
	bool startsInComment = false; // 行の頭が /* … */ の途中か（前の行から続いている）
};

/// <summary>
/// 開いているファイル1つ（タブ1つ）
/// <para>OpenFile という名前は windows.h の関数とぶつかるので使わない</para>
/// </summary>
struct CodeFile {
	std::filesystem::path path;                // ファイルの場所
	std::string title;                         // タブに出す名前（ファイルの名前）
	std::vector<Line> lines;                   // 中身
	std::filesystem::file_time_type writeTime; // 読んだときの更新時刻（変わったら読み直す）
	float maxLineWidth = -1.0f;                // 一番長い行の幅（横のスクロールに使う。コードのフォントで測るので、描くときに測る。-1 はまだ）
	int scrollToLine = 0;                      // 次に描くとき、この行（1から）まで送る（0なら送らない）
	int foundLine = 0;                         // 検索で見つけた行（1から。0なら無し）
	bool selectRequest = false;                // 次に描くとき、このタブを前に出す
	bool closeRequest = false;                 // タブの × を押した
};

ImFont* codeFont = nullptr;      // コード用のフォント（無ければ nullptr＝ふだんのフォント）
std::vector<CodeFile> files;     // 開いているファイル（タブの並び）
std::string findText;            // 検索の文字
bool windowFocusRequest = false; // 次のフレームで Code ウィンドウを前に出す
bool findFocusRequest = false;   // 次のフレームで検索欄に文字を打てるようにする（Ctrl+F）
double lastReloadCheck = 0.0;    // 最後に更新時刻を見た時刻（ImGui::GetTime）

//=============================================================================
// 文字の見分け（std::isalpha は日本語の文字（0x80 より大きい値）を渡すと動きが決まっていないので、自分で書く）
//=============================================================================
bool IsWordStart(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool IsDigit(char c) { return c >= '0' && c <= '9'; }
bool IsWordChar(char c) { return IsWordStart(c) || IsDigit(c); }

// 色を付けるキーワード（C++ と HLSL のよく出る物）
const std::unordered_set<std::string_view>& Keywords() {
	static const std::unordered_set<std::string_view> keywords = {
	    "alignas",    "alignof",  "auto",     "bool",      "break",   "case",     "catch",     "char",         "class",    "concept",  "const",     "consteval",     "constexpr",   "constinit",
	    "const_cast", "continue", "decltype", "default",   "delete",  "do",       "double",    "dynamic_cast", "else",     "enum",     "explicit",  "export",        "extern",      "false",
	    "final",      "float",    "for",      "friend",    "goto",    "if",       "inline",    "int",          "long",     "mutable",  "namespace", "new",           "noexcept",    "nullptr",
	    "operator",   "override", "private",  "protected", "public",  "requires", "return",    "short",        "signed",   "sizeof",   "static",    "static_assert", "static_cast", "reinterpret_cast",
	    "struct",     "switch",   "template", "this",      "throw",   "true",     "try",       "typedef",      "typeid",   "typename", "union",     "unsigned",      "using",       "virtual",
	    "void",       "volatile", "while",    "int8_t",    "int16_t", "int32_t",  "int64_t",   "uint8_t",      "uint16_t", "uint32_t", "uint64_t",  "size_t",        "float2",      "float3",
	    "float4",     "float4x4", "float3x3", "uint",      "cbuffer", "struct",   "Texture2D", "SamplerState", "in",       "out",      "inout"};
	return keywords;
}

// その場所から、色の付くかたまり（文字列・数・言葉・コメント）が始まるか
bool StartsToken(std::string_view line, size_t i) {
	const char c = line[i];
	const char next = (i + 1 < line.size()) ? line[i + 1] : '\0';
	return c == '"' || c == '\'' || IsWordStart(c) || IsDigit(c) || (c == '/' && (next == '/' || next == '*'));
}

/// <summary>
/// 1行を、色ごとのかたまりに分ける
/// </summary>
/// <param name="inBlockComment">行の頭で /* … */ の途中か。行の終わりで、次の行の頭の状態に書き換わる</param>
/// <param name="out">かたまりを足す先（nullptr なら足さない。次の行へ持ち越す状態だけ調べたいとき）</param>
void Tokenize(std::string_view line, bool& inBlockComment, std::vector<Token>* out) {
	const auto push = [out](size_t begin, size_t end, TokenKind kind) {
		if (out != nullptr && end > begin) {
			out->push_back({begin, end - begin, kind});
		}
	};
	const size_t firstChar = line.find_first_not_of(' '); // 行の最初の文字（# が来たらプリプロセッサの命令）
	size_t i = 0;
	while (i < line.size()) {
		const char c = line[i];
		const char next = (i + 1 < line.size()) ? line[i + 1] : '\0';
		// --- /* … */（前の行から続いているときも）---
		if (inBlockComment || (c == '/' && next == '*')) {
			const size_t searchFrom = inBlockComment ? i : i + 2;
			const size_t end = line.find("*/", searchFrom);
			if (end == std::string_view::npos) {
				push(i, line.size(), TokenKind::Comment);
				inBlockComment = true; // 次の行へ続く
				return;
			}
			push(i, end + 2, TokenKind::Comment);
			i = end + 2;
			inBlockComment = false;
			continue;
		}
		// --- // から行の終わりまで ---
		if (c == '/' && next == '/') {
			push(i, line.size(), TokenKind::Comment);
			return;
		}
		// --- 文字列 "…" と文字 '…'（\" は終わりではない）---
		if (c == '"' || c == '\'') {
			size_t end = i + 1;
			while (end < line.size() && line[end] != c) {
				end += (line[end] == '\\') ? 2 : 1; // \ の次の1文字は飛ばす
			}
			end = (std::min)(end + 1, line.size()); // 閉じる " も含める
			push(i, end, TokenKind::String);
			i = end;
			continue;
		}
		// --- 行の最初の # から続く言葉（#include・#define など）---
		if (c == '#' && i == firstChar) {
			size_t end = i + 1;
			while (end < line.size() && IsWordChar(line[end])) {
				++end;
			}
			push(i, end, TokenKind::Preprocessor);
			i = end;
			continue;
		}
		// --- 数（0x1F・1.5f・1'000 など。言葉の途中の数字は、下の「言葉」に入る）---
		if (IsDigit(c)) {
			size_t end = i + 1;
			while (end < line.size() && (IsWordChar(line[end]) || line[end] == '.' || line[end] == '\'')) {
				++end;
			}
			push(i, end, TokenKind::Number);
			i = end;
			continue;
		}
		// --- 言葉（キーワードなら色を付ける）---
		if (IsWordStart(c)) {
			size_t end = i + 1;
			while (end < line.size() && IsWordChar(line[end])) {
				++end;
			}
			push(i, end, Keywords().contains(line.substr(i, end - i)) ? TokenKind::Keyword : TokenKind::Text);
			i = end;
			continue;
		}
		// --- それ以外（記号・空白・日本語）：次に色の付くかたまりが始まる所まで、まとめる ---
		size_t end = i + 1;
		while (end < line.size() && !StartsToken(line, end)) {
			++end;
		}
		push(i, end, TokenKind::Text);
		i = end;
	}
}

// 色の種類 → 色
ImU32 ColorOf(TokenKind kind) {
	switch (kind) {
	case TokenKind::Keyword:
		return kKeywordColor;
	case TokenKind::Comment:
		return kCommentColor;
	case TokenKind::String:
		return kStringColor;
	case TokenKind::Number:
		return kNumberColor;
	case TokenKind::Preprocessor:
		return kPreprocessorColor;
	case TokenKind::Text:
		break;
	}
	return kTextColor;
}


//=============================================================================
// ファイルを読む
//=============================================================================
// タブを、次の4の倍数の列まで空白にする（列は ASCII を1つ、日本語を2つと数える。等幅のフォントで日本語は2文字分の幅なので）
std::string ExpandTabs(std::string_view text) {
	std::string result;
	result.reserve(text.size());
	int column = 0;
	for (const char c : text) {
		if (c == '\t') {
			const int spaces = kTabWidth - column % kTabWidth;
			result.append(static_cast<size_t>(spaces), ' ');
			column += spaces;
			continue;
		}
		result.push_back(c);
		const unsigned char byte = static_cast<unsigned char>(c);
		if (byte < 0x80) {
			++column; // ASCII
		} else if (byte >= 0xC0) {
			column += 2; // 日本語などの1文字目（UTF-8 は1文字が2〜4バイト。2バイト目からは 0x80〜0xBF なので数えない）
		}
	}
	return result;
}

// ファイルを読み直す。読めなければ false
bool LoadFile(CodeFile& file) {
	std::ifstream stream(file.path, std::ios::binary);
	if (!stream) {
		return false;
	}
	std::ostringstream buffer;
	buffer << stream.rdbuf(); // ファイルの中身を全部読む
	std::string text = buffer.str();
	// UTF-8 の BOM（ファイルの頭の3バイトの印。Visual Studio が付けることがある）を取る
	if (text.starts_with("\xEF\xBB\xBF")) {
		text.erase(0, 3);
	}

	file.lines.clear();
	bool inBlockComment = false;
	size_t start = 0;
	while (true) {
		const size_t newline = text.find('\n', start);
		const size_t end = (newline == std::string::npos) ? text.size() : newline;
		std::string_view raw(text.data() + start, end - start);
		if (raw.ends_with('\r')) {
			raw.remove_suffix(1); // Windows の改行（\r\n）の \r を取る
		}
		Line line;
		line.text = ExpandTabs(raw);
		line.startsInComment = inBlockComment;
		Tokenize(line.text, inBlockComment, nullptr); // 次の行の頭が /* … */ の途中かだけ調べる
		file.lines.push_back(std::move(line));
		if (newline == std::string::npos) {
			break;
		}
		start = newline + 1;
	}
	std::error_code error;
	file.writeTime = std::filesystem::last_write_time(file.path, error);
	file.maxLineWidth = -1.0f; // 幅は描くときに測り直す
	return true;
}

// 書き換わったファイルを読み直す（1秒ごと。Visual Studio で保存したら、すぐに出る）
void ReloadChangedFiles() {
	if (ImGui::GetTime() - lastReloadCheck < kReloadCheckSeconds) {
		return;
	}
	lastReloadCheck = ImGui::GetTime();
	for (CodeFile& file : files) {
		std::error_code error;
		const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(file.path, error);
		if (!error && writeTime != file.writeTime) {
			LoadFile(file);
		}
	}
}


//=============================================================================
// 検索
//=============================================================================
// 大文字・小文字を区別しないで、text の中に word があるか
bool ContainsIgnoreCase(std::string_view text, std::string_view word) {
	const auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
	const auto found = std::search(text.begin(), text.end(), word.begin(), word.end(), [&lower](char a, char b) { return lower(a) == lower(b); });
	return found != text.end();
}

// 今見つけている行の次から探して、見つけた行へ送る（最後まで行ったら、頭に戻って探す）
void FindNext(CodeFile& file) {
	if (findText.empty() || file.lines.empty()) {
		return;
	}
	const size_t count = file.lines.size();
	const size_t start = static_cast<size_t>(file.foundLine); // foundLine は1から。0なら頭から
	for (size_t offset = 0; offset < count; ++offset) {
		const size_t index = (start + offset) % count;
		if (ContainsIgnoreCase(file.lines[index].text, findText)) {
			file.foundLine = static_cast<int>(index) + 1;
			file.scrollToLine = file.foundLine;
			return;
		}
	}
	file.foundLine = 0; // 見つからない
}


//=============================================================================
// 描く
//=============================================================================
// 上の帯（ファイルの場所・検索・ボタン）
void DrawToolbar(CodeFile& file) {
	// --- 検索（Enter で次を探す）---
	if (findFocusRequest) {
		ImGui::SetKeyboardFocusHere();
		findFocusRequest = false;
	}
	ImGui::SetNextItemWidth(220.0f);
	if (EditorWidgets::InputText("##find", findText, ImGuiInputTextFlags_EnterReturnsTrue)) {
		FindNext(file);
		findFocusRequest = true; // Enter で欄から抜けるので、続けて Enter を押せるように戻す
	}
	ImGui::SetItemTooltip("検索（Ctrl+F）。Enter で次を探す。大文字・小文字は区別しない");
	ImGui::SameLine();
	if (ImGui::Button("Next")) {
		FindNext(file);
	}

	// --- 対の .h / .cpp を開く ---
	const std::string extension = file.path.extension().string();
	std::filesystem::path pair = file.path;
	if (extension == ".cpp") {
		pair.replace_extension(".h");
	} else if (extension == ".h") {
		pair.replace_extension(".cpp");
	}
	std::error_code error;
	if (pair != file.path && std::filesystem::exists(pair, error)) {
		ImGui::SameLine();
		const std::string label = std::format("Open {}", pair.extension().string());
		if (ImGui::Button(label.c_str())) {
			CodeWindow::Open(pair);
		}
	}

	// --- Visual Studio で開く（書き換えはそちらで）---
	ImGui::SameLine();
	if (ImGui::Button("Open in Visual Studio")) {
		CodeWindow::OpenExternal(file.path);
	}
	ImGui::SameLine();
	if (ImGui::Button("Copy All")) {
		std::string all;
		for (const Line& line : file.lines) {
			all += line.text;
			all += '\n';
		}
		ImGui::SetClipboardText(all.c_str());
	}
	ImGui::TextDisabled("%s", ToUtf8(file.path).c_str());
}

// ===== 中身（行番号と、色分けした行）。見えている行だけ描く（ImGuiListClipper） =====
void DrawCode(CodeFile& file) {
	ImGui::PushFont(codeFont, (codeFont != nullptr) ? kFontSize : 0.0f);
	const float textHeight = ImGui::GetTextLineHeight();
	const float lineHeight = std::floor(textHeight * kLineSpacing);         // 1行の高さ（文字の上下に少し間を空ける）
	const float textOffsetY = std::floor((lineHeight - textHeight) * 0.5f); // 文字を行の真ん中に置く
	const float charWidth = ImGui::CalcTextSize("0").x;
	const std::string lastNumber = std::to_string(file.lines.size());
	const float numberWidth = charWidth * static_cast<float>(lastNumber.size() + 2); // 行番号の列の幅（一番大きい番号＋空白2つ）

	// 一番長い行の幅（横のスクロールの長さ。読み直した後に1回だけ測る）
	if (file.maxLineWidth < 0.0f) {
		file.maxLineWidth = 0.0f;
		for (const Line& line : file.lines) {
			file.maxLineWidth = (std::max)(file.maxLineWidth, ImGui::CalcTextSize(line.text.c_str()).x);
		}
	}
	const float contentWidth = numberWidth + file.maxLineWidth + charWidth * 2.0f;
	ImGui::SetNextWindowContentSize(ImVec2(contentWidth, 0.0f));
	if (ImGui::BeginChild("##code", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
		// --- 頼まれた行へ送る（その行が、見えている所の上から3割くらいに来るように）---
		if (file.scrollToLine > 0) {
			ImGui::SetScrollY((std::max)(0.0f, static_cast<float>(file.scrollToLine - 1) * lineHeight - ImGui::GetWindowHeight() * 0.3f));
			file.scrollToLine = 0;
		}
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		static std::vector<Token> tokens;                                                              // 1行分のかたまり（毎フレーム中身だけ入れ直す。メモリは使い回す）
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f)); // 行と行の間を空けない
		// ImGuiListClipper：たくさんの同じ高さの行のうち、見えている行の番号だけを教えてくれる（何千行あっても軽い）
		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(file.lines.size()), lineHeight);
		while (clipper.Step()) {
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
				const Line& line = file.lines[static_cast<size_t>(row)];
				const ImVec2 position = ImGui::GetCursorScreenPos();
				const float textY = position.y + textOffsetY; // 文字を描く高さ
				// 検索で見つけた行は、帯を敷く
				if (row + 1 == file.foundLine) {
					drawList->AddRectFilled(position, ImVec2(position.x + contentWidth, position.y + lineHeight), kFoundLineColor);
				}
				// 行番号（右寄せ）
				const std::string number = std::to_string(row + 1);
				drawList->AddText(ImVec2(position.x + numberWidth - charWidth * static_cast<float>(number.size() + 1), textY), kLineNumberColor, number.c_str());
				// 中身（色ごとのかたまりを、左から並べて描く）
				bool inBlockComment = line.startsInComment;
				tokens.clear();
				Tokenize(line.text, inBlockComment, &tokens);
				float x = position.x + numberWidth;
				for (const Token& token : tokens) {
					const char* begin = line.text.data() + token.begin;
					const char* end = begin + token.length;
					drawList->AddText(ImVec2(x, textY), ColorOf(token.kind), begin, end);
					x += ImGui::CalcTextSize(begin, end).x;
				}
				// 行の場所を取る（右クリックで、その行を写せるように、見えないボタンにする）
				ImGui::PushID(row);
				ImGui::InvisibleButton("##line", ImVec2(contentWidth, lineHeight));
				if (ImGui::BeginPopupContextItem("lineMenu")) {
					ImGui::TextDisabled("%d 行目", row + 1);
					if (ImGui::MenuItem("Copy Line")) {
						ImGui::SetClipboardText(line.text.c_str());
					}
					ImGui::EndPopup();
				}
				ImGui::PopID();
			}
		}
		ImGui::PopStyleVar();
	}
	ImGui::EndChild();
	ImGui::PopFont();
}
} // namespace


//=============================================================================
// 初期化（コード用のフォント）
//=============================================================================
void CodeWindow::Initialize() {
	// 開いているファイル（.shader など）の名前を変えたら、タブを新しいパスにする（前のパスのままだと、読み直せずに中身が消える）
	AssetMoveRegistry::AddListener([](const MovedPath& moved) {
		for (CodeFile& file : files) {
			if (moved.Apply(file.path)) {
				file.title = ToUtf8(file.path.filename());
			}
		}
	});
	ImGuiIO& io = ImGui::GetIO();
	std::error_code error;
	if (!std::filesystem::exists(kCodeFontPath, error)) {
		LogManager::Warning(std::format("等幅のフォントが無いので、コードはふだんのフォントで出します: {}", kCodeFontPath));
		return;
	}
	codeFont = io.Fonts->AddFontFromFileTTF(kCodeFontPath, kFontSize);
	if (codeFont == nullptr) {
		return;
	}
	// 日本語は Consolas に無いので、日本語のフォントを「無い文字の分だけ」混ぜる（MergeMode：前に足したフォントに混ぜる）
	const auto found = std::ranges::find_if(kJapaneseFonts, [&error](const JapaneseFont& font) { return std::filesystem::exists(font.path, error); });
	if (found == kJapaneseFonts.end()) {
		LogManager::Warning("日本語のフォントが見つからないので、コードの中の日本語は出ません");
		return;
	}
	ImFontConfig config;
	config.MergeMode = true;
	// 日本語の1文字の幅を、ちょうど英数字2文字分にする（字は幅の真ん中に置かれる）。日本語の後ろに続く文字も、列がそろう
	config.GlyphMinAdvanceX = kFontSize * kCodeCharWidth * 2.0f;
	config.GlyphMaxAdvanceX = config.GlyphMinAdvanceX;
	io.Fonts->AddFontFromFileTTF(found->path, kFontSize * found->sizeScale, &config);
}

//=============================================================================
// 描画
//=============================================================================
void CodeWindow::Draw() {
	// 開いているファイルが無ければ、ウィンドウごと出さない（全部のタブを閉じると消える。次に開くと、前と同じ場所に出る）
	if (files.empty()) {
		return;
	}
	// 開いたばかりなら、ウィンドウを前に出す（Scene・Game と同じ所にドッキングしてあっても、タブが前に来る）
	if (windowFocusRequest) {
		ImGui::SetNextWindowFocus();
		windowFocusRequest = false;
	}
	ImGui::SetNextWindowSize(ImVec2(700.0f, 500.0f), ImGuiCond_FirstUseEver); // 初めて出すときの大きさ（後は imgui.ini に覚えられる）
	ImGui::SetNextWindowBgAlpha(1.0f);                                        // 浮かせて置いても、後ろの文字がコードに重ならないように不透明にする
	if (!ImGui::Begin("Code")) {
		ImGui::End();
		return; // 隠れている間は読み直しもしない
	}
	ReloadChangedFiles();

	// --- Ctrl+F で検索欄へ（このウィンドウを触っているときだけ）---
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F)) {
		findFocusRequest = true;
	}

	// --- 開いているファイルを、タブで並べる ---
	if (ImGui::BeginTabBar("##codeTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll)) {
		for (CodeFile& file : files) {
			bool isOpen = true; // × を押すと false になる
			const ImGuiTabItemFlags flags = file.selectRequest ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
			file.selectRequest = false;
			// 「表示###ID」：表示はファイルの名前、ID は場所（同じ名前のファイルが2つあってもぶつからない）
			const std::string label = std::format("{}###{}", file.title, ToUtf8(file.path));
			const bool isSelected = ImGui::BeginTabItem(label.c_str(), &isOpen, flags);
			ImGui::SetItemTooltip("%s", ToUtf8(file.path).c_str());
			if (isSelected) {
				DrawToolbar(file);
				DrawCode(file);
				ImGui::EndTabItem();
			}
			file.closeRequest = !isOpen;
		}
		ImGui::EndTabBar();
	}
	// × を押したタブを閉じる（回している途中で消すと並びが崩れるので、回し終わってから）
	std::erase_if(files, [](const CodeFile& file) { return file.closeRequest; });
	ImGui::End();
}

//=============================================================================
// 開く
//=============================================================================
void CodeWindow::Open(const std::filesystem::path& path, int line) {
	windowFocusRequest = true;
	// もう開いていれば、そのタブを前に出す
	std::error_code error;
	for (CodeFile& file : files) {
		if (std::filesystem::equivalent(file.path, path, error)) {
			file.selectRequest = true;
			file.scrollToLine = line;
			return;
		}
	}
	// 新しく開く
	CodeFile file;
	file.path = path;
	file.title = ToUtf8(path.filename());
	if (!LoadFile(file)) {
		LogManager::Warning(std::format("ファイルを開けませんでした: {}", ToUtf8(path)));
		return;
	}
	file.selectRequest = true;
	file.scrollToLine = line;
	files.push_back(std::move(file));
}

bool CodeWindow::IsCodeFile(const std::filesystem::path& path) {
	// 拡張子を小文字にして比べる（.CPP でもよいように）
	std::string extension = path.extension().string();
	std::transform(extension.begin(), extension.end(), extension.begin(), [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; });
	return std::find(kCodeExtensions.begin(), kCodeExtensions.end(), extension) != kCodeExtensions.end();
}

void CodeWindow::OpenExternal(const std::filesystem::path& path) {
	std::error_code error;
	const std::wstring target = std::filesystem::absolute(path, error).make_preferred().wstring();
	ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

#else // USE_IMGUI

#include "CodeWindow.h"

// エディタの無いビルド（Release）では、何もしない
void CodeWindow::Initialize() {}
void CodeWindow::Draw() {}
void CodeWindow::Open(const std::filesystem::path&, int) {}
bool CodeWindow::IsCodeFile(const std::filesystem::path&) { return false; }
void CodeWindow::OpenExternal(const std::filesystem::path&) {}

#endif // USE_IMGUI