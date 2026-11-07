#include "MyEngine/ECS/ComponentUI.h"

#include <array>
#include <format>
#include <string>
#include <cctype>
#include <algorithm>
#include <filesystem>

#include "MyEngine/AI/AttackTableManager.h"
#include "MyEngine/Core/NameList.h"
#include "MyEngine/Curve/CurveManager.h"
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/ECS/GameObject.h"
#include "MyEngine/Physics/PhysicMaterialManager.h"
#include "MyEngine/Graphics/Material/MaterialManager.h"
#include "MyEngine/Graphics/Model/ModelManager.h"
#include "MyEngine/Graphics/Text/FontManager.h"
#include "MyEngine/Graphics/Texture/TextureManager.h"
#include "MyEngine/Graphics/Animation/AnimationManager.h"
#include "MyEngine/Graphics/Pipeline/ShaderPackageLoader.h"
#include "MyEngine/Sound/SoundManager.h"
#include "MyEngine/String/ConvertString.h"
#include "MyEngine/String/FileNameRule.h"
#include "MyEngine/Timeline/TimelineManager.h"
#include "MyEngine/Scene/Prefab/PrefabManager.h"

//=============================================================================
// アセットの番号 ↔ パス（Inspector・保存・読み込みで共通）
//=============================================================================
const std::string& GetAssetPath(AssetType type, uint32_t handle) {
	static const std::string kEmpty; // 見つからないときに返す空文字（参照で返すので static にする）
	if (handle == 0) {
		return kEmpty;
	}
	switch (type) {
	case AssetType::Model:
		return ModelManager::GetModelPath(handle);
	case AssetType::Texture:
		return TextureManager::GetTexturePath(handle);
	case AssetType::Sound:
		return SoundManager::GetSoundPath(handle);
	case AssetType::Animation:
		return AnimationManager::GetPath(handle);
	case AssetType::Prefab:
		return PrefabManager::GetPath(handle);
	case AssetType::Font:
		return FontManager::GetPath(handle);
	case AssetType::PhysicMaterial:
		return PhysicMaterialManager::GetPath(handle);
	case AssetType::AttackTable:
		return AttackTableManager::GetPath(handle);
	case AssetType::Curve:
		return CurveManager::GetPath(handle);
	case AssetType::Timeline:
		return TimelineManager::GetPath(handle);
	case AssetType::Shader:
		return ShaderPackageLoader::GetProgramPath(handle);
	case AssetType::Material:
		return MaterialManager::GetPath(handle);
	}
	return kEmpty;
}

uint32_t LoadAsset(AssetType type, const std::string& path) {
	if (path.empty()) {
		return 0; // 未選択
	}
	// シーンファイル・Inspector から来るパスは UTF-8 の文字なので、ここで1回だけ path に直す（日本語のパスでも化けないように）
	const std::filesystem::path filePath = ToPath(path);
	// 無いファイルを読ませるとManagerの中で止まるので、先に確かめる
	std::error_code error;
	if (!std::filesystem::exists(filePath, error)) {
		LogManager::Warning(std::format("アセットが見つかりません（未選択として扱います）: {}", path));
		return 0;
	}
	switch (type) {
	case AssetType::Model:
		return ModelManager::Load(filePath);
	case AssetType::Texture:
		return TextureManager::Load(filePath);
	case AssetType::Sound:
		return SoundManager::Load(filePath);
	case AssetType::Animation:
		return AnimationManager::Load(filePath);
	case AssetType::Prefab:
		return PrefabManager::Load(filePath);
	case AssetType::Font:
		return FontManager::Load(filePath);
	case AssetType::PhysicMaterial:
		return PhysicMaterialManager::Load(filePath);
	case AssetType::AttackTable:
		return AttackTableManager::Load(filePath);
	case AssetType::Curve:
		return CurveManager::Load(filePath);
	case AssetType::Timeline:
		return TimelineManager::Load(filePath);
	case AssetType::Shader: {
		// .shader はエンジンが起動したときに全部読んである。番号は描画プログラムの番号 + 1（#PROGRAM の無い .shader は選べない）
		const uint32_t handle = ShaderPackageLoader::FindProgramHandle(path);
		if (handle == 0) {
			LogManager::Warning(std::format("#PROGRAM のある .shader ではないので、Shader に選べません（未選択として扱います）: {}", path));
		}
		return handle;
	}
	case AssetType::Material:
		return MaterialManager::Load(filePath);
	}
	return 0;
}

