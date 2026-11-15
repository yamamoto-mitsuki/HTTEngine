#include "KeyConfigTest.h"

#include <format>

#include <HTTEngine/Components/UI/TextComponent.h>

SCRIPT(KeyConfigTest, "Input"); // クラス名が保存の名前なので後から変えない

namespace {
constexpr float kLeft = 40.0f;          // 文字の左端（画面の左からのピクセル）
constexpr float kTop = 40.0f;           // 一番上の文字（画面の上からのピクセル）
constexpr float kLineHeight = 30.0f;    // 1行の高さ（ピクセル）
constexpr float kFontSize = 22.0f;      // 文字の大きさ
constexpr Vector4 kNormalColor = {1.0f, 1.0f, 1.0f, 1.0f};
constexpr Vector4 kSelectedColor = {1.0f, 0.85f, 0.3f, 1.0f};

// 行の名前（ゲームの画面に出す文字なので、ゲームが決める）
const char* RowLabel(InputBindingRow row) {
	switch (row) {
	case InputBindingRow::Positive:
		return "＋";
	case InputBindingRow::Negative:
		return "−";
	case InputBindingRow::Axis:
		return "軸";
	case InputBindingRow::Up:
		return "上";
	case InputBindingRow::Down:
		return "下";
	case InputBindingRow::Left:
		return "左";
	case InputBindingRow::Right:
		return "右";
	case InputBindingRow::Horizontal:
		return "横の軸";
	case InputBindingRow::Vertical:
		return "縦の軸";
	default:
		return "";
	}
}

// 画面の左上から数えた位置に置く Text を作る（Component が付くのは次のフレームの頭）
GameObject CreateText(const std::string& name, GameObject parent, int line) {
	GameObject object = GameObject::Create(name, parent);
	object.transform.translation = {kLeft, -(kTop + kLineHeight * static_cast<float>(line)), 0.0f};
	TextComponent text;
	text.enabled = false;
	text.text = "";
	text.fontSize = kFontSize;
	text.align = TextAlign::Left;
	text.anchor = ScreenAnchor::TopLeft;
	text.pivot = {0.0f, 1.0f}; // 左上を位置に合わせる
	text.outlineWidth = 2.0f;
	object.AddComponent(text);
	return object;
}

// Text の文字・色・出すか（まだ Component が付いていなければ何もしない）
void SetText(GameObject object, bool isVisible, const std::string& value, const Vector4& color = kNormalColor) {
	TextComponent* text = object ? object.GetComponent<TextComponent>() : nullptr;
	if (text == nullptr) {
		return;
	}
	text->enabled = isVisible;
	text->text = value;
	text->color = color;
}
} // namespace


//=============================================================================
// 初期化・更新・終わり
//=============================================================================
// ===== 初期化 =====
void KeyConfigTest::Start() {
	BuildItems();
	titleText_ = CreateText("KeyConfigTitle", gameObject, 0);
	for (size_t i = 0; i < items_.size(); ++i) {
		rowTexts_.push_back(CreateText(std::format("KeyConfigRow{}", i), gameObject, static_cast<int>(i) + 1));
	}
	statusText_ = CreateText("KeyConfigStatus", gameObject, static_cast<int>(items_.size()) + 2);
}

// ===== 更新 =====
void KeyConfigTest::Update() {
	// --- 拾う係の結果（GameInput が、このフレームの Script より前に動かしている）---
	switch (GameInput::GetRebindResult()) {
	case InputRebinder::Result::Captured: {
		const InputActionMap& map = GameInput::GetMap();
		const std::vector<InputBindingRef> conflicts = map.FindConflicts(GameInput::GetRebindAction(), GameInput::GetRebindIndex());
		status_ = conflicts.empty() ? "入れ替えました" : std::format("入れ替えました（「{}」とぶつかっています）", map.GetDesc(conflicts.front().action).label);
		break;
	}
	case InputRebinder::Result::Canceled:
		status_ = "やめました";
		break;
	case InputRebinder::Result::Listening:
	case InputRebinder::Result::Idle:
		break;
	}

	// --- 開く・閉じる（拾っている間は、表が止まっているので効かない）---
	if (GameInput::IsTriggered("Menu.Open")) {
		SetOpen(!isOpen_);
	}

	// --- 選ぶ・決める ---
	if (isOpen_ && !GameInput::IsRebinding() && !items_.empty()) {
		const int count = static_cast<int>(items_.size());
		if (GameInput::IsTriggered("Menu.Up")) {
			cursor_ = (cursor_ + count - 1) % count;
		}
		if (GameInput::IsTriggered("Menu.Down")) {
			cursor_ = (cursor_ + 1) % count;
		}
		if (GameInput::IsTriggered("Menu.Submit")) {
			Submit();
		}
	}
	Refresh();
}

// ===== 終わり =====
void KeyConfigTest::OnDestroy() {
	if (isOpen_) {
		Time::SetGameTimeScale(1.0f); // 止めた時間を戻す
	}
	GameInput::CancelRebind();
}


