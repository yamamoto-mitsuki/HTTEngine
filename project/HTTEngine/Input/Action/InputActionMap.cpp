#include "InputActionMap.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <utility>

#include <externals/nlohmann/json.hpp>

#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Diagnostics/LogManager.h"

namespace {
using json = nlohmann::json;

constexpr float kPressThreshold = 0.5f; // 軸をボタンとして読むとき・Value と Vector2 を「押している」とするとき、この量から
constexpr int kFileVersion = 1;         // 上書きのファイルの形の版（形を変えたら上げる）

// 向きが - か（-X・-Y）
bool IsNegative(InputDirection direction) { return direction == InputDirection::NegativeX || direction == InputDirection::NegativeY; }
// 向きが Y か（+Y・-Y）
bool IsVertical(InputDirection direction) { return direction == InputDirection::PositiveY || direction == InputDirection::NegativeY; }
} // namespace


//=============================================================================
// 作る・登録
//=============================================================================
InputActionMap::InputActionMap(InputGate gate, InputModifierMatch modifierMatch, InputDeviceMask allowedDevices)
    : gate_(gate), modifierMatch_(modifierMatch), allowedDevices_(allowedDevices) {}

// ===== Action を足す =====
InputActionId InputActionMap::AddAction(InputActionDesc desc) {
	HTT_ASSERT_MSG(!desc.name.empty(), "Input Action の名前が空です");
	const InputActionId existing = Find(desc.name);
	if (existing.IsValid()) {
		LogManager::Warning("[InputActionMap] 同じ名前の Action がもうあります: " + desc.name);
		return existing;
	}
	Action action;
	action.bindings = desc.defaults;
	action.saved = desc.defaults;
	action.desc = std::move(desc);
	actions_.push_back(std::move(action));
	return {static_cast<uint32_t>(actions_.size() - 1)};
}

// ===== 名前から番号 =====
InputActionId InputActionMap::Find(std::string_view name) const {
	for (size_t i = 0; i < actions_.size(); ++i) {
		if (actions_[i].desc.name == name) {
			return {static_cast<uint32_t>(i)};
		}
	}
	return {};
}

const InputActionDesc& InputActionMap::GetDesc(InputActionId id) const {
	const Action* action = Get(id);
	HTT_ASSERT_MSG(action != nullptr, "無効な Input Action の番号です");
	return action->desc;
}

// ===== 割り当てられる入力 =====
InputDeviceMask InputActionMap::GetAcceptedDevices(InputActionId id) const {
	const Action* action = Get(id);
	if (action == nullptr) {
		return InputDeviceMask::None;
	}
	InputDeviceMask accepts = action->desc.accepts;
	if (accepts == InputDeviceMask::None) {
		accepts = (action->desc.type == InputActionType::Button) ? InputDeviceMask::Buttons : InputDeviceMask::All;
	}
	return accepts & allowedDevices_;
}

const InputActionMap::Action* InputActionMap::Get(InputActionId id) const {
	if (!id.IsValid() || id.index >= actions_.size()) {
		return nullptr;
	}
	return &actions_[id.index];
}


//=============================================================================
// 毎フレーム
//=============================================================================
// ===== 入力を読んで、全部の Action の状態を決める =====
void InputActionMap::Update() {
	const InputModifiers held = ReadModifiers(gate_);
	for (Action& action : actions_) {
		// --- 割り当てを全部見て、足す ---
		bool isAnyDown = false;
		Vector2 sum = {0.0f, 0.0f};
		for (const InputBinding& binding : action.bindings) {
			if (binding.IsEmpty() || !Contains(allowedDevices_, binding.control.device) || !MatchesModifiers(binding, held)) {
				continue;
			}
			const float raw = ReadControl(binding.control, gate_, gamepadIndex_);
			const float signedValue = IsNegative(binding.direction) ? -raw : raw;
			switch (action.desc.type) {
			case InputActionType::Button:
				// ボタンは押していれば 1。軸は、割り当ての向きに 0.5 以上で押した（ホイールの上だけ、など）
				if ((binding.control.IsAxis() ? signedValue : raw) >= kPressThreshold) {
					isAnyDown = true;
				}
				break;
			case InputActionType::Value:
				sum.x += signedValue;
				break;
			case InputActionType::Vector2:
				(IsVertical(binding.direction) ? sum.y : sum.x) += signedValue;
				break;
			}
		}

		// --- 種類ごとに、量と「押しているか」を決める ---
		bool isDown = false;
		switch (action.desc.type) {
		case InputActionType::Button:
			action.value = {isAnyDown ? 1.0f : 0.0f, 0.0f};
			isDown = isAnyDown;
			break;
		case InputActionType::Value:
			action.value = {sum.x, 0.0f};
			isDown = std::abs(sum.x) >= kPressThreshold;
			break;
		case InputActionType::Vector2:
			// キーの斜め（長さ √2）・キーとスティックを一緒に倒したとき、長さを 1 までにする
			if (action.desc.clampLength && LengthSq(sum) > 1.0f) {
				sum = Normalize(sum);
			}
			action.value = sum;
			isDown = Length(sum) >= kPressThreshold;
			break;
		}
		action.wasDown = action.isDown;
		action.isDown = isDown;

		// --- 一度離した（前のフレームも今も押していない）ら、また効かせる ---
		// 前のフレームも見るのは、離したフレームに「離した瞬間」を出さないため
		if (action.waitRelease && !action.isDown && !action.wasDown) {
			action.waitRelease = false;
		}
	}
}