//=============================================================================
// アセットの種類ごとの拡張子（Inspector のファイルの一覧・Project ウィンドウのアイコン）
//=============================================================================
namespace {
constexpr std::array<std::string_view, 4> kModelExtensions = {".obj", ".gltf", ".glb", ".fbx"};
constexpr std::array<std::string_view, 5> kTextureExtensions = {".png", ".jpg", ".jpeg", ".bmp", ".hdr"};
constexpr std::array<std::string_view, 2> kSoundExtensions = {".wav", ".mp3"};
constexpr std::array<std::string_view, 3> kAnimationExtensions = {".gltf", ".glb", ".fbx"};
constexpr std::array<std::string_view, 1> kPrefabExtensions = {kPrefabFileExtension}; // ".prefab"
constexpr std::array<std::string_view, 3> kFontExtensions = {".ttf", ".otf", ".ttc"}; // .ttc は何個かのフォントが1つに入った物（1つ目を使う）
constexpr std::array<std::string_view, 1> kPhysicMaterialExtensions = {PhysicMaterialManager::kFileExtension}; // ".physmat"
constexpr std::array<std::string_view, 1> kAttackTableExtensions = {AttackTableManager::kFileExtension};       // ".attacktable"
constexpr std::array<std::string_view, 1> kCurveExtensions = {CurveManager::kFileExtension};                   // ".curve"
constexpr std::array<std::string_view, 1> kTimelineExtensions = {TimelineManager::kFileExtension};             // ".timeline"
constexpr std::array<std::string_view, 1> kShaderExtensions = {".shader"};                                    // ".shader"（#PROGRAM の無い物は、一覧に出さない）
constexpr std::array<std::string_view, 1> kMaterialExtensions = {MaterialManager::kFileExtension};             // ".material"

// その種類の拡張子の一覧
std::span<const std::string_view> ExtensionsOf(AssetType type) {
	switch (type) {
	case AssetType::Model:
		return kModelExtensions;
	case AssetType::Texture:
		return kTextureExtensions;
	case AssetType::Sound:
		return kSoundExtensions;
	case AssetType::Animation:
		return kAnimationExtensions;
	case AssetType::Prefab:
		return kPrefabExtensions;
	case AssetType::Font:
		return kFontExtensions;
	case AssetType::PhysicMaterial:
		return kPhysicMaterialExtensions;
	case AssetType::AttackTable:
		return kAttackTableExtensions;
	case AssetType::Curve:
		return kCurveExtensions;
	case AssetType::Timeline:
		return kTimelineExtensions;
	case AssetType::Shader:
		return kShaderExtensions;
	case AssetType::Material:
		return kMaterialExtensions;
	}
	return {};
}
} // namespace

bool IsAssetFile(std::string_view path, AssetType type) {
	// 最後の「.」から後ろが拡張子（フォルダの名前に入っている「.」は見ない）
	const size_t dot = path.find_last_of('.');
	const size_t slash = path.find_last_of("/\\");
	if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) {
		return false; // 拡張子が無い
	}
	// 大文字でも通るように、小文字へ直して比べる
	std::string extension(path.substr(dot));
	std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	const std::span<const std::string_view> candidates = ExtensionsOf(type);
	return std::find(candidates.begin(), candidates.end(), extension) != candidates.end();
}

//=============================================================================
// GameObject の項目（Script 用）
//=============================================================================
void ComponentUI::Field(const char* label, GameObject& value, FieldStyle style) {
	// EntityRef に直して、いつもの参照の項目に任せる（Inspector の見た目・保存の書き方が同じになる）
	EntityRef ref = EntityManager::RefOf(value.handle);
	Field(label, ref, style);
	// 書き換わっていたら（Inspector で落とした・ファイルから読んだ）、今の Handle に直して戻す
	value.handle = EntityManager::Find(ref);
}

#ifdef USE_IMGUI
#include <cstring>
#include <vector>
#include <limits>

#include <externals/imgui/imgui.h>