//=============================================================================
// 項目
//=============================================================================
// ===== 一覧を作る（変えられない Action は出さない）=====
void KeyConfigTest::BuildItems() {
	items_.clear();
	const InputActionMap& map = GameInput::GetMap();
	for (uint32_t i = 0; i < map.GetActionCount(); ++i) {
		const InputActionId id = {i};
		const InputActionDesc& desc = map.GetDesc(id);
		if (desc.isFixed) {
			continue;
		}
		for (const InputBindingRow row : InputBindingRows::Get(desc.type)) {
			if ((map.GetAcceptedDevices(id) & InputBindingRows::DevicesOf(row)) == InputDeviceMask::None) {
				continue; // この行に入れられる入力が無い
			}
			items_.push_back({Item::Kind::Binding, id, row});
		}
	}
	items_.push_back({Item::Kind::Reset});
	items_.push_back({Item::Kind::Revert});
	items_.push_back({Item::Kind::Save});
}

// ===== 開く・閉じる =====
void KeyConfigTest::SetOpen(bool isOpen) {
	isOpen_ = isOpen;
	Time::SetGameTimeScale(isOpen ? 0.0f : 1.0f); // 開いている間はプレイヤーを動かさない
	status_.clear();
	if (!isOpen) {
		GameInput::CancelRebind();
	}
}

// ===== 決める =====
void KeyConfigTest::Submit() {
	const Item& item = items_[cursor_];
	switch (item.kind) {
	case Item::Kind::Binding: {
		// その行の1つ目の割り当てを入れ替える（無ければ足す）
		const std::string name = GameInput::GetMap().GetDesc(item.action).name;
		GameInput::StartRebind(name, item.row, FindFirstBinding(item.action, item.row));
		status_ = "割り当てるキー・ボタンを押してください（Esc でやめる）";
		break;
	}
	case Item::Kind::Reset:
		GameInput::GetMap().ResetAllToDefault();
		status_ = "既定に戻しました（残すなら保存）";
		break;
	case Item::Kind::Revert:
		GameInput::RevertBindings();
		status_ = "保存した割り当てに戻しました";
		break;
	case Item::Kind::Save:
		status_ = GameInput::SaveBindings() ? "保存しました" : "保存できませんでした（ログを見る）";
		break;
	}
}

// ===== その行の1つ目の割り当て =====
int KeyConfigTest::FindFirstBinding(InputActionId action, InputBindingRow row) const {
	const InputActionMap& map = GameInput::GetMap();
	const std::vector<InputBinding>& bindings = map.GetBindings(action);
	for (int i = 0; i < static_cast<int>(bindings.size()); ++i) {
		if (InputBindingRows::Of(map.GetDesc(action).type, bindings[i]) == row) {
			return i;
		}
	}
	return -1;
}


//=============================================================================
// 文字
//=============================================================================
// ===== 全部書き直す =====
void KeyConfigTest::Refresh() {
	SetText(titleText_, isOpen_, std::format("キーコンフィグ{}（Tab で閉じる）", GameInput::IsBindingsDirty() ? " *" : ""));
	for (size_t i = 0; i < rowTexts_.size() && i < items_.size(); ++i) {
		const bool isSelected = (static_cast<int>(i) == cursor_);
		SetText(rowTexts_[i], isOpen_, std::format("{} {}", isSelected ? ">" : " ", MakeItemText(items_[i])), isSelected ? kSelectedColor : kNormalColor);
	}
	// 閉じている間は、開き方とジャンプのキーだけ出す（GetDisplayText の確かめ）
	const std::string closedText = std::format("{} でキーコンフィグ / {} でジャンプ", GameInput::GetDisplayText("Menu.Open"), GameInput::GetDisplayText("Jump"));
	SetText(statusText_, true, isOpen_ ? status_ : closedText);
}

// ===== 項目1つの文字 =====
std::string KeyConfigTest::MakeItemText(const Item& item) const {
	switch (item.kind) {
	case Item::Kind::Reset:
		return "全部を既定に戻す";
	case Item::Kind::Revert:
		return "保存した割り当てに戻す";
	case Item::Kind::Save:
		return "保存する";
	case Item::Kind::Binding:
		break;
	}
	const InputActionMap& map = GameInput::GetMap();
	const InputActionDesc& desc = map.GetDesc(item.action);
	std::string text = desc.label;
	if (item.row != InputBindingRow::All) {
		text += std::format("（{}）", RowLabel(item.row));
	}
	text += ": ";
	// 拾っている行
	if (GameInput::IsRebinding() && GameInput::GetRebindAction() == item.action && items_[cursor_].row == item.row && &items_[cursor_] == &item) {
		return text + "押してください…";
	}
	// その行の割り当てと、ぶつかり
	const std::vector<InputBinding>& bindings = map.GetBindings(item.action);
	bool isFirst = true;
	std::string conflict;
	for (int i = 0; i < static_cast<int>(bindings.size()); ++i) {
		if (InputBindingRows::Of(desc.type, bindings[i]) != item.row) {
			continue;
		}
		text += std::format("{}{}", isFirst ? "" : " / ", InputBindingText::ToDisplayString(bindings[i]));
		isFirst = false;
		const std::vector<InputBindingRef> conflicts = map.FindConflicts(item.action, i);
		if (!conflicts.empty() && conflict.empty()) {
			conflict = std::format("  ！「{}」とぶつかる", map.GetDesc(conflicts.front().action).label);
		}
	}
	if (isFirst) {
		text += "（なし）";
	}
	return text + conflict;
}
