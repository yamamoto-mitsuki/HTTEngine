#include "InputBinding.h"

#include <array>
#include <vector>

#include <Windows.h>
#include <dinput.h>

#include "HTTEngine/Input/XInputGamepad.h"

namespace {
//=============================================================================
// 名前の一覧
//=============================================================================
// 保存に使う名前。変えると、保存した割り当てが読めなくなる（足すのはよい）
// キーは DirectInput の番号（キーの場所）で決まる。日本語の配列の「@」は英語の配列の「[」の場所なので LeftBracket になる
struct NamedControl {
	InputControl control;
	std::string_view name;
};

// --- 作るときの短い書き方 ---
constexpr InputControl Key(uint32_t dik) { return {InputDevice::Key, dik}; }
constexpr InputControl MouseButton(uint32_t button) { return {InputDevice::MouseButton, button}; }
constexpr InputControl MouseMove(InputMouseMove axis) { return {InputDevice::MouseMove, static_cast<uint32_t>(axis)}; }
constexpr InputControl MouseWheel() { return {InputDevice::MouseWheel, 0}; }
constexpr InputControl PadButton(uint32_t button) { return {InputDevice::PadButton, button}; }
constexpr InputControl PadAxis(InputPadAxis axis) { return {InputDevice::PadAxis, static_cast<uint32_t>(axis)}; }

constexpr auto kControlNames = std::to_array<NamedControl>({
    // ===== キーボード =====
    // --- 文字 ---
    {Key(DIK_A), "Key.A"}, {Key(DIK_B), "Key.B"}, {Key(DIK_C), "Key.C"}, {Key(DIK_D), "Key.D"}, {Key(DIK_E), "Key.E"},
    {Key(DIK_F), "Key.F"}, {Key(DIK_G), "Key.G"}, {Key(DIK_H), "Key.H"}, {Key(DIK_I), "Key.I"}, {Key(DIK_J), "Key.J"},
    {Key(DIK_K), "Key.K"}, {Key(DIK_L), "Key.L"}, {Key(DIK_M), "Key.M"}, {Key(DIK_N), "Key.N"}, {Key(DIK_O), "Key.O"},
    {Key(DIK_P), "Key.P"}, {Key(DIK_Q), "Key.Q"}, {Key(DIK_R), "Key.R"}, {Key(DIK_S), "Key.S"}, {Key(DIK_T), "Key.T"},
    {Key(DIK_U), "Key.U"}, {Key(DIK_V), "Key.V"}, {Key(DIK_W), "Key.W"}, {Key(DIK_X), "Key.X"}, {Key(DIK_Y), "Key.Y"},
    {Key(DIK_Z), "Key.Z"},
    // --- 数字（上の段）---
    {Key(DIK_0), "Key.0"}, {Key(DIK_1), "Key.1"}, {Key(DIK_2), "Key.2"}, {Key(DIK_3), "Key.3"}, {Key(DIK_4), "Key.4"},
    {Key(DIK_5), "Key.5"}, {Key(DIK_6), "Key.6"}, {Key(DIK_7), "Key.7"}, {Key(DIK_8), "Key.8"}, {Key(DIK_9), "Key.9"},
    // --- ファンクションキー ---
    {Key(DIK_F1), "Key.F1"}, {Key(DIK_F2), "Key.F2"}, {Key(DIK_F3), "Key.F3"}, {Key(DIK_F4), "Key.F4"},
    {Key(DIK_F5), "Key.F5"}, {Key(DIK_F6), "Key.F6"}, {Key(DIK_F7), "Key.F7"}, {Key(DIK_F8), "Key.F8"},
    {Key(DIK_F9), "Key.F9"}, {Key(DIK_F10), "Key.F10"}, {Key(DIK_F11), "Key.F11"}, {Key(DIK_F12), "Key.F12"},
    // --- 修飾キー・空白など ---
    {Key(DIK_LSHIFT), "Key.LeftShift"}, {Key(DIK_RSHIFT), "Key.RightShift"},
    {Key(DIK_LCONTROL), "Key.LeftCtrl"}, {Key(DIK_RCONTROL), "Key.RightCtrl"},
    {Key(DIK_LMENU), "Key.LeftAlt"}, {Key(DIK_RMENU), "Key.RightAlt"},
    {Key(DIK_ESCAPE), "Key.Escape"}, {Key(DIK_TAB), "Key.Tab"}, {Key(DIK_CAPITAL), "Key.CapsLock"},
    {Key(DIK_SPACE), "Key.Space"}, {Key(DIK_RETURN), "Key.Enter"}, {Key(DIK_BACK), "Key.Backspace"},
    // --- 矢印・編集 ---
    {Key(DIK_UP), "Key.Up"}, {Key(DIK_DOWN), "Key.Down"}, {Key(DIK_LEFT), "Key.Left"}, {Key(DIK_RIGHT), "Key.Right"},
    {Key(DIK_INSERT), "Key.Insert"}, {Key(DIK_DELETE), "Key.Delete"}, {Key(DIK_HOME), "Key.Home"}, {Key(DIK_END), "Key.End"},
    {Key(DIK_PRIOR), "Key.PageUp"}, {Key(DIK_NEXT), "Key.PageDown"},
    // --- 記号（英語の配列の名前）---
    {Key(DIK_MINUS), "Key.Minus"}, {Key(DIK_EQUALS), "Key.Equals"}, {Key(DIK_LBRACKET), "Key.LeftBracket"}, {Key(DIK_RBRACKET), "Key.RightBracket"},
    {Key(DIK_BACKSLASH), "Key.Backslash"}, {Key(DIK_SEMICOLON), "Key.Semicolon"}, {Key(DIK_APOSTROPHE), "Key.Apostrophe"}, {Key(DIK_GRAVE), "Key.Grave"},
    {Key(DIK_COMMA), "Key.Comma"}, {Key(DIK_PERIOD), "Key.Period"}, {Key(DIK_SLASH), "Key.Slash"},
    // --- 日本語の配列だけにあるキー ---
    {Key(DIK_YEN), "Key.Yen"}, {Key(DIK_ABNT_C1), "Key.Ro"}, {Key(DIK_KANA), "Key.Kana"}, {Key(DIK_CONVERT), "Key.Convert"}, {Key(DIK_NOCONVERT), "Key.NoConvert"},
    // --- テンキー ---
    {Key(DIK_NUMPAD0), "Key.Numpad0"}, {Key(DIK_NUMPAD1), "Key.Numpad1"}, {Key(DIK_NUMPAD2), "Key.Numpad2"}, {Key(DIK_NUMPAD3), "Key.Numpad3"}, {Key(DIK_NUMPAD4), "Key.Numpad4"},
    {Key(DIK_NUMPAD5), "Key.Numpad5"}, {Key(DIK_NUMPAD6), "Key.Numpad6"}, {Key(DIK_NUMPAD7), "Key.Numpad7"}, {Key(DIK_NUMPAD8), "Key.Numpad8"}, {Key(DIK_NUMPAD9), "Key.Numpad9"},
    {Key(DIK_ADD), "Key.NumpadAdd"}, {Key(DIK_SUBTRACT), "Key.NumpadSubtract"}, {Key(DIK_MULTIPLY), "Key.NumpadMultiply"}, {Key(DIK_DIVIDE), "Key.NumpadDivide"},
    {Key(DIK_DECIMAL), "Key.NumpadDecimal"}, {Key(DIK_NUMPADENTER), "Key.NumpadEnter"},
    // --- そのほか ---
    {Key(DIK_SYSRQ), "Key.PrintScreen"}, {Key(DIK_SCROLL), "Key.ScrollLock"}, {Key(DIK_PAUSE), "Key.Pause"}, {Key(DIK_NUMLOCK), "Key.NumLock"},

    // ===== マウス =====
    {MouseButton(0), "Mouse.Left"}, {MouseButton(1), "Mouse.Right"}, {MouseButton(2), "Mouse.Middle"}, {MouseButton(3), "Mouse.X1"}, {MouseButton(4), "Mouse.X2"},
    {MouseMove(InputMouseMove::X), "Mouse.MoveX"}, {MouseMove(InputMouseMove::Y), "Mouse.MoveY"}, {MouseWheel(), "Mouse.Wheel"},

    // ===== パッド（Xbox の並び）=====
    {PadButton(GamepadButton::A), "Pad.A"}, {PadButton(GamepadButton::B), "Pad.B"}, {PadButton(GamepadButton::X), "Pad.X"}, {PadButton(GamepadButton::Y), "Pad.Y"},
    {PadButton(GamepadButton::LeftShoulder), "Pad.LeftShoulder"}, {PadButton(GamepadButton::RightShoulder), "Pad.RightShoulder"},
    {PadButton(GamepadButton::LeftTrigger), "Pad.LeftTrigger"}, {PadButton(GamepadButton::RightTrigger), "Pad.RightTrigger"},
    {PadButton(GamepadButton::LeftThumb), "Pad.LeftThumb"}, {PadButton(GamepadButton::RightThumb), "Pad.RightThumb"},
    {PadButton(GamepadButton::Start), "Pad.Start"}, {PadButton(GamepadButton::Back), "Pad.Back"},
    {PadButton(GamepadButton::DpadUp), "Pad.DpadUp"}, {PadButton(GamepadButton::DpadDown), "Pad.DpadDown"},
    {PadButton(GamepadButton::DpadLeft), "Pad.DpadLeft"}, {PadButton(GamepadButton::DpadRight), "Pad.DpadRight"},
    {PadAxis(InputPadAxis::LeftStickX), "Pad.LeftStickX"}, {PadAxis(InputPadAxis::LeftStickY), "Pad.LeftStickY"},
    {PadAxis(InputPadAxis::RightStickX), "Pad.RightStickX"}, {PadAxis(InputPadAxis::RightStickY), "Pad.RightStickY"},
    {PadAxis(InputPadAxis::LeftTrigger), "Pad.LeftTriggerAmount"}, {PadAxis(InputPadAxis::RightTrigger), "Pad.RightTriggerAmount"},
});

// --- 修飾キーの文字（書く順番もこの順）---
struct ModifierName {
	InputModifiers modifier;
	std::string_view name;
};
constexpr auto kModifierNames = std::to_array<ModifierName>({
    {InputModifiers::Ctrl, "Ctrl"},
    {InputModifiers::Shift, "Shift"},
    {InputModifiers::Alt, "Alt"},
});

// --- 向きの文字（+X は書かない）---
struct DirectionName {
	InputDirection direction;
	std::string_view name;
};
constexpr auto kDirectionNames = std::to_array<DirectionName>({
    {InputDirection::PositiveX, "+X"},
    {InputDirection::NegativeX, "-X"},
    {InputDirection::PositiveY, "+Y"},
    {InputDirection::NegativeY, "-Y"},
});
} // namespace