// ===== 止める・戻す =====
void InputActionMap::SetEnabled(bool enabled) {
	if (enabled && !isEnabled_) {
		// 止めている間に押した物（割り当てを拾ったキーなど）が、戻した瞬間に効かないように
		for (Action& action : actions_) {
			action.waitRelease = true;
		}
	}
	isEnabled_ = enabled;
}


//=============================================================================
// 聞く
//=============================================================================
bool InputActionMap::IsPressed(InputActionId id) const {
	const Action* action = Get(id);
	return action && IsActive(*action) && action->isDown;
}
bool InputActionMap::IsTriggered(InputActionId id) const {
	const Action* action = Get(id);
	return action && IsActive(*action) && action->isDown && !action->wasDown;
}
bool InputActionMap::IsReleased(InputActionId id) const {
	const Action* action = Get(id);
	return action && IsActive(*action) && !action->isDown && action->wasDown;
}
float InputActionMap::GetValue(InputActionId id) const {
	const Action* action = Get(id);
	if (action == nullptr || !IsActive(*action)) {
		return 0.0f;
	}
	if (action->desc.type == InputActionType::Vector2) {
		return Length(action->value);
	}
	return action->value.x;
}
Vector2 InputActionMap::GetVector2(InputActionId id) const {
	const Action* action = Get(id);
	if (action == nullptr || !IsActive(*action)) {
		return {0.0f, 0.0f};
	}
	return action->value;
}


//=============================================================================
// 入力を読む
//=============================================================================
// ===== 入力1つの今の値 =====
float InputActionMap::ReadControl(const InputControl& control, InputGate gate, int gamepadIndex) {
	switch (control.device) {
	case InputDevice::None:
		return 0.0f;
	case InputDevice::Key:
		return InputManager::IsKeyPressed(static_cast<uint8_t>(control.code), gate) ? 1.0f : 0.0f;
	case InputDevice::MouseButton:
		return InputManager::IsMouseButtonPressed(static_cast<int>(control.code), gate) ? 1.0f : 0.0f;
	case InputDevice::MouseMove:
		if (static_cast<InputMouseMove>(control.code) == InputMouseMove::Y) {
			return static_cast<float>(InputManager::GetMouseDeltaY(gate));
		}
		return static_cast<float>(InputManager::GetMouseDeltaX(gate));
	case InputDevice::MouseWheel:
		return static_cast<float>(InputManager::GetMouseDeltaWheel(gate)) / static_cast<float>(WHEEL_DELTA); // 1 目盛りで 1
	case InputDevice::PadButton:
		return InputManager::IsButtonPressed(control.code, gamepadIndex) ? 1.0f : 0.0f;
	case InputDevice::PadAxis:
		switch (static_cast<InputPadAxis>(control.code)) {
		case InputPadAxis::LeftStickX:
			return InputManager::GetLeftStickX(gamepadIndex);
		case InputPadAxis::LeftStickY:
			return InputManager::GetLeftStickY(gamepadIndex);
		case InputPadAxis::RightStickX:
			return InputManager::GetRightStickX(gamepadIndex);
		case InputPadAxis::RightStickY:
			return InputManager::GetRightStickY(gamepadIndex);
		case InputPadAxis::LeftTrigger:
			return InputManager::GetLeftTrigger(gamepadIndex);
		case InputPadAxis::RightTrigger:
			return InputManager::GetRightTrigger(gamepadIndex);
		}
		return 0.0f;
	}
	return 0.0f;
}

// ===== 今押している修飾キー =====
InputModifiers InputActionMap::ReadModifiers(InputGate gate) {
	InputModifiers held = InputModifiers::None;
	if (InputManager::IsKeyPressed(DIK_LCONTROL, gate) || InputManager::IsKeyPressed(DIK_RCONTROL, gate)) {
		held |= InputModifiers::Ctrl;
	}
	if (InputManager::IsKeyPressed(DIK_LSHIFT, gate) || InputManager::IsKeyPressed(DIK_RSHIFT, gate)) {
		held |= InputModifiers::Shift;
	}
	if (InputManager::IsKeyPressed(DIK_LMENU, gate) || InputManager::IsKeyPressed(DIK_RMENU, gate)) {
		held |= InputModifiers::Alt;
	}
	return held;
}

