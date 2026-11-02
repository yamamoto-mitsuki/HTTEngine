#pragma once
#include <cstdint>
#include <type_traits>

#include "MyEngine/Math/Easing.h"
#include "MyEngine/Time/EngineTime.h"

// 画面（ポーズ画面・クリア画面など）の出し方・消し方。resources/ui/〇〇.uiscreen の一番上の Entity に付ける（カテゴリ：UI）
// 動かすのは UIScreen（UI/UIScreen.h）。UIScreen::Open で出して、UIScreen::Close で消す
// 出し方・消し方は2通り：Timeline（一番上の Entity を再生する Entity にして再生する。要素ごとに好きに動かせる）か、
// Motion（ファイル無しで、一番上の Entity の直下の子を1つずつ同じ動きで動かす。Hierarchy の上から順に、Stagger の秒ずつ遅れて動く）

/// <summary>
/// 出し方・消し方の動き
/// </summary>
enum class UIScreenMotionType {
	None,  // 動かない（すぐ出る・消すときは最後まで出たまま）
	Fade,  // だんだん見えてくる・見えなくなる（Alpha Group の Alpha を動かす）
	Slide, // 画面の外の方から滑り込む・外の方へ出ていく（Translation を動かす）
	Scale, // 小さい所から大きくなる・小さくなって消える（Scale を動かす）
};

/// <summary>
/// 滑り込むときの、画面のどちら側か（出すときは「どこから」、消すときは「どこへ」）
/// </summary>
enum class UIScreenSide {
	Left,
	Right,
	Top,
	Bottom,
};

/// <summary>
/// 出し方・消し方1つ分（UI Screen Component に「出す」「消す」の2つ持つ）
/// </summary>
struct UIScreenMotion {
	UIScreenMotionType type = UIScreenMotionType::Fade;
	UIScreenSide side = UIScreenSide::Bottom; // Slide のとき：どちら側から・どちら側へ
	float distance = 60.0f;                   // Slide のとき：動く長さ（ピクセル。ゲームの画面のつもりで）
	float fromScale = 0.6f;                   // Scale のとき：一番小さいときの大きさ（元の大きさを 1 として）
	bool withFade = true;                     // Slide・Scale のとき：一緒にフェードもする
	float duration = 0.3f;                    // 1つの要素が動ききるまでの秒数（止めない時間で数える）
	Ease::Type ease = Ease::Type::OutCubic;   // 動きの速さの変え方（出すときは Out〜、消すときは In〜 が自然）
};

static_assert(std::is_trivially_copyable_v<UIScreenMotion>);

/// <summary>
/// 画面の出し方・消し方（画面のファイルの一番上の Entity に付ける）
/// </summary>
struct UIScreenComponent {
	bool pauseGame = true;              // 出している間、ゲームを止める（Time::SetGamePaused。中の Script は動く）
	TimeMode timeMode = TimeMode::Real; // 出す・消す動きの時間（Pause Game を入れた画面は、いつも Real）
	uint32_t showTimeline = 0;          // 出すときに再生する .timeline（選べば Show の Motion の代わりに使う。0 なら Motion）
	uint32_t hideTimeline = 0;          // 消すときに再生する .timeline（選べば Hide の Motion の代わりに使う。0 なら Motion）
	UIScreenMotion show;                // 出し方
	UIScreenMotion hide = {.type = UIScreenMotionType::Fade, .duration = 0.2f, .ease = Ease::Type::Linear}; // 消し方
	float showStagger = 0.05f; // 出すとき、要素1つごとに遅らせる秒数（上から2つ目は 0.05 秒、3つ目は 0.1 秒遅れて動き出す）
	float hideStagger = 0.0f;  // 消すとき、要素1つごとに遅らせる秒数
};

static_assert(std::is_trivially_copyable_v<UIScreenComponent>);

/// <summary>
/// 要素1つだけ、動き方を変える（画面の一番上の Entity の直下の子に付ける。付けなければ UI Screen Component の動きのまま）
/// <para>例：後ろを暗くする Image はフェードだけ、パネルは下から滑り込む</para>
/// </summary>
struct UIScreenElementComponent {
	bool overrideShow = false; // true なら、出し方を show にする
	UIScreenMotion show;
	bool overrideHide = false; // true なら、消し方を hide にする
	UIScreenMotion hide = {.type = UIScreenMotionType::Fade, .duration = 0.2f, .ease = Ease::Type::Linear};
	float delay = 0.0f; // Stagger の遅れに足す秒数（出す・消すの両方）
};

static_assert(std::is_trivially_copyable_v<UIScreenElementComponent>);