//=============================================================================
// 入力1つ
//=============================================================================
// ===== 名前 =====
std::string InputBindingText::ControlName(const InputControl& control) {
	for (const NamedControl& entry : kControlNames) {
		if (entry.control == control) {
			return std::string(entry.name);
		}
	}
	return {};
}

// ===== 名前から =====
std::optional<InputControl> InputBindingText::ParseControl(std::string_view name) {
	for (const NamedControl& entry : kControlNames) {
		if (entry.name == name) {
			return entry.control;
		}
	}
	return std::nullopt;
}

// ===== 全部 =====
std::span<const InputControl> InputBindingText::AllControls() {
	// kControlNames から入力だけを抜き出した物（初めて呼ばれたときに1回だけ作る）
	static const std::vector<InputControl> controls = [] {
		std::vector<InputControl> result;
		result.reserve(kControlNames.size());
		for (const NamedControl& entry : kControlNames) {
			result.push_back(entry.control);
		}
		return result;
	}();
	return controls;
}


//=============================================================================
// 割り当て1つ
//=============================================================================
// ===== 文字にする =====
std::string InputBindingText::ToString(const InputBinding& binding) {
	const std::string name = ControlName(binding.control);
	if (name.empty()) {
		return {};
	}
	std::string text;
	// --- 修飾キー（"Ctrl+Shift+"）---
	for (const ModifierName& entry : kModifierNames) {
		if ((binding.modifiers & entry.modifier) != InputModifiers::None) {
			text += entry.name;
			text += '+';
		}
	}
	text += name;
	// --- 向き（+X のときは書かない）---
	if (binding.direction != InputDirection::PositiveX) {
		for (const DirectionName& entry : kDirectionNames) {
			if (entry.direction == binding.direction) {
				text += ':';
				text += entry.name;
			}
		}
	}
	return text;
}