// ===== 修飾キーが合っているか =====
bool InputActionMap::MatchesModifiers(const InputBinding& binding, InputModifiers held) const {
	// 割り当ての入力そのものが修飾キー（"Key.LeftShift" でダッシュ など）なら、それを押していることは数えない
	const InputModifiers own = (binding.control.device == InputDevice::Key) ? InputBindingText::ModifierOfKey(binding.control.code) : InputModifiers::None;
	const InputModifiers others = held & ~own;
	if (modifierMatch_ == InputModifierMatch::Exact) {
		return others == binding.modifiers;
	}
	return (others & binding.modifiers) == binding.modifiers;
}


//=============================================================================
// 割り当て
//=============================================================================
const std::vector<InputBinding>& InputActionMap::GetBindings(InputActionId id) const {
	const Action* action = Get(id);
	HTT_ASSERT_MSG(action != nullptr, "無効な Input Action の番号です");
	return action->bindings;
}

// ===== 書き換えてよい Action（無効な番号・変えられない Action なら nullptr）=====
InputActionMap::Action* InputActionMap::GetEditable(InputActionId id) {
	const Action* action = Get(id);
	if (action == nullptr || action->desc.isFixed) {
		return nullptr;
	}
	return &actions_[id.index];
}

// ===== 全部入れ替える =====
void InputActionMap::SetBindings(InputActionId id, std::vector<InputBinding> bindings) {
	Action* action = GetEditable(id);
	if (action == nullptr) {
		return;
	}
	action->bindings = std::move(bindings);
	action->waitRelease = true; // 新しい割り当てのキーを今押していても、一度離すまで効かせない
}

// ===== 1つ入れ替える・足す =====
void InputActionMap::SetBinding(InputActionId id, int index, const InputBinding& binding) {
	Action* action = GetEditable(id);
	if (action == nullptr || index < 0) {
		return;
	}
	if (index < static_cast<int>(action->bindings.size())) {
		action->bindings[index] = binding;
	} else {
		action->bindings.push_back(binding);
	}
	action->waitRelease = true;
}

// ===== 1つ消す =====
void InputActionMap::RemoveBinding(InputActionId id, int index) {
	Action* action = GetEditable(id);
	if (action == nullptr || index < 0 || index >= static_cast<int>(action->bindings.size())) {
		return;
	}
	action->bindings.erase(action->bindings.begin() + index);
}

// ===== 既定に戻す =====
void InputActionMap::ResetToDefault(InputActionId id) {
	const Action* action = Get(id);
	if (action) {
		SetBindings(id, action->desc.defaults);
	}
}
void InputActionMap::ResetAllToDefault() {
	for (Action& action : actions_) {
		action.bindings = action.desc.defaults;
		action.waitRelease = true;
	}
}
bool InputActionMap::IsDefault(InputActionId id) const {
	const Action* action = Get(id);
	return action && action->bindings == action->desc.defaults;
}

// ===== 画面に出す文字 =====
std::string InputActionMap::GetDisplayText(InputActionId id) const {
	const Action* action = Get(id);
	if (action == nullptr) {
		return {};
	}
	for (const InputBinding& binding : action->bindings) {
		if (!binding.IsEmpty()) {
			return InputBindingText::ToDisplayString(binding);
		}
	}
	return {};
}


//=============================================================================
// ぶつかり
//=============================================================================
// ===== 一覧 =====
std::vector<InputBindingRef> InputActionMap::FindConflicts(InputActionId id, int index) const {
	std::vector<InputBindingRef> conflicts;
	const Action* self = Get(id);
	if (self == nullptr || index < 0 || index >= static_cast<int>(self->bindings.size())) {
		return conflicts;
	}
	const InputBinding& binding = self->bindings[index];
	if (binding.IsEmpty()) {
		return conflicts;
	}
	for (size_t a = 0; a < actions_.size(); ++a) {
		const Action& other = actions_[a];
		for (size_t b = 0; b < other.bindings.size(); ++b) {
			if (a == id.index && static_cast<int>(b) == index) {
				continue; // 自分
			}
			if (Overlaps(*self, binding, other, other.bindings[b])) {
				conflicts.push_back({{static_cast<uint32_t>(a)}, static_cast<int>(b)});
			}
		}
	}
	return conflicts;
}

