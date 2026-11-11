#pragma once
#include <array>
#include <cstdint>
#include <type_traits>

#include <externals/magic_enum/magic_enum.hpp>

#include "HTTEngine/Core/EnumTips.h"
#include "HTTEngine/String/FixedString.h"
#include "HTTEngine/Time/EngineTime.h"

// UI の要素の状態（マウスが乗った・押した・選んだ・使えない・出す・消す）で、Timeline を再生する。Entityに付けるComponent（カテゴリ：UI）
// 動かすのは UIStateSystem（Systems/UI/UIStateSystem.h）。マウスの当たりは、同じ Entity の Vector Shape か Image の形

/// <summary>
/// 要素の状態（強い順：Disabled ＞ Pressed ＞ Hover ＞ Selected ＞ Normal。Unity の Selectable と同じ）
/// </summary>
enum class UIElementState {
	Normal,   // 何もしていない
	Hover,    // マウスが乗っている
	Pressed,  // 上で押している
	Selected, // コードで選んだ（UIStateSystem::SetSelected。キーで選ぶメニューなど）
	Disabled, // 使えない（Interactable を外した）
};

inline constexpr size_t kUIElementStateCount = magic_enum::enum_count<UIElementState>(); // UIElementState の数

/// <summary>
/// クリックしたときにすること（コードを書かずに、画面を出す・消す）
/// </summary>
enum class UIClickAction {
	None,           // 何もしない（Script で UIStateSystem::IsClicked を見る）
	OpenScreen,     // Screen の名前の画面を出す
	CloseScreen,    // Screen の名前の画面を消す
	CloseOwnScreen, // この要素が入っている画面を消す
	SwitchScreen,   // この要素が入っている画面を消して、Screen の名前の画面を出す（2つの動きは同時に進む）
};

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(UIClickAction) {
	using enum UIClickAction;
	return MakeEnumTips<UIClickAction>({
	    {None, "何もしない（Script で UIStateSystem::IsClicked を見る）"},
	    {OpenScreen, "Screen の名前の画面を出す"},
	    {CloseScreen, "Screen の名前の画面を消す"},
	    {CloseOwnScreen, "この要素が入っている画面を消す"},
	    {SwitchScreen, "この要素が入っている画面を消して、Screen の名前の画面を出す（2つの動きは同時に進む）"},
	});
}

/// <summary>
/// 要素の状態で Timeline を再生する（Timeline の対象を空にすると、この Entity 自身を動かす）
/// <para>状態の Timeline を選んでいなければ、Normal の Timeline を再生する（元の形へ戻すため）</para>
/// </summary>
struct UIStateComponent {
	bool interactable = true;                                       // 外している間は Disabled（マウスに反応しない）
	bool useMouse = true;                                           // マウスで Hover・Pressed になる・クリックできる
	bool startVisible = true;                                       // 最初から見えている（外すと、最初に Hide の最後の形にする）
	TimeMode timeMode = TimeMode::Real;                             // 再生に使う時間（Real：ポーズ中も動く / Game：ポーズ・スローで止まる）
	std::array<uint32_t, kUIElementStateCount> stateTimelines = {}; // [UIElementState] その状態になったときに再生する .timeline
	uint32_t showTimeline = 0;                                      // UIStateSystem::SetVisible(true) で再生する .timeline
	uint32_t hideTimeline = 0;                                      // UIStateSystem::SetVisible(false) で再生する .timeline
	UIClickAction clickAction = UIClickAction::None;                // クリックしたときにすること
	FixedString<64> screen;                                         // Open Screen・Close Screen・Switch Screen の画面の名前（resources/ui/〇〇.uiscreen の 〇〇）
};

static_assert(std::is_trivially_copyable_v<UIStateComponent>);