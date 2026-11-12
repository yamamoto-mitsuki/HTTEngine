#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>


//=============================================================================
// 入力の割り当て1つ（Input Action の部品）
//=============================================================================
// 「どのキー・ボタン・軸か（InputControl）」と「一緒に押す修飾キー」と「2軸のどちらの向きに足すか」の組。
// 保存は文字で書く（"Ctrl+Key.Z"・"Mouse.Right"・"Key.A:-X"）。名前の一覧は InputBinding.cpp の kControlNames


/// <summary>
/// 入力の出どころ（キーボード・マウス・パッドの、ボタンか軸か）
/// </summary>
enum class InputDevice : uint8_t {
	None,        // 割り当てなし
	Key,         // キーボードのキー（code は DIK_〜。キーの場所で決まる。日本語の配列でも英語の配列の名前で呼ぶ）
	MouseButton, // マウスのボタン（code は 0=左 1=右 2=中 3・4=横）
	MouseMove,   // マウスの動き（code は InputMouseMove）
	MouseWheel,  // マウスのホイール（code は 0。上が+。1 目盛りで 1）
	PadButton,   // パッドのボタン（code は GamepadButton::〜。LT・RT をボタンとして読むのもここ）
	PadAxis,     // パッドのスティック・トリガーの量（code は InputPadAxis）
};

/// <summary>
/// マウスの動きの向き（InputDevice::MouseMove の code）
/// </summary>
enum class InputMouseMove : uint8_t {
	X, // 横の動き（右が+。マウスが数えた数。InputManager::GetMouseDeltaX と同じ）
	Y, // 縦の動き（下が+。InputManager::GetMouseDeltaY と同じ）
};

/// <summary>
/// パッドの軸（InputDevice::PadAxis の code）
/// </summary>
enum class InputPadAxis : uint8_t {
	LeftStickX,   // 左スティックの横（右が+。-1〜1）
	LeftStickY,   // 左スティックの縦（上が+）
	RightStickX,  // 右スティックの横
	RightStickY,  // 右スティックの縦
	LeftTrigger,  // LT を引いた量（0〜1）
	RightTrigger, // RT を引いた量（0〜1）
};

/// <summary>
/// 一緒に押す修飾キー（左右どちらでもよい）。ビットの組
/// </summary>
enum class InputModifiers : uint8_t {
	None = 0,
	Ctrl = 1 << 0,
	Shift = 1 << 1,
	Alt = 1 << 2,
};
constexpr InputModifiers operator|(InputModifiers a, InputModifiers b) { return static_cast<InputModifiers>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b)); }
constexpr InputModifiers operator&(InputModifiers a, InputModifiers b) { return static_cast<InputModifiers>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b)); }
constexpr InputModifiers operator~(InputModifiers a) { return static_cast<InputModifiers>(~static_cast<uint8_t>(a) & 0x07); }
constexpr InputModifiers& operator|=(InputModifiers& a, InputModifiers b) { return a = a | b; }

/// <summary>
/// 2軸（Vector2）の Action で、この割り当ての値をどちらの向きに足すか。1軸（Value）の Action では + か - かだけを見る
/// </summary>
enum class InputDirection : uint8_t {
	PositiveX, // 右（1軸なら +）。書かないときはこれ
	NegativeX, // 左（1軸なら -）
	PositiveY, // 上（1軸なら +）
	NegativeY, // 下（1軸なら -）
};

/// <summary>
/// 入力1つ（どのキー・ボタン・軸か）
/// </summary>
struct InputControl {
	InputDevice device = InputDevice::None;
	uint32_t code = 0; // device ごとの番号（InputDevice のコメント）

	bool operator==(const InputControl&) const = default;
	bool IsEmpty() const { return device == InputDevice::None; }
	// 軸（押す・離すではなく量）か
	bool IsAxis() const { return device == InputDevice::MouseMove || device == InputDevice::MouseWheel || device == InputDevice::PadAxis; }
};

/// <summary>
/// 割り当て1つ（入力1つ＋修飾キー＋足す向き）
/// </summary>
struct InputBinding {
	InputControl control;
	InputModifiers modifiers = InputModifiers::None;            // 一緒に押していないと効かない修飾キー
	InputDirection direction = InputDirection::PositiveX; // 値を足す向き

	bool operator==(const InputBinding&) const = default;
	bool IsEmpty() const { return control.IsEmpty(); }

	// --- コードで既定の割り当てを書くときの短い書き方 ---
	static constexpr InputBinding Key(uint32_t dik, InputModifiers modifiers = InputModifiers::None) { return {{InputDevice::Key, dik}, modifiers}; }
	static constexpr InputBinding Mouse(uint32_t button, InputModifiers modifiers = InputModifiers::None) { return {{InputDevice::MouseButton, button}, modifiers}; }
	static constexpr InputBinding Wheel(InputModifiers modifiers = InputModifiers::None) { return {{InputDevice::MouseWheel, 0}, modifiers}; }
	static constexpr InputBinding Pad(uint32_t button) { return {{InputDevice::PadButton, button}}; }
};


/// <summary>
/// 割り当てと文字の行き来・名前の一覧
/// </summary>
namespace InputBindingText {
// --- 入力1つ ---
// 保存に使う名前（"Key.Z"・"Mouse.Right"・"Pad.A"・"Pad.LeftStickX"）。一覧に無ければ空
std::string ControlName(const InputControl& control);
// 名前から入力1つ（大文字・小文字は区別する）。知らない名前なら空
std::optional<InputControl> ParseControl(std::string_view name);
// 名前を持つ入力の全部（キー・マウス・パッドの順）。割り当てを拾うとき（InputRebinder）に、新しく押した物を探すのに使う
std::span<const InputControl> AllControls();

// --- 割り当て1つ ---
// 保存に使う文字（"Ctrl+Shift+Key.Z"・"Key.A:-X"。向きが +X なら書かない）
std::string ToString(const InputBinding& binding);
// 文字から割り当て。読めなければ空
std::optional<InputBinding> Parse(std::string_view text);
// 画面に出す文字（"Ctrl+Z"・"Mouse Right"・"Pad A"。キーは "Key." を付けない。向きは書かない）
std::string ToDisplayString(const InputBinding& binding);

// --- 修飾キー ---
// そのキーが修飾キー（左右の Ctrl・Shift・Alt）なら、どれか。違えば None
InputModifiers ModifierOfKey(uint32_t dik);
} // namespace InputBindingText