// ===== 2つの割り当てが、同時に効いてしまうか =====
bool InputActionMap::Overlaps(const Action& a, const InputBinding& ba, const Action& b, const InputBinding& bb) const {
	// --- 入力が違えば、ぶつからない ---
	if (ba.control != bb.control || ba.IsEmpty()) {
		return false;
	}
	// --- 効く場所が重ならなければ、ぶつからない（空はどこでも効く）---
	if (!a.desc.context.empty() && !b.desc.context.empty() && a.desc.context != b.desc.context) {
		return false;
	}
	// --- 軸を、ボタンとして逆の向きに使い分けている（ホイールの上と下）なら、ぶつからない ---
	if (ba.control.IsAxis() && a.desc.type == InputActionType::Button && b.desc.type == InputActionType::Button && IsNegative(ba.direction) != IsNegative(bb.direction)) {
		return false;
	}
	// --- 修飾キー ---
	if (modifierMatch_ == InputModifierMatch::Exact) {
		return ba.modifiers == bb.modifiers;
	}
	// AtLeast：片方の修飾キーが、もう片方に全部入っていれば、両方効く時がある（"Space" と "Shift+Space"）
	return (ba.modifiers & bb.modifiers) == ba.modifiers || (ba.modifiers & bb.modifiers) == bb.modifiers;
}


//=============================================================================
// 保存（人ごとの上書き）
//=============================================================================
// ===== 読む =====
bool InputActionMap::LoadOverrides(const std::filesystem::path& path) {
	// 前に読んだ上書きが残らないように、全部既定から始める
	for (Action& action : actions_) {
		action.bindings = action.desc.defaults;
		action.waitRelease = true;
	}
	auto markSaved = [this] {
		for (Action& action : actions_) {
			action.saved = action.bindings;
		}
	};

	std::ifstream ifs(path);
	if (!ifs) {
		markSaved(); // まだ保存していない：既定のまま
		return false;
	}
	// 例外を投げない版のパース
	const json root = json::parse(ifs, nullptr, false);
	if (root.is_discarded() || !root.is_object()) {
		LogManager::Error("[InputActionMap] 割り当てのファイルを読めません（既定のままにします）: " + path.string());
		markSaved();
		return false;
	}
	if (root.value("version", 0) != kFileVersion) {
		LogManager::Warning("[InputActionMap] 割り当てのファイルの版が違います: " + path.string());
	}

	const auto bindings = root.find("bindings");
	if (bindings != root.end() && bindings->is_object()) {
		for (const auto& [name, list] : bindings->items()) {
			const InputActionId id = Find(name);
			if (!id.IsValid() || !list.is_array() || actions_[id.index].desc.isFixed) {
				LogManager::Warning("[InputActionMap] 知らない（変えられない）Action を飛ばしました: " + name);
				continue;
			}
			// --- 割り当ての文字を1つずつ読む（読めない物だけ飛ばす）---
			std::vector<InputBinding> parsed;
			for (const json& text : list) {
				const std::optional<InputBinding> binding = text.is_string() ? InputBindingText::Parse(text.get<std::string>()) : std::nullopt;
				if (!binding) {
					LogManager::Warning("[InputActionMap] 読めない割り当てを飛ばしました: " + name + " " + text.dump());
					continue;
				}
				parsed.push_back(*binding);
			}
			actions_[id.index].bindings = std::move(parsed);
		}
	}
	markSaved();
	return true;
}

// ===== 書く =====
bool InputActionMap::SaveOverrides(const std::filesystem::path& path) {
	// --- 既定から変えた Action だけ（割り当てを全部外したのも、空の配列で書く）---
	json bindings = json::object();
	for (const Action& action : actions_) {
		if (action.bindings == action.desc.defaults) {
			continue;
		}
		json list = json::array();
		for (const InputBinding& binding : action.bindings) {
			list.push_back(InputBindingText::ToString(binding));
		}
		bindings[action.desc.name] = std::move(list);
	}
	const json root = {{"version", kFileVersion}, {"bindings", std::move(bindings)}};

	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	std::ofstream ofs(path);
	if (!ofs) {
		LogManager::Error("[InputActionMap] 割り当てのファイルを書けません: " + path.string());
		return false;
	}
	ofs << std::setw(4) << root << std::endl;
	for (Action& action : actions_) {
		action.saved = action.bindings;
	}
	LogManager::Log("[InputActionMap] 保存しました: " + path.string());
	return true;
}

// ===== 保存した状態から変えたか =====
bool InputActionMap::IsDirty() const {
	for (const Action& action : actions_) {
		if (action.bindings != action.saved) {
			return true;
		}
	}
	return false;
}

// ===== 保存した状態に戻す =====
void InputActionMap::RevertToSaved() {
	for (Action& action : actions_) {
		if (action.bindings != action.saved) {
			action.bindings = action.saved;
			action.waitRelease = true;
		}
	}
}