#include "MyEngine/Editor/Widgets/FontPreview.h"
#include "MyEngine/Editor/Widgets/EditorWidgets.h"
#include "MyEngine/Editor/Windows/HierarchyWindow.h"
namespace {
constexpr float kRangeDragPixels = 200.0f; // 範囲がある項目を、何ピクセル動かすと端から端まで行くか（スライダーの代わり）
constexpr float kMultilineRows = 4.5f;     // 何行もある文字の入力欄の高さ（行の数。半分は余白）

// 範囲が決まっているか
bool HasRange(const FieldStyle& style) { return style.min < style.max; }

// つまんで動かす速さ
// 指定が無ければ、両端がある範囲（Range）は kRangeDragPixels で端から端まで、それ以外（AtLeast・範囲なし）は型ごとの既定
float DragSpeed(const FieldStyle& style, float defaultSpeed) {
	if (style.dragSpeed > 0.0f) {
		return style.dragSpeed;
	}
	if (HasRange(style) && style.max < FLT_MAX) {
		return (style.max - style.min) / kRangeDragPixels;
	}
	return defaultSpeed;
}

// 直前の項目にマウスを乗せたら説明を出す
void DrawTooltip(const FieldStyle& style) {
	if (style.tooltip != nullptr && ImGui::IsItemHovered()) {
		ImGui::SetTooltip("%s", style.tooltip);
	}
}

// ===== アセットの一覧（フォルダを掘るのは、最初に開いたときと Refresh のときだけ）=====
constexpr std::array<const char*, 2> kAssetSearchRoots = {"resources", "MyEngine/Resources"}; // 探すフォルダ（ゲーム側とエンジン側）。無いフォルダは飛ばす
constexpr size_t kAssetTypeCount = magic_enum::enum_count<AssetType>();          // AssetType の数（種類を足しても、ここは直さなくてよい）

// 探す場所を1行で（"探す場所: resources/ MyEngine/Resources/（下のフォルダも全部）"）
// kAssetSearchRoots から作るので、場所を変えれば表示も変わる
const std::string& SearchRootsText() {
	static const std::string text = [] {
		std::string result = "探す場所:";
		for (const char* root : kAssetSearchRoots) {
			result += std::format(" {}/", root);
		}
		return result + "（下のフォルダも全部）";
	}();
	return text;
}

// その種類のファイルを探すフォルダ（フォントは Windows のフォントのフォルダ、Shader はエンジンの .shader の置き場も足す）
std::vector<std::filesystem::path> SearchRootsOf(AssetType type) {
	std::vector<std::filesystem::path> roots(std::begin(kAssetSearchRoots), std::end(kAssetSearchRoots));
	if (type == AssetType::Font) {
		const std::vector<std::filesystem::path> systemFolders = FontManager::GetSystemFontFolders();
		roots.insert(roots.end(), systemFolders.begin(), systemFolders.end());
	}
	// Shader は、エンジンの .shader の置き場（MyEngine/Shader/Data）も探す（エンジンの既定の Shader も選べるように）
	if (type == AssetType::Shader) {
		roots.emplace_back(kShaderDataDir);
	}
	return roots;
}

// その種類のファイルのパス一覧（区切りは / にそろえる。ModelManagerのキャッシュのキーと同じ形にするため）
// 探す場所ごとに名前の順に並べる（ゲームのフォルダの物が先、Windows のフォントは後ろ）
const std::vector<std::string>& AssetFiles(AssetType type, bool refresh) {
	static std::array<std::vector<std::string>, kAssetTypeCount> files; // [AssetType] ごとの一覧
	static std::array<bool, kAssetTypeCount> scanned{};              // 全部 false から
	const size_t slot = static_cast<size_t>(type);
	if (scanned[slot] && !refresh) {
		return files[slot];
	}
	files[slot].clear();
	for (const std::filesystem::path& root : SearchRootsOf(type)) {
		std::error_code error; // 例外ではなくエラーコードで受ける（フォルダが無くても止まらない）
		if (!std::filesystem::exists(root, error)) {
			continue;
		}
		const size_t first = files[slot].size(); // この場所の分は、ここから後ろに入る
		const auto options = std::filesystem::directory_options::skip_permission_denied;
		for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(root, options, error)) {
			if (!entry.is_regular_file(error)) {
				continue;
			}
			// UTF-8 で作る（generic_string() は Shift-JIS になり、ImGui・Manager は UTF-8 のつもりで読むので合わない）
			const std::string path = ToUtf8(entry.path());
			if (!IsAssetFile(path, type)) {
				continue;
			}
			// Shader は、#PROGRAM のある .shader（.material が選べる物）だけ。Include・VS・ポストエフェクトの PS は出さない
			if (type == AssetType::Shader && ShaderPackageLoader::FindProgramHandle(path) == 0) {
				continue;
			}
			// 日本語などが入った名前は、選ぶと Manager が読めずに止まるので、一覧に出さない（Audio.md 8章）
			if (!IsLoadableAssetPath(path)) {
				LogManager::Warning(std::format("名前に英数字以外の文字があるので、一覧に出しません（名前を英数字にしてください）: {}", path));
				continue;
			}
			files[slot].push_back(path);
		}
		std::sort(files[slot].begin() + first, files[slot].end());
	}
	scanned[slot] = true;
	return files[slot];
}

// ===== フォントの選択欄 =====
constexpr float kFontRowSampleSize = 16.0f;  // 一覧の1行の見本の文字の大きさ（ピクセル）
constexpr float kSelectedSampleSize = 20.0f; // 選んでいるフォントの見本（欄の下）の文字の大きさ
constexpr float kFontListMinWidth = 480.0f;  // 一覧の窓の幅の下限（見本の文字が切れないように）
constexpr float kFontListMaxHeight = 560.0f; // 一覧の窓の高さの上限（超えたらスクロール）
constexpr const char* kFontSampleText = "あいうえお アイウエオ 漢字 ABC abc 0123"; // 見本の文字（日本語・英字・数字が出るか分かるように）

// フォントの一覧の1行（上にファイル名、下にそのフォントで書いた見本）。押されたら true
bool DrawFontRow(const std::string& file, bool isSelected) {
	const float nameHeight = ImGui::GetTextLineHeight();
	const float rowHeight = nameHeight + kFontRowSampleSize;
	// 行の大きさの、文字の無い Selectable を置いてから、その上に自分で文字を描く（1行に2つの大きさの文字を出すため）
	ImGui::PushID(file.c_str());
	const bool clicked = ImGui::Selectable("##font", isSelected, ImGuiSelectableFlags_None, ImVec2(0.0f, rowHeight));
	ImGui::PopID();
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("%s", file.c_str()); // 置いてある場所
	}
	const ImVec2 rowMin = ImGui::GetItemRectMin();
	const ImVec2 samplePosition(rowMin.x, rowMin.y + nameHeight);
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddText(rowMin, ImGui::GetColorU32(ImGuiCol_TextDisabled), ToUtf8(ToPath(file).filename()).c_str());
	// 見本は、そのフォントで書く（読んでいる途中なら「…」、読めないフォントならそう書く）
	if (ImFont* font = FontPreview::Get(file)) {
		drawList->AddText(font, kFontRowSampleSize, samplePosition, ImGui::GetColorU32(ImGuiCol_Text), kFontSampleText);
	} else {
		drawList->AddText(samplePosition, ImGui::GetColorU32(ImGuiCol_TextDisabled), FontPreview::IsFailed(file) ? "（このフォントは読めません）" : "…");
	}
	return clicked;
}