// ===== 文字から =====
std::optional<InputBinding> InputBindingText::Parse(std::string_view text) {
	InputBinding binding;
	// --- 前から「修飾キー+」を外していく（入力の名前に + は入らない）---
	for (size_t plus = text.find('+'); plus != std::string_view::npos; plus = text.find('+')) {
		const std::string_view token = text.substr(0, plus);
		bool isModifier = false;
		for (const ModifierName& entry : kModifierNames) {
			if (entry.name == token) {
				binding.modifiers |= entry.modifier;
				isModifier = true;
			}
		}
		if (!isModifier) {
			break; // ":+X" の + まで来た
		}
		text.remove_prefix(plus + 1);
	}
	// --- 後ろの ":向き" ---
	const size_t colon = text.find(':');
	if (colon != std::string_view::npos) {
		const std::string_view directionName = text.substr(colon + 1);
		bool isFound = false;
		for (const DirectionName& entry : kDirectionNames) {
			if (entry.name == directionName) {
				binding.direction = entry.direction;
				isFound = true;
			}
		}
		if (!isFound) {
			return std::nullopt;
		}
		text = text.substr(0, colon);
	}
	// --- 残りが入力の名前 ---
	const std::optional<InputControl> control = ParseControl(text);
	if (!control) {
		return std::nullopt;
	}
	binding.control = *control;
	return binding;
}

// ===== 画面に出す文字 =====
std::string InputBindingText::ToDisplayString(const InputBinding& binding) {
	std::string name = ControlName(binding.control);
	if (name.empty()) {
		return {};
	}
	constexpr std::string_view kKeyPrefix = "Key.";
	if (name.starts_with(kKeyPrefix)) {
		name.erase(0, kKeyPrefix.size()); // キーは "Z" だけ
	} else {
		name[name.find('.')] = ' '; // "Mouse Right"・"Pad A"
	}
	std::string text;
	for (const ModifierName& entry : kModifierNames) {
		if ((binding.modifiers & entry.modifier) != InputModifiers::None) {
			text += entry.name;
			text += '+';
		}
	}
	return text + name;
}


//=============================================================================
// 修飾キー
//=============================================================================
InputModifiers InputBindingText::ModifierOfKey(uint32_t dik) {
	switch (dik) {
	case DIK_LCONTROL:
	case DIK_RCONTROL:
		return InputModifiers::Ctrl;
	case DIK_LSHIFT:
	case DIK_RSHIFT:
		return InputModifiers::Shift;
	case DIK_LMENU:
	case DIK_RMENU:
		return InputModifiers::Alt;
	default:
		return InputModifiers::None;
	}
}
