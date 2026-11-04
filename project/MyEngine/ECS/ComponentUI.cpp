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
#include "MyEngine/Graphics/Model/ModelManager.h"
#include "MyEngine/Graphics/Text/FontManager.h"
#include "MyEngine/Graphics/Texture/TextureManager.h"
#include "MyEngine/Graphics/Animation/AnimationManager.h"
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

// その種類のファイルを探すフォルダ（フォントだけは、Windows のフォントのフォルダも足す）
std::vector<std::filesystem::path> SearchRootsOf(AssetType type) {
	std::vector<std::filesystem::path> roots(std::begin(kAssetSearchRoots), std::end(kAssetSearchRoots));
	if (type == AssetType::Font) {
		const std::vector<std::filesystem::path> systemFolders = FontManager::GetSystemFontFolders();
		roots.insert(roots.end(), systemFolders.begin(), systemFolders.end());
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

	void ColorField(const char* label, Vector3& rgb) override { ImGui::ColorEdit3(label, &rgb.x); }
	void ColorField(const char* label, Vector4& rgba) override { ImGui::ColorEdit4(label, &rgba.x); }

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
	void ColorField(const char*, Vector3&) override {}
	void ColorField(const char*, Vector4&) override {}
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