// 選んでいるフォントの見本（欄の下に、大きめに）
void DrawSelectedFontSample(const std::string& file) {
	ImFont* font = file.empty() ? nullptr : FontPreview::Get(file);
	if (font == nullptr) {
		return;
	}
	ImGui::PushFont(font, kSelectedSampleSize);
	ImGui::TextUnformatted(kFontSampleText);
	ImGui::PopFont();
}

// ===== 名前の一覧のビットの並び =====
constexpr uint32_t kAllBits = 0xFFFFFFFFu; // 全部選んだ（Everything）
constexpr size_t kMaskPreviewNames = 3;    // 欄に並べる名前の数（これより多ければ「…」）

// 欄に出す文字（何も無し・全部・選んだ名前を「,」でつないだ物）
std::string MakeMaskPreview(uint32_t bits, const NameList& names) {
	if (bits == 0u) {
		return "Nothing";
	}
	if (bits == kAllBits) {
		return "Everything";
	}
	std::string text;
	size_t count = 0;
	for (int i = 0; i < NameList::kCount; ++i) {
		if ((bits & (1u << i)) == 0u) {
			continue;
		}
		if (count == kMaskPreviewNames) {
			return text + ", …";
		}
		const std::string name = names.GetName(i);
		text += (count == 0 ? "" : ", ") + (name.empty() ? std::format("({})", i) : name);
		++count;
	}
	return text;
}

// ===== クリップの名前の欄 =====
constexpr ImU32 kMissingClipColor = IM_COL32(255, 96, 96, 255); // ファイルに無い名前の色（攻撃表の知らない部品と同じ赤）

// ===== 一覧 =====
constexpr const char* kListDragType = "MYENGINE_LIST_ITEM"; // 一覧の要素を運ぶドラッグの種類（ImGui の決まりで 32 文字まで）
constexpr float kListGripWidthRate = 0.75f;                  // 並べ替えのつまみの幅（行の高さに対する割合）
constexpr float kListGripLineRate = 0.18f;                   // つまみの横線の間隔（行の高さに対する割合）

/// <summary>
/// ドラッグで運ぶ中身（どの一覧の何番か。ほかの一覧へは落とせないように、一覧の ID も運ぶ）
/// </summary>
struct ListDragPayload {
	ImGuiID listId = 0; // 運び始めた一覧（ImGui の ID。Component・項目の名前で決まる）
	size_t index = 0;   // 運んでいる要素の番号
};

/// <summary>
/// 描き終わってからする操作（描いている途中で並びを変えると、残りの要素の番号がずれて、別の要素を描いてしまう）
/// </summary>
struct ListEdit {
	enum class Kind {
		None,        // 何もしない
		Add,         // 最後に足す（最後の要素の写し。空なら初期値）
		RemoveLast,  // 最後を消す
		InsertAbove, // index の上に初期値を足す
		Duplicate,   // index の写しを、すぐ下に足す
		Remove,      // index を消す
		Move,        // index を to の位置へ
	};
	Kind kind = Kind::None;
	size_t index = 0; // 対象の要素
	size_t to = 0;    // Move の行き先
};

// "表示名##キー" の表示名の所（ImGui と同じく、## から後ろは出さない）
std::string DisplayLabel(const char* label) {
	const char* mark = std::strstr(label, "##");
	return (mark != nullptr) ? std::string(label, mark) : std::string(label);
}

// 並べ替えのつまみ（横線3本）。押したまま動かすと、要素を運ぶドラッグになる
void DrawListGrip() {
	const float height = ImGui::GetFrameHeight();
	ImGui::InvisibleButton("##grip", ImVec2(height * kListGripWidthRate, height));
	const ImVec2 min = ImGui::GetItemRectMin();
	const ImVec2 max = ImGui::GetItemRectMax();
	const bool isHot = ImGui::IsItemHovered() || ImGui::IsItemActive();
	const ImU32 color = ImGui::GetColorU32(isHot ? ImGuiCol_Text : ImGuiCol_TextDisabled);
	const float centerY = (min.y + max.y) * 0.5f;
	const float gap = height * kListGripLineRate;
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	for (int line = -1; line <= 1; ++line) {
		const float y = centerY + gap * static_cast<float>(line);
		drawList->AddLine(ImVec2(min.x + 2.0f, y), ImVec2(max.x - 2.0f, y), color, 1.5f);
	}
	if (isHot) {
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS); // 上下に動かせることが分かるように
	}
}

// 次のフレームから、index 番の要素の見出しを開いておく（足した要素の中身がすぐ見えるように）
// 見出しの開け閉めは、ImGui が ID ごとに覚えている（要素の見出しの ID は、番号の PushID の中の "##element"）
void OpenListElement(size_t index) {
	ImGui::PushID(static_cast<int>(index));
	const ImGuiID id = ImGui::GetID("##element");
	ImGui::PopID();
	ImGui::GetStateStorage()->SetInt(id, 1);
}

