#include "InputRebinder.h"

#include <cmath>

#include <Windows.h>
#include <dinput.h>

namespace {
constexpr float kAxisThreshold = 0.5f;       // スティック・トリガーを、この量より倒したら拾う
constexpr float kMouseMoveThreshold = 40.0f; // マウスを1フレームにこの数より動かしたら拾う（ボタンへ動かす途中で拾わないように大きめ）
constexpr InputControl kCancelKey = {InputDevice::Key, DIK_ESCAPE}; // やめるキー
} // namespace


//=============================================================================
// 始める・やめる
//=============================================================================
// ===== 始める =====
void InputRebinder::Start(const InputActionMap& map, InputActionId id, int index) {
	map_ = &map;
	action_ = id;
	index_ = index;
	accepts_ = map.GetAcceptedDevices(id);
	// 向きは元の割り当てから引き継ぐ（Vector2 の「上」の割り当てを入れ替えたら、新しいキーも「上」）
	const std::vector<InputBinding>& bindings = map.GetBindings(id);
	direction_ = (index >= 0 && index < static_cast<int>(bindings.size())) ? bindings[index].direction : InputDirection::PositiveX;
	isFirstUpdate_ = true;
	pendingModifierKey_.reset();
	pendingModifiers_ = InputModifiers::None;
	wasDown_.assign(InputBindingText::AllControls().size(), false);
	captured_.reset();
}

// ===== やめる =====
void InputRebinder::Cancel() {
	map_ = nullptr;
	pendingModifierKey_.reset();
}

// ===== 拾い終える =====
void InputRebinder::Finish(const InputBinding& binding) {
	captured_ = binding;
	map_ = nullptr;
	pendingModifierKey_.reset();
}


//=============================================================================
// 毎フレーム
//=============================================================================
InputRebinder::Result InputRebinder::Update() {
	if (map_ == nullptr) {
		return Result::Idle;
	}
	const std::span<const InputControl> controls = InputBindingText::AllControls();

	// --- 今押している物 ---
	std::vector<bool> isDown(controls.size(), false);
	for (size_t i = 0; i < controls.size(); ++i) {
		isDown[i] = IsDown(controls[i]);
	}

	// --- 始めた最初のフレームは、今押している物を覚えるだけ ---
	// 覚えた物は「前のフレームに押していた」になるので、離して押し直すまで拾わない（Start を押したクリック・Enter など）
	// 全部を離すまで待つ形にしないのは、押しっぱなしに見えるキーがあるため（日本語のキーボードの半角/全角などは、DirectInput に離した知らせが来ないことがある）
	if (isFirstUpdate_) {
		isFirstUpdate_ = false;
		wasDown_ = isDown;
		return Result::Listening;
	}

	// --- 新しく押した物を探す ---
	// ボタンの Action はボタンを先に、量の Action は軸を先に見る（トリガーを引いたら、量の Action は "LeftTriggerAmount" を拾う）
	const bool isButtonAction = map_->GetDesc(action_).type == InputActionType::Button;
	const InputModifiers held = InputActionMap::ReadModifiers(map_->GetGate());
	for (const bool isAxisPass : {!isButtonAction, isButtonAction}) {
		for (size_t i = 0; i < controls.size(); ++i) {
			const InputControl& control = controls[i];
			if (control.IsAxis() != isAxisPass || !isDown[i] || wasDown_[i]) {
				continue;
			}
			if (control == kCancelKey) {
				Cancel();
				return Result::Canceled;
			}
			if (!Contains(accepts_, control.device)) {
				continue;
			}
			// 修飾キーは、ほかを押すかもしれないので、離すまで待つ
			const InputModifiers own = (control.device == InputDevice::Key) ? InputBindingText::ModifierOfKey(control.code) : InputModifiers::None;
			if (own != InputModifiers::None) {
				pendingModifierKey_ = control;
				pendingModifiers_ = held & ~own;
				continue;
			}
			InputBinding binding = {control, held, direction_};
			// ボタンの Action に軸（ホイールなど）を割り当てるときは、動かした向きを使う（ホイールの下で「次へ」など）
			if (isButtonAction && control.IsAxis()) {
				binding.direction = (InputActionMap::ReadControl(control, map_->GetGate(), map_->GetGamepadIndex()) < 0.0f) ? InputDirection::NegativeX : InputDirection::PositiveX;
			}
			Finish(binding);
			return Result::Captured;
		}
	}

	// --- 修飾キーだけを押して、ほかを押さずに離した：そのキー1つを拾う ---
	if (pendingModifierKey_ && !IsDown(*pendingModifierKey_)) {
		Finish({*pendingModifierKey_, pendingModifiers_, direction_});
		return Result::Captured;
	}

	wasDown_ = isDown;
	return Result::Listening;
}


//=============================================================================
// 読む
//=============================================================================
// ===== 拾うときの決まりで「押している」か =====
bool InputRebinder::IsDown(const InputControl& control) const {
	const float value = InputActionMap::ReadControl(control, map_->GetGate(), map_->GetGamepadIndex());
	switch (control.device) {
	case InputDevice::MouseMove:
		return std::abs(value) >= kMouseMoveThreshold;
	case InputDevice::MouseWheel:
		return value != 0.0f;
	case InputDevice::PadAxis:
		return std::abs(value) >= kAxisThreshold;
	default:
		return value >= kAxisThreshold; // ボタン（0 か 1）
	}
}
