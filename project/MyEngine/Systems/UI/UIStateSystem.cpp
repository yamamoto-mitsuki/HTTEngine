#include "UIStateSystem.h"

#include <algorithm>
#include <array>
#include <format>
#include <memory>
#include <vector>

#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/ECS/ComponentSerializer.h"
#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/ECS/GameComponent.h"
#include "MyEngine/Editor/Inspector/ComponentEditor.h"
#include "MyEngine/Input/InputManager.h"
#include "MyEngine/Scene/SceneManager.h"
#include "MyEngine/Systems/Animation/TimelineSystem.h"
#include "MyEngine/Systems/Rendering2D/SpriteRenderSystem.h"
#include "MyEngine/Systems/UI/VectorShapeSystem.h"
#include "MyEngine/Time/EngineTime.h"
#include "MyEngine/UI/UIScreen.h"

namespace {
constexpr int kLeftButton = 0; // マウスの左ボタン

/// <summary>
/// 状態の Timeline の欄1つ（Inspector の名前・保存の名前・説明）
/// </summary>
struct StateField {
	const char* label;
	const char* key;
	const char* tooltip;
};
constexpr std::array<StateField, kUIElementStateCount> kStateFields = {
    {
     {"Normal", "normal", "何もしていないときに再生する（ほかの状態から戻るときにも使うので、From を Current にして元の形へ戻す物にする）"},
     {"Hover", "hover", "マウスが乗ったときに再生する（選んでいなければ Normal）"},
     {"Pressed", "pressed", "上で押したときに再生する（選んでいなければ Normal）"},
     {"Selected", "selected", "UIStateSystem::SetSelected で選んだときに再生する（選んでいなければ Normal）"},
     {"Disabled", "disabled", "Interactable を外したときに再生する（選んでいなければ Normal）"},
     }
};

/// <summary>
/// 要素1つの、実行中の値
/// </summary>
struct UIStateRuntime {
	Handle<Entity> entity;
	UIElementState state = UIElementState::Normal; // 今の状態（最後に再生した状態）
	bool isStarted = false;                        // 最初の状態を当てた
	bool isSelected = false;                       // SetSelected で選んでいる
	bool isPressing = false;                       // 上で押して、まだ離していない
	bool isClicked = false;                        // この前の Update でクリックされた
	bool isVisible = true;                         // 見えている（Hide を再生した後は false）
	bool needsVisibility = false;                  // 次の Update で Show・Hide を再生する
};

/// <summary>
/// UIStateSystem が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct UIStateState {
	std::vector<UIStateRuntime> runtimes; // 動かしている要素（数が少ないので、一列に並べて順に探す）
};
std::unique_ptr<UIStateState> state;

//=============================================================================
// 小さな道具
//=============================================================================
// ===== 実行中の値（無ければ nullptr）=====
UIStateRuntime* FindRuntime(Handle<Entity> entity) {
	const auto found = std::ranges::find(state->runtimes, entity, &UIStateRuntime::entity);
	return (found != state->runtimes.end()) ? &*found : nullptr;
}

// ===== 実行中の値を用意する（UI State が無ければ nullptr）=====
UIStateRuntime* RequireRuntime(Handle<Entity> entity) {
	const UIStateComponent* component = EntityManager::Get<UIStateComponent>(entity);
	if (component == nullptr) {
		return nullptr;
	}
	if (UIStateRuntime* runtime = FindRuntime(entity)) {
		return runtime;
	}
	state->runtimes.push_back({.entity = entity, .isVisible = component->startVisible});
	return &state->runtimes.back();
}

// ===== マウスが要素の形の上にあるか（Vector Shape か Image）=====
bool IsMouseOver(Handle<Entity> entity) { return VectorShapeSystem::IsMouseOver(entity) || SpriteRenderSystem::IsMouseOver(entity); }

// ===== マウスに反応してよいか（ポーズ中は、開いている画面の中だけ）=====
bool CanReceiveInput(Handle<Entity> entity) { return !Time::IsGamePaused() || UIScreen::IsInsideOpenScreen(entity); }

// ===== 今の状態を決める（強い順）=====
UIElementState DecideState(const UIStateComponent& component, const UIStateRuntime& runtime, bool isOver) {
	if (!component.interactable) {
		return UIElementState::Disabled;
	}
	if (runtime.isPressing && isOver) {
		return UIElementState::Pressed;
	}
	if (isOver) {
		return UIElementState::Hover;
	}
	return runtime.isSelected ? UIElementState::Selected : UIElementState::Normal;
}