// 描き終わってから、頼まれた操作をする
void ApplyListEdit(ListAccess& list, const ListEdit& edit) {
	const size_t size = list.GetSize();
	const bool isFull = size >= list.GetCapacity();
	switch (edit.kind) {
	case ListEdit::Kind::None:
		break;
	case ListEdit::Kind::Add:
		if (isFull) {
			break;
		}
		// Unity と同じく、最後の要素を写して足す（段階の一覧のように、前の要素と似た値を続けて入れることが多いので）
		if (size > 0) {
			list.Duplicate(size - 1);
		} else {
			list.Insert(0);
		}
		OpenListElement(size);
		break;
	case ListEdit::Kind::RemoveLast:
		if (size > 0) {
			list.Erase(size - 1);
		}
		break;
	case ListEdit::Kind::InsertAbove:
		if (!isFull && edit.index <= size) {
			list.Insert(edit.index);
			OpenListElement(edit.index);
		}
		break;
	case ListEdit::Kind::Duplicate:
		if (!isFull && edit.index < size) {
			list.Duplicate(edit.index);
			OpenListElement(edit.index + 1);
		}
		break;
	case ListEdit::Kind::Remove:
		if (edit.index < size) {
			list.Erase(edit.index);
		}
		break;
	case ListEdit::Kind::Move:
		if (edit.index < size && edit.to < size) {
			list.Move(edit.index, edit.to);
		}
		break;
	}
}


/// <summary>
/// Inspectorに描く係。ImGuiを知っているのはここだけ
/// </summary>
class InspectorComponentUI final : public ComponentUI {
public:
	void Field(const char* label, float& value, FieldStyle style) override {
		if (HasRange(style)) {
			ImGui::DragFloat(label, &value, DragSpeed(style, 0.01f), style.min, style.max, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		} else {
			ImGui::DragFloat(label, &value, DragSpeed(style, 0.01f));
		}
		DrawTooltip(style);
	}

	void Field(const char* label, int& value, FieldStyle style) override {
		const int min = static_cast<int>(style.min);
		// AtLeast の上限（FLT_MAX）は int に入らない（そのまま変換すると値が壊れる）ので、int の一番大きい値にする
		// (std::numeric_limits<int>::max) とかっこで囲むのは、windows.h の max マクロに邪魔されないため
		constexpr int kIntMax = (std::numeric_limits<int>::max)();
		const int max = (style.max >= static_cast<float>(kIntMax)) ? kIntMax : static_cast<int>(style.max);
		if (HasRange(style)) {
			ImGui::DragInt(label, &value, DragSpeed(style, 1.0f), min, max, "%d", ImGuiSliderFlags_AlwaysClamp);
		} else {
			ImGui::DragInt(label, &value, DragSpeed(style, 1.0f));
		}
		DrawTooltip(style);
	}

	void Field(const char* label, bool& value, FieldStyle style) override {
		ImGui::Checkbox(label, &value);
		DrawTooltip(style);
	}

	void Field(const char* label, Vector2& value, FieldStyle style) override {
		if (HasRange(style)) {
			ImGui::DragFloat2(label, &value.x, DragSpeed(style, 0.01f), style.min, style.max, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		} else {
			ImGui::DragFloat2(label, &value.x, DragSpeed(style, 0.01f));
		}
		DrawTooltip(style);
	}

	void Field(const char* label, Vector3& value, FieldStyle style) override {
		if (HasRange(style)) {
			ImGui::DragFloat3(label, &value.x, DragSpeed(style, 0.01f), style.min, style.max, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		} else {
			ImGui::DragFloat3(label, &value.x, DragSpeed(style, 0.01f));
		}
		DrawTooltip(style);
	}

	void Field(const char* label, Vector4& value, FieldStyle style) override {
		if (HasRange(style)) {
			ImGui::DragFloat4(label, &value.x, DragSpeed(style, 0.01f), style.min, style.max, "%.3f", ImGuiSliderFlags_AlwaysClamp);
		} else {
			ImGui::DragFloat4(label, &value.x, DragSpeed(style, 0.01f));
		}
		DrawTooltip(style);
	}

	// 相手の名前を出した台。HierarchyからEntityをドラッグして落とすと入る。右クリックで外す
	void Field(const char* label, EntityRef& value, FieldStyle style) override {
		const Entity* target = EntityManager::Get(EntityManager::Find(value));
		// 番号はあるのに居ない＝相手が消えている（消した操作をUndoすると、同じ番号で戻ってまた指す）
		const std::string preview = target ? target->name : (value.IsSet() ? "(missing)" : "(none)");
		ImGui::PushID(label);                                                 // 同じ区画に参照が2つあっても、台のIDがぶつからないように
		ImGui::Button(preview.c_str(), ImVec2(ImGui::CalcItemWidth(), 0.0f)); // 押しても何もしない
		DrawTooltip(style);
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(HierarchyWindow::kDragDropType)) {
				// Hierarchyが運んでくるのはHandle。覚えるのは作り直しても変わらない番号
				value = EntityManager::RefOf(*static_cast<const Handle<Entity>*>(payload->Data));
			}
			ImGui::EndDragDropTarget();
		}
		if (ImGui::BeginPopupContextItem("menu")) {
			if (ImGui::MenuItem("Clear")) {
				value = {};
			}
			ImGui::EndPopup();
		}
		ImGui::PopID();
		// 他の項目と同じく、ラベルは右に出す（"表示###キー" の ## から後ろは、ImGuiのほかの項目と同じく出さない）
		ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
		ImGui::TextUnformatted(label, std::strstr(label, "##")); // 見つからなければnullptr＝最後まで出す
	}

	// 文字の入力欄（長さの上限なし。日本語も打てる）。Multiline なら何行もある箱（4行分の高さ）
	void Field(const char* label, std::string& value, FieldStyle style) override {
		if (style.multiline) {
			EditorWidgets::InputTextMultiline(label, value, ImVec2(0.0f, ImGui::GetTextLineHeight() * kMultilineRows));
		} else {
			EditorWidgets::InputText(label, value);
		}
		DrawTooltip(style);
	}

	// resources以下の、その種類のファイルから選ぶ
	void AssetField(const char* label, uint32_t& handle, AssetType type, FieldStyle style) override {
		if (type == AssetType::Font) {
			FontField(label, handle, style); // フォントは、選択肢ごとにそのフォントの見本を出す
			return;
		}
		const std::string& path = GetAssetPath(type, handle);
		const char* preview = (handle == 0) ? "(none)" : (path.empty() ? "(unknown)" : path.c_str());
		if (ImGui::BeginCombo(label, preview)) {
			ImGui::TextDisabled("%s", SearchRootsText().c_str()); // どこを探しているか（置き場所を覚えていなくても分かるように）
			const bool refresh = ImGui::Selectable("[Refresh]");  // フォルダにファイルを増やしたとき用
			if (ImGui::Selectable("(none)", handle == 0)) {
				handle = 0;
			}
			for (const std::string& file : AssetFiles(type, refresh)) {
				if (ImGui::Selectable(file.c_str(), file == path)) {
					handle = LoadAsset(type, file); // 同じパスならキャッシュが返るので、選び直しても読み込み直さない
				}
			}
			ImGui::EndCombo();
		}
		DrawTooltip(style);
	}

	// Settings の名前の一覧から1つ選ぶ（名前の付いている番号だけ並べる。今の番号に名前が無くても、欄にはその番号を出す）
	void NameField(const char* label, int& index, const NameList& names, FieldStyle style) override {
		const std::string preview = names.GetLabel(index);
		if (ImGui::BeginCombo(label, preview.c_str())) {
			for (const int i : names.GetNamedIndices()) {
				const std::string text = names.GetLabel(i);
				if (ImGui::Selectable(text.c_str(), i == index)) {
					index = i;
				}
				if (i == index) {
					ImGui::SetItemDefaultFocus(); // 開いたときに今の選択へスクロールする
				}
			}
			ImGui::Separator();
			ImGui::TextDisabled("名前は Settings の %s で付ける", names.GetGroupName().c_str());
			ImGui::EndCombo();
		}
		DrawTooltip(style);
	}

	// Settings の名前の一覧から、いくつでも選ぶ（開くと、名前の付いている番号のチェックが並ぶ）
	void NameMaskField(const char* label, uint32_t& bits, const NameList& names, FieldStyle style) override {
		const std::string preview = MakeMaskPreview(bits, names);
		if (ImGui::BeginCombo(label, preview.c_str())) {
			if (ImGui::Selectable("Nothing", bits == 0u)) {
				bits = 0u;
			}
			if (ImGui::Selectable("Everything", bits == kAllBits)) {
				bits = kAllBits;
			}
			ImGui::Separator();
			// チェックは押しても一覧が閉じない（Selectable は閉じる）ので、続けていくつも選べる
			for (const int i : names.GetNamedIndices()) {
				const uint32_t bit = 1u << i;
				bool isOn = (bits & bit) != 0u;
				if (ImGui::Checkbox(names.GetLabel(i).c_str(), &isOn)) {
					bits = isOn ? (bits | bit) : (bits & ~bit);
				}
			}
			ImGui::Separator();
			ImGui::TextDisabled("名前は Settings の %s で付ける", names.GetGroupName().c_str());
			ImGui::EndCombo();
		}
		DrawTooltip(style);
	}

	// アニメーションのクリップの名前。ClipSourceScope で決めたファイルの中の名前から選ぶ
	// ファイルが決まっていない（Sample Animation が未選択など）なら、名前を打つ欄にする
	void ClipField(const char* label, std::string& clip, FieldStyle style) override {
		const uint32_t source = GetClipSource();
		const uint32_t count = AnimationManager::GetAnimationCount(source); // 未選択なら 0
		if (count == 0) {
			Field(label, clip, style);
			return;
		}
		// --- 欄（ファイルに無い名前は、消さずに赤で出す）---
		const bool isMissing = !clip.empty() && !AnimationManager::FindAnimationIndex(source, clip).has_value();
		const std::string preview = clip.empty() ? "(none)" : (isMissing ? std::format("(無い) {}", clip) : clip);
		if (isMissing) {
			ImGui::PushStyleColor(ImGuiCol_Text, kMissingClipColor);
		}
		const bool isOpen = ImGui::BeginCombo(label, preview.c_str());
		if (isMissing) {
			ImGui::PopStyleColor(); // 赤は欄の文字だけ（一覧の中は、いつもの色）
		}
		if (isOpen) {
			// --- 一覧（一番上に、今の無い名前。選び直すまで、そのまま持つ）---
			if (isMissing) {
				ImGui::TextColored(ImColor(kMissingClipColor), "(無い) %s", clip.c_str());
				ImGui::Separator();
			}
			if (ImGui::Selectable("(none)", clip.empty())) {
				clip.clear();
			}
			for (uint32_t index = 0; index < count; ++index) {
				const Animation* animation = AnimationManager::GetAnimation(source, index);
				const bool isSelected = (animation->name == clip);
				ImGui::PushID(static_cast<int>(index)); // 同じ名前のアニメーションが2つあっても、別の行として押せるように
				if (ImGui::Selectable(animation->name.c_str(), isSelected)) {
					clip = animation->name;
				}
				ImGui::SetItemTooltip("%.2f 秒", animation->duration);
				if (isSelected) {
					ImGui::SetItemDefaultFocus(); // 開いたときに今の選択へスクロールする
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		if (isMissing) {
			ImGui::SetItemTooltip("「%s」という名前のアニメーションが %s にありません（流さずに、モデルに入っている形で描きます）。名前を戻すか、選び直してください", clip.c_str(),
			                      AnimationManager::GetPath(source).c_str());
		} else {
			DrawTooltip(style);
		}
	}

	void ColorField(const char* label, Vector3& rgb, FieldStyle style) override {
		ImGui::ColorEdit3(label, &rgb.x);
		DrawTooltip(style);
	}
	void ColorField(const char* label, Vector4& rgba, FieldStyle style) override {
		ImGui::ColorEdit4(label, &rgba.x);
		DrawTooltip(style);
	}

	void BeginDisabled(bool isDisabled) override { ImGui::BeginDisabled(isDisabled); }
	void EndDisabled() override { ImGui::EndDisabled(); }
	void SameLineAfterCheck() override {
		const float checkWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - checkWidth); // 値の欄の右端を、ほかの行とそろえる
	}

	void Label(const char* text) override { ImGui::TextDisabled("%s", text); }
	void Separator(const char* text) override {
		if (text != nullptr) {
			ImGui::SeparatorText(text);
		} else {
			ImGui::Separator();
		}
	}
	void Space() override { ImGui::Spacing(); }
	bool Button(const char* label) override { return ImGui::Button(label); }

protected:
	void EnumField(const char* label, size_t& index, std::span<const std::string_view> names, FieldStyle style) override {
		// string_view は末尾に0があるとは限らないので、ImGuiに渡す前に std::string にする
		const std::string current = index < names.size() ? std::string(names[index]) : std::string("?");
		if (ImGui::BeginCombo(label, current.c_str())) {
			for (size_t i = 0; i < names.size(); ++i) {
				const std::string name(names[i]);
				if (ImGui::Selectable(name.c_str(), i == index)) {
					index = i;
				}
				if (i == index) {
					ImGui::SetItemDefaultFocus(); // 開いたときに今の選択へスクロールする
				}
			}
			ImGui::EndCombo();
		}
		DrawTooltip(style);
	}

	// 一覧：見出し（名前と数）を開くと、要素の見出しが並ぶ。要素の見出しを開くと、その要素の項目
	// 左のつまみをドラッグして並べ替え、要素の見出しの右クリックで上に足す・写す・消す。一番下の ＋ で最後を写して足す、－ で最後を消す
	void ListFieldErased(const char* label, ListAccess& list, FieldStyle style) override {
		ImGui::PushID(label); // 同じ区画に一覧が2つあっても、要素の ID がぶつからないように
		const size_t size = list.GetSize();
		const size_t capacity = list.GetCapacity();
		const bool isOpen = ImGui::TreeNodeEx("##list", ImGuiTreeNodeFlags_DefaultOpen, "%s (%zu)", DisplayLabel(label).c_str(), size);
		DrawTooltip(style);
		if (!isOpen) {
			ImGui::PopID();
			return;
		}
		const ImGuiID listId = ImGui::GetID("##items"); // 運んだ要素を、同じ一覧にだけ落とせるようにする印
		ListEdit edit;

		// --- 要素 ---
		for (size_t i = 0; i < size; ++i) {
			ImGui::PushID(static_cast<int>(i));
			const std::string title = list.MakeTitle(i);
			const std::string heading = title.empty() ? std::format("{}", i) : std::format("{}  {}", i, title);

			// つまみ（ここから運ぶ）
			DrawListGrip();
			if (ImGui::BeginDragDropSource()) {
				const ListDragPayload payload{listId, i};
				ImGui::SetDragDropPayload(kListDragType, &payload, sizeof(payload));
				ImGui::TextUnformatted(heading.c_str()); // 運んでいる間、マウスの横に出す
				ImGui::EndDragDropSource();
			}
			ImGui::SameLine();

			// 見出し（ID は番号だけで決める。見出しの文字が変わっても、開け閉めが変わらないように）
			const bool isElementOpen = ImGui::TreeNodeEx("##element", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", heading.c_str());
			// 見出しに落とすと、その位置へ動かす（同じ一覧から運んだ物だけ。ほかの一覧の物は、落とせる色も出さない）
			if (ImGui::BeginDragDropTarget()) {
				const ImGuiPayload* dragging = ImGui::GetDragDropPayload();
				const bool isSameList = dragging != nullptr && dragging->IsDataType(kListDragType) && static_cast<const ListDragPayload*>(dragging->Data)->listId == listId;
				if (isSameList) {
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kListDragType)) {
						edit = {ListEdit::Kind::Move, static_cast<const ListDragPayload*>(payload->Data)->index, i};
					}
				}
				ImGui::EndDragDropTarget();
			}
			// 見出しの右クリック
			if (ImGui::BeginPopupContextItem("##elementMenu")) {
				const bool isFull = size >= capacity;
				if (ImGui::MenuItem("Insert Above", nullptr, false, !isFull)) {
					edit = {ListEdit::Kind::InsertAbove, i};
				}
				if (ImGui::MenuItem("Duplicate", nullptr, false, !isFull)) {
					edit = {ListEdit::Kind::Duplicate, i};
				}
				if (ImGui::MenuItem("Remove")) {
					edit = {ListEdit::Kind::Remove, i};
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Move Up", nullptr, false, i > 0)) {
					edit = {ListEdit::Kind::Move, i, i - 1};
				}
				if (ImGui::MenuItem("Move Down", nullptr, false, i + 1 < size)) {
					edit = {ListEdit::Kind::Move, i, i + 1};
				}
				ImGui::EndPopup();
			}
			// 中身（要素1つの見せ方。ui.Field の並び）
			if (isElementOpen) {
				list.DescribeElement(*this, i);
				ImGui::TreePop();
			}
			ImGui::PopID();
		}

		// --- 足す・消すボタン（上限があれば、今の数と上限も出す）---
		ImGui::BeginDisabled(size >= capacity);
		if (EditorWidgets::PlusButton("##add")) {
			edit = {ListEdit::Kind::Add};
		}
		ImGui::SetItemTooltip("最後の要素を写して足す（空なら初期値）");
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(size == 0);
		if (ImGui::Button(" - ")) {
			edit = {ListEdit::Kind::RemoveLast};
		}
		ImGui::SetItemTooltip("最後の要素を消す（途中の要素は、見出しの右クリックの Remove）");
		ImGui::EndDisabled();
		if (capacity != SIZE_MAX) {
			ImGui::SameLine();
			ImGui::TextDisabled("%zu / %zu", size, capacity);
		}

		ApplyListEdit(list, edit);
		ImGui::TreePop();
		ImGui::PopID();
	}

	// フォントを選ぶ欄。選択肢ごとに、そのフォントで書いた見本を並べる（見えている行のフォントだけ読む）。欄の下に、選んでいるフォントの見本
	void FontField(const char* label, uint32_t& handle, FieldStyle style) {
		const std::string& path = GetAssetPath(AssetType::Font, handle);
		const std::string preview = (handle == 0) ? "(none) 既定のフォント" : (path.empty() ? "(unknown)" : ToUtf8(ToPath(path).filename()));
		// 一覧の窓の大きさ（見本の文字が切れない幅・画面に収まる高さ。BeginCombo の前に決めると、その大きさで開く）
		ImGui::SetNextWindowSizeConstraints(ImVec2(kFontListMinWidth, 0.0f), ImVec2(FLT_MAX, kFontListMaxHeight));
		if (ImGui::BeginCombo(label, preview.c_str())) {
			ImGui::TextDisabled("%s", SearchRootsText().c_str());
			ImGui::TextDisabled("＋ Windows に入っているフォント（全員用と、自分だけに入れた物）");
			const bool refresh = ImGui::Selectable("[Refresh]");
			// 名前で絞り込む（Add Component の検索と同じ ImGuiTextFilter。大文字・小文字は区別しない）
			static ImGuiTextFilter filter;
			filter.Draw("Search##fontFilter");
			if (ImGui::Selectable("(none) 既定のフォント", handle == 0)) {
				handle = 0;
			}
			std::vector<const std::string*> shown; // 絞り込んだ後の一覧
			for (const std::string& file : AssetFiles(AssetType::Font, refresh)) {
				if (filter.PassFilter(file.c_str())) {
					shown.push_back(&file);
				}
			}
			// ImGuiListClipper：見えている行だけを描く（見えない行のフォントは読まない。百個以上あっても重くならない）
			ImGuiListClipper clipper;
			clipper.Begin(static_cast<int>(shown.size()));
			while (clipper.Step()) {
				for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
					const std::string& file = *shown[i];
					if (DrawFontRow(file, file == path)) {
						handle = LoadAsset(AssetType::Font, file);
					}
				}
			}
			ImGui::EndCombo();
		}
		DrawTooltip(style);
		DrawSelectedFontSample((handle == 0) ? FontManager::GetPath(FontManager::GetDefaultFont()) : GetAssetPath(AssetType::Font, handle));
	}
};
} // namespace

#else // USE_IMGUI

namespace {
/// <summary>
/// Editorの無いビルド（Release）用。Componentの見せ方は書かれているが、描く相手が居ないので何もしない
/// </summary>
class InspectorComponentUI final : public ComponentUI {
public:
	void Field(const char*, float&, FieldStyle) override {}
	void Field(const char*, int&, FieldStyle) override {}
	void Field(const char*, bool&, FieldStyle) override {}
	void Field(const char*, Vector2&, FieldStyle) override {}
	void Field(const char*, Vector3&, FieldStyle) override {}
	void Field(const char*, Vector4&, FieldStyle) override {}
	void Field(const char*, EntityRef&, FieldStyle) override {}
	void Field(const char*, std::string&, FieldStyle) override {}
	void AssetField(const char*, uint32_t&, AssetType, FieldStyle) override {}
	void ColorField(const char*, Vector3&, FieldStyle) override {}
	void ColorField(const char*, Vector4&, FieldStyle) override {}
	void Label(const char*) override {}
	void Separator(const char*) override {}
	void Space() override {}
	bool Button(const char*) override { return false; }

protected:
	void EnumField(const char*, size_t&, std::span<const std::string_view>, FieldStyle) override {}
};
} // namespace

#endif // USE_IMGUI

//=============================================================================
// Inspectorに描く係を取り出す
//=============================================================================
ComponentUI& GetInspectorUI() {
	static InspectorComponentUI ui; // 最初に呼ばれたときに1つだけ作られる
	return ui;
}