// ===== 状態の Timeline を再生する（選んでいなければ Normal。Normal も無ければ、前の状態の再生をやめる）=====
void PlayState(Handle<Entity> entity, const UIStateComponent& component, UIElementState elementState, bool jumpToEnd) {
	uint32_t timeline = component.stateTimelines[static_cast<size_t>(elementState)];
	if (timeline == 0) {
		timeline = component.stateTimelines[static_cast<size_t>(UIElementState::Normal)];
	}
	if (timeline == 0) {
		TimelineSystem::StopLayer(entity, TimelineLayer::UIState);
		return;
	}
	TimelineSystem::PlayLayer(entity, TimelineLayer::UIState, {.timeline = timeline, .timeMode = component.timeMode, .jumpToEnd = jumpToEnd});
}

// ===== Show・Hide の Timeline を再生する（選んでいなければ、前の再生をやめるだけ）=====
void PlayVisibility(Handle<Entity> entity, const UIStateComponent& component, bool isVisible, bool jumpToEnd) {
	const uint32_t timeline = isVisible ? component.showTimeline : component.hideTimeline;
	if (timeline == 0) {
		TimelineSystem::StopLayer(entity, TimelineLayer::UIVisibility);
		return;
	}
	TimelineSystem::PlayLayer(entity, TimelineLayer::UIVisibility, {.timeline = timeline, .timeMode = component.timeMode, .jumpToEnd = jumpToEnd});
}

// ===== クリックしたときにすること =====
void RunClickAction(Handle<Entity> entity, const UIStateComponent& component) {
	const std::string screen = component.screen.ToString();
	switch (component.clickAction) {
	case UIClickAction::None:
		break;
	case UIClickAction::OpenScreen:
		UIScreen::Open(screen);
		break;
	case UIClickAction::CloseScreen:
		UIScreen::Close(screen);
		break;
	case UIClickAction::CloseOwnScreen:
		UIScreen::CloseContaining(entity);
		break;
	case UIClickAction::SwitchScreen:
		// 出す方を先に頼む（先に消すと、ゲームを止める画面が1つも無いフレームができて、1フレームだけゲームが動く）
		UIScreen::Open(screen);
		UIScreen::CloseContaining(entity);
		break;
	}
}

// ===== 要素1つを1フレーム進める =====
void UpdateEntity(Handle<Entity> entity, const UIStateComponent& component) {
	UIStateRuntime& runtime = *RequireRuntime(entity);
	runtime.isClicked = false;
	if (!EntityManager::IsActiveInHierarchy(entity)) {
		runtime.isPressing = false;
		return; // 無効の間は状態を変えない（有効に戻ったら、その時の状態の Timeline を再生する）
	}
	// --- マウス（上で押して、上で離したらクリック）---
	// 最初のフレームは見ない（まだワールド行列が計算されていないので、形がどこにあるか分からない）
	const bool canInput = runtime.isStarted && runtime.isVisible && component.interactable && component.useMouse && CanReceiveInput(entity);
	const bool isOver = canInput && IsMouseOver(entity);
	if (isOver && InputManager::IsMouseButtonTriggered(kLeftButton)) {
		runtime.isPressing = true;
	}
	if (runtime.isPressing && !InputManager::IsMouseButtonPressed(kLeftButton)) {
		runtime.isPressing = false;
		runtime.isClicked = isOver;
	}
	if (!canInput) {
		runtime.isPressing = false;
	}
	// --- 状態が変わったら、その状態の Timeline を再生する（最初のフレームは、動かさずに最後の形を当てる）---
	const UIElementState next = DecideState(component, runtime, isOver);
	if (!runtime.isStarted) {
		runtime.isStarted = true;
		PlayState(entity, component, next, true);
		if (!runtime.isVisible) {
			PlayVisibility(entity, component, false, true);
		}
		runtime.needsVisibility = false;
	} else if (next != runtime.state) {
		PlayState(entity, component, next, false);
	}
	runtime.state = next;
	// --- 出す・消す（SetVisible で頼まれた）---
	if (runtime.needsVisibility) {
		runtime.needsVisibility = false;
		PlayVisibility(entity, component, runtime.isVisible, false);
	}
	if (runtime.isClicked) {
		RunClickAction(entity, component);
	}
}

//=============================================================================
// Inspector と 保存
//=============================================================================
void DescribeUIState(ComponentUI& ui, UIStateComponent& value) {
	ui.Label("状態が変わったら、その状態の Timeline を再生する（Timeline の対象を空にすると、この Entity を動かす）。マウスの当たりは、この Entity の Vector Shape か Image の形");
	ui.Field("Interactable###interactable", value.interactable, Tip("外すと Disabled（マウスに反応しない）"));
	ui.Field("Use Mouse###useMouse", value.useMouse, Tip("マウスで Hover・Pressed になる・クリックできる"));
	ui.Field("Start Visible###startVisible", value.startVisible, Tip("最初から見えている。外すと、最初に Hide の最後の形にする（UIStateSystem::SetVisible(true) で出す）"));
	ui.Field("Time Mode###timeMode", value.timeMode, Tip("Real：ゲームが止まっていても動く / Game：ポーズ・スロー・ヒットストップで止まる"));
	ui.Separator("States");
	for (size_t i = 0; i < kStateFields.size(); ++i) {
		const StateField& field = kStateFields[i];
		ui.AssetField(std::format("{}###{}", field.label, field.key).c_str(), value.stateTimelines[i], AssetType::Timeline, Tip(field.tooltip));
	}
	ui.Separator("Show / Hide");
	ui.AssetField("Show###show", value.showTimeline, AssetType::Timeline, Tip("UIStateSystem::SetVisible(true) で再生する"));
	ui.AssetField("Hide###hide", value.hideTimeline, AssetType::Timeline, Tip("UIStateSystem::SetVisible(false) で再生する。消えている間はマウスに反応しない"));
	ui.Separator("Click");
	ui.Field(
	    "Action###clickAction", value.clickAction,
	    Tip("クリックしたときにすること。None：何もしない（Script で UIStateSystem::IsClicked を見る）/ Open Screen・Close Screen：Screen の画面を出す・消す / Close Own "
	        "Screen：この要素が入っている画面を消す / Switch Screen：この要素が入っている画面を消して、Screen の画面を出す"));
	const bool usesScreen = value.clickAction == UIClickAction::OpenScreen || value.clickAction == UIClickAction::CloseScreen || value.clickAction == UIClickAction::SwitchScreen;
	ui.BeginDisabled(!usesScreen);
	ui.Field("Screen###screen", value.screen, Tip("画面の名前（resources/ui/〇〇.uiscreen の 〇〇）"));
	ui.EndDisabled();
}
} // namespace

//=============================================================================
// 初期化 / 解放
//=============================================================================
void UIStateSystem::Initialize() {
	MY_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<UIStateState>();
	// UI State の置き場所・保存・Inspector を登録する（見せ方は DescribeUIState 1つで、保存と Inspector の両方に使う）
	EntityManager::RegisterComponent<UIStateComponent>();
	ComponentSerializer::Register<UIStateComponent>("UI State", &DescribeUIState);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<UIStateComponent>>("UI State", "UI", &DescribeUIState));
	EntityManager::AddDestroyListener(&UIStateSystem::OnEntityDestroyed);
}

void UIStateSystem::Release() { state.reset(); }

//=============================================================================
// 毎フレーム
//=============================================================================
void UIStateSystem::Update(bool isGameRunning) {
	MY_ASSERT_MSG(state != nullptr, "UIStateSystem::Initialize()を先に呼んでください");
	// 停止中：全部忘れる（Play を押すと、また最初の状態から）
	if (!isGameRunning) {
		state->runtimes.clear();
		return;
	}
	// エディタの一時停止中は、状態も変えない
	const SceneManager* main = SceneManager::GetMain();
	if (main != nullptr && main->GetPlayState() == PlayState::Paused) {
		return;
	}
	// --- UI State を外された Entity を片付ける ---
	std::erase_if(state->runtimes, [](const UIStateRuntime& runtime) { return EntityManager::Get<UIStateComponent>(runtime.entity) == nullptr; });
	EntityManager::ForEach<UIStateComponent>([](Handle<Entity> entity, UIStateComponent& component) { UpdateEntity(entity, component); });
}

//=============================================================================
// ゲームから使う
//=============================================================================
UIElementState UIStateSystem::GetState(Handle<Entity> entity) {
	const UIStateRuntime* runtime = FindRuntime(entity);
	return (runtime != nullptr) ? runtime->state : UIElementState::Normal;
}

bool UIStateSystem::IsClicked(Handle<Entity> entity) {
	const UIStateRuntime* runtime = FindRuntime(entity);
	return runtime != nullptr && runtime->isClicked;
}

void UIStateSystem::SetSelected(Handle<Entity> entity, bool isSelected) {
	if (UIStateRuntime* runtime = RequireRuntime(entity)) {
		runtime->isSelected = isSelected;
	}
}

void UIStateSystem::SetVisible(Handle<Entity> entity, bool isVisible) {
	UIStateRuntime* runtime = RequireRuntime(entity);
	if (runtime == nullptr || runtime->isVisible == isVisible) {
		return;
	}
	runtime->isVisible = isVisible;
	runtime->isPressing = false;
	runtime->needsVisibility = true; // 再生するのは次の Update（最初の Update の前なら、最初の形として当てる）
}

bool UIStateSystem::IsVisible(Handle<Entity> entity) {
	const UIStateRuntime* runtime = FindRuntime(entity);
	if (runtime != nullptr) {
		return runtime->isVisible;
	}
	const UIStateComponent* component = EntityManager::Get<UIStateComponent>(entity);
	return component != nullptr && component->startVisible;
}

//=============================================================================
// Entity の破棄
//=============================================================================
void UIStateSystem::OnEntityDestroyed(Handle<Entity> entity) {
	if (state != nullptr) {
		std::erase_if(state->runtimes, [entity](const UIStateRuntime& runtime) { return runtime.entity == entity; });
	}
}