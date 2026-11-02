#include "UIScreen.h"

#include <algorithm>
#include <format>
#include <memory>
#include <string>
#include <vector>

#include <externals/nlohmann/json.hpp>

#include "MyEngine/Components/Core/TransformComponent.h"
#include "MyEngine/Components/UI/AlphaGroupComponent.h"
#include "MyEngine/Components/UI/UIScreenComponent.h"
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/ECS/ComponentSerializer.h"
#include "MyEngine/ECS/ComponentStorage.h"
#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/ECS/GameComponent.h"
#include "MyEngine/Editor/Inspector/ComponentEditor.h"
#include "MyEngine/Scene/PrefabManager.h"
#include "MyEngine/Scene/SceneManager.h"
#include "MyEngine/Scene/SceneSerializer.h"
#include "MyEngine/String/ConvertString.h"
#include "MyEngine/Systems/Animation/TimelineSystem.h"
#include "MyEngine/Time/EngineTime.h"

namespace {
/// <summary>
/// 開いた画面が今どうしているか
/// </summary>
enum class UIScreenPhase {
	Waiting, // Open した。Component が付くのを待っている（次のフレームの頭の Flush で Showing へ）
	Showing, // 出している途中
	Shown,   // 出きった
	Hiding,  // 消している途中（終わったら Entity を消す）
};

/// <summary>
/// 要素の形（Motion が動かす3つの値）
/// </summary>
struct UIScreenPose {
	Vector3 translation;
	Vector3 scale;
	float alpha = 1.0f; // Alpha Group の Alpha
};

/// <summary>
/// 動く要素1つ（画面の一番上の Entity の直下の子）
/// </summary>
struct UIScreenElement {
	Handle<Entity> entity;
	UIScreenPose basePose;  // 出きったときの形（ファイルに書いてある形。出すときの基準）
	UIScreenPose closePose; // Close したときの形（消すときの基準。出している途中に閉じても、その形から消える）
	UIScreenMotion show;    // 出し方（UI Screen Element で変えていれば、そちら）
	UIScreenMotion hide;    // 消し方
	float showDelay = 0.0f; // 出し始めるまでの秒（Stagger × 何番目か ＋ UI Screen Element の Delay）
	float hideDelay = 0.0f; // 消し始めるまでの秒
};

/// <summary>
/// 開いている画面1つ
/// </summary>
struct OpenUIScreen {
	std::string name;
	Handle<Entity> root; // 一番上の Entity（UI Screen Component が付いている）
	UIScreenPhase phase = UIScreenPhase::Waiting;
	float time = 0.0f;                     // 出し始めて・消し始めてからの秒
	bool pauseGame = false;                // 出している間、ゲームを止めるか
	TimeMode timeMode = TimeMode::Real;    // 出す・消す動きの時間
	uint32_t showTimeline = 0;             // 出すときの Timeline（0 なら Motion）
	uint32_t hideTimeline = 0;             // 消すときの Timeline（0 なら Motion）
	std::vector<UIScreenElement> elements; // 動く要素（Hierarchy の上から順）
	float showEnd = 0.0f;                  // 全部の要素が出きる秒（Motion）
	float hideEnd = 0.0f;                  // 全部の要素が消えきる秒（Motion）
};

/// <summary>
/// UIScreen が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct UIScreenState {
	std::vector<OpenUIScreen> screens;
	bool isPausing = false; // 画面がゲームを止めているか（止めた・動かしたを1回だけ Time に書く）
};
std::unique_ptr<UIScreenState> state;

//=============================================================================
// 小さな道具
//=============================================================================
// ===== 名前から開いている画面（無ければ end）=====
std::vector<OpenUIScreen>::iterator FindScreen(const std::string& name) { return std::ranges::find(state->screens, name, &OpenUIScreen::name); }

// ===== 滑り込む向き（画面のどちら側か → ピクセルの向き。Y は上が＋）=====
Vector3 SideDirection(UIScreenSide side) {
	switch (side) {
	case UIScreenSide::Left:
		return {-1.0f, 0.0f, 0.0f};
	case UIScreenSide::Right:
		return {1.0f, 0.0f, 0.0f};
	case UIScreenSide::Top:
		return {0.0f, 1.0f, 0.0f};
	case UIScreenSide::Bottom:
		return {0.0f, -1.0f, 0.0f};
	}
	return {};
}

// ===== どれだけ見えているか（0：出す前・消えきった ～ 1：出きった）。OutBack などは途中で 1 を少し超える =====
float CalcVisible(const UIScreenMotion& motion, float time, float delay, bool isShowing) {
	float t = 1.0f;
	if (motion.duration > 0.0f) {
		t = std::clamp((time - delay) / motion.duration, 0.0f, 1.0f);
	} else if (time < delay) {
		t = 0.0f; // 0 秒なら、遅れの分を待ってから一度に変える
	}
	const float eased = Ease::Apply(t, motion.ease);
	return isShowing ? eased : 1.0f - eased;
}

// ===== 要素の今の形（Transform か Alpha Group が無ければ false）=====
bool ReadPose(Handle<Entity> entity, UIScreenPose& out) {
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	const AlphaGroupComponent* group = EntityManager::Get<AlphaGroupComponent>(entity);
	if (transform == nullptr || group == nullptr) {
		return false;
	}
	out = {transform->translation, transform->scale, group->enabled ? group->alpha : 1.0f}; // 切ってあった Alpha Group は 1 と同じ
	return true;
}

// ===== 要素1つに、pose を基準にして、見えている量 visible の形を当てる（Translation・Scale・Alpha Group の Alpha）=====
void ApplyElement(const UIScreenElement& element, const UIScreenPose& pose, const UIScreenMotion& motion, float visible) {
	TransformComponent* transform = EntityManager::Get<TransformComponent>(element.entity);
	AlphaGroupComponent* group = EntityManager::Get<AlphaGroupComponent>(element.entity);
	if (transform == nullptr || group == nullptr) {
		return;
	}
	Vector3 translation = pose.translation;
	Vector3 scale = pose.scale;
	float alpha = pose.alpha;
	switch (motion.type) {
	case UIScreenMotionType::None:
		break; // 動かさない
	case UIScreenMotionType::Fade:
		alpha *= visible;
		break;
	case UIScreenMotionType::Slide:
		// 見えていない分だけ、画面の外の方へずらす（visible が 1 を超えると、少し行き過ぎて戻る）
		translation += SideDirection(motion.side) * (motion.distance * (1.0f - visible));
		alpha *= motion.withFade ? visible : 1.0f;
		break;
	case UIScreenMotionType::Scale:
		scale *= Ease::Lerp(motion.fromScale, 1.0f, visible);
		alpha *= motion.withFade ? visible : 1.0f;
		break;
	}
	transform->translation = translation;
	transform->scale = scale;
	group->enabled = true;
	group->alpha = std::clamp(alpha, 0.0f, 1.0f);
}

// ===== 画面の全部の要素に、今の時間の形を当てる（Motion）=====
void ApplyScreen(const OpenUIScreen& screen) {
	const bool isShowing = (screen.phase != UIScreenPhase::Hiding);
	for (const UIScreenElement& element : screen.elements) {
		const UIScreenMotion& motion = isShowing ? element.show : element.hide;
		const float delay = isShowing ? element.showDelay : element.hideDelay;
		ApplyElement(element, isShowing ? element.basePose : element.closePose, motion, CalcVisible(motion, screen.time, delay, isShowing));
	}
}

// ===== 出す・消す Timeline を再生する =====
void PlayScreenTimeline(const OpenUIScreen& screen, uint32_t timeline) { TimelineSystem::PlayLayer(screen.root, TimelineLayer::UIScreen, {.timeline = timeline, .timeMode = screen.timeMode}); }

// ===== 出す・消す Timeline が終わったか（再生していない・ファイルが無いときも終わった扱い）=====
bool IsScreenTimelineFinished(const OpenUIScreen& screen) {
	const TimelineStatus status = TimelineSystem::GetStatus(screen.root, TimelineLayer::UIScreen);
	return !status.isValid || status.state == TimelinePlayState::Finished || status.state == TimelinePlayState::Stopped;
}

// ===== Component が付いた画面の要素を集めて、出し始める（フレームの頭で呼ぶ。Alpha Group をその場で付けるので）=====
void BeginShow(OpenUIScreen& screen) {
	// --- 出し方・消し方（UI Screen Component が無ければ、動かさずに出す）---
	UIScreenComponent settings;
	if (const UIScreenComponent* component = EntityManager::Get<UIScreenComponent>(screen.root)) {
		settings = *component;
	} else {
		LogManager::Warning(std::format("画面 \"{}\" の一番上の Entity に UI Screen Component がありません（動かさずに出します）", screen.name));
		settings.pauseGame = false;
		settings.show.type = UIScreenMotionType::None;
		settings.hide.type = UIScreenMotionType::None;
	}
	screen.pauseGame = settings.pauseGame;
	screen.timeMode = settings.pauseGame ? TimeMode::Real : settings.timeMode; // ゲームを止める画面が、自分の時間で止まらないように
	screen.showTimeline = settings.showTimeline;
	screen.hideTimeline = settings.hideTimeline;

	// --- 要素（直下の子。Hierarchy の上から順）---
	std::vector<Handle<Entity>> children;
	EntityManager::GetChildren(screen.root, children);
	screen.elements.clear();
	for (size_t i = 0; i < children.size(); ++i) {
		if (EntityManager::Get<TransformComponent>(children[i]) == nullptr) {
			continue;
		}
		// フェードは Alpha Group で掛ける。無ければ今付ける（フレームの頭なので、予約せずにその場で付けてよい）
		if (EntityManager::Get<AlphaGroupComponent>(children[i]) == nullptr) {
			EntityManager::RestoreComponents(children[i], {ComponentSnapshot::Make(AlphaGroupComponent{})});
		}
		UIScreenElement element;
		element.entity = children[i];
		ReadPose(children[i], element.basePose);
		element.closePose = element.basePose;
		element.show = settings.show;
		element.hide = settings.hide;
		float extraDelay = 0.0f;
		if (const UIScreenElementComponent* own = EntityManager::Get<UIScreenElementComponent>(children[i])) {
			element.show = own->overrideShow ? own->show : element.show;
			element.hide = own->overrideHide ? own->hide : element.hide;
			extraDelay = own->delay;
		}
		element.showDelay = settings.showStagger * static_cast<float>(i) + extraDelay;
		element.hideDelay = settings.hideStagger * static_cast<float>(i) + extraDelay;
		screen.showEnd = (std::max)(screen.showEnd, element.showDelay + element.show.duration);
		screen.hideEnd = (std::max)(screen.hideEnd, element.hideDelay + element.hide.duration);
		screen.elements.push_back(element);
	}
	screen.phase = UIScreenPhase::Showing;
	screen.time = 0.0f;
	// --- 描く前に、出し始めの形にしておく（Timeline はこのフレームの TimelineSystem::Update が最初の値を書く）---
	if (screen.showTimeline != 0) {
		PlayScreenTimeline(screen, screen.showTimeline);
	} else {
		ApplyScreen(screen);
	}
}

// ===== 消し始める =====
void BeginHide(OpenUIScreen& screen) {
	screen.phase = UIScreenPhase::Hiding;
	screen.time = 0.0f;
	if (screen.hideTimeline != 0) {
		PlayScreenTimeline(screen, screen.hideTimeline); // 出す Timeline と同じ層なので、出す方は置き換わって止まる
		return;
	}
	// --- Motion：今の形から消す（出す Timeline が書くのもやめる）---
	TimelineSystem::StopLayer(screen.root, TimelineLayer::UIScreen);
	for (UIScreenElement& element : screen.elements) {
		ReadPose(element.entity, element.closePose);
	}
}

// ===== 画面がゲームを止めるか =====
// 止める画面が1つでも出ていれば止める。消している途中の画面は数えない。
// Component が付くのを待っている画面は、止めているかが分からないので今のまま（Switch Screen で消して出す間に、1フレームだけ動き出さないように）
void UpdatePause() {
	const bool wantsPause = std::ranges::any_of(state->screens, [](const OpenUIScreen& screen) {
		switch (screen.phase) {
		case UIScreenPhase::Waiting:
			return state->isPausing;
		case UIScreenPhase::Showing:
		case UIScreenPhase::Shown:
			return screen.pauseGame;
		case UIScreenPhase::Hiding:
			return false;
		}
		return false;
	});
	if (wantsPause != state->isPausing) {
		state->isPausing = wantsPause;
		Time::SetGamePaused(wantsPause);
	}
}

// ===== 画面1つを1フレーム進める（もう忘れてよければ true）=====
bool UpdateScreen(OpenUIScreen& screen, float deltaTime) {
	switch (screen.phase) {
	case UIScreenPhase::Waiting:
	case UIScreenPhase::Shown:
		return false;
	case UIScreenPhase::Showing:
		screen.time += deltaTime;
		if (screen.showTimeline != 0) {
			screen.phase = IsScreenTimelineFinished(screen) ? UIScreenPhase::Shown : screen.phase;
			return false;
		}
		ApplyScreen(screen);
		if (screen.time >= screen.showEnd) {
			screen.phase = UIScreenPhase::Shown;
		}
		return false;
	case UIScreenPhase::Hiding: {
		screen.time += deltaTime;
		bool isFinished = false;
		if (screen.hideTimeline != 0) {
			isFinished = IsScreenTimelineFinished(screen);
		} else {
			ApplyScreen(screen);
			isFinished = screen.time >= screen.hideEnd;
		}
		if (isFinished) {
			EntityManager::Destroy(screen.root); // 子も一緒に消える（Timeline の再生も、Entity と一緒に消える）
		}
		return isFinished;
	}
	}
	return false;
}

//=============================================================================
// Inspector と 保存
//=============================================================================
// ===== 出し方・消し方1つ分（id は保存の名前の頭。"show" → "showMotion"・"showDuration" …）=====
// 選んだ動きの値だけ見せる（保存も、使っている方だけが書かれる）
void DescribeMotion(ComponentUI& ui, UIScreenMotion& motion, const char* id) {
	ui.Field(std::format("Motion###{}Motion", id).c_str(), motion.type, Tip("None：動かない / Fade：だんだん見える・消える / Slide：画面の外の方から滑り込む / Scale：小さい所から大きくなる"));
	switch (motion.type) {
	case UIScreenMotionType::None:
		return; // 秒数もイージングも使わない
	case UIScreenMotionType::Fade:
		break;
	case UIScreenMotionType::Slide:
		ui.Field(std::format("Side###{}Side", id).c_str(), motion.side, Tip("出すときは、どちら側から来るか。消すときは、どちら側へ行くか"));
		ui.Field(std::format("Distance###{}Distance", id).c_str(), motion.distance, AtLeast(0.0f).Tip("動く長さ（ピクセル。ゲームの画面のつもりで。画面の外から入れるなら、画面の幅か高さの値）"));
		ui.Field(std::format("With Fade###{}WithFade", id).c_str(), motion.withFade, Tip("動きながらフェードもする"));
		break;
	case UIScreenMotionType::Scale:
		ui.Field(std::format("From Scale###{}FromScale", id).c_str(), motion.fromScale, AtLeast(0.0f).Tip("一番小さいときの大きさ（元の大きさが 1）"));
		ui.Field(std::format("With Fade###{}WithFade", id).c_str(), motion.withFade, Tip("大きさを変えながらフェードもする"));
		break;
	}
	ui.Field(std::format("Duration###{}Duration", id).c_str(), motion.duration, AtLeast(0.0f).Tip("1つの要素が動ききるまでの秒数（ゲームを止めていても進む）"));
	ui.Field(
	    std::format("Ease###{}Ease", id).c_str(), motion.ease,
	    Tip("動きの速さの変え方。出すときは Out〜（速く入って、ゆっくり止まる）、消すときは In〜（ゆっくり動き出して、速く抜ける）が自然。OutBack は少し行き過ぎて戻る"));
}

void DescribeUIScreen(ComponentUI& ui, UIScreenComponent& value) {
	ui.Label("画面のファイルの一番上の Entity に付ける。Timeline を選ぶとそれを再生し、選ばなければ直下の子が上から順に1つずつ Motion で動く");
	ui.Field("Pause Game###pauseGame", value.pauseGame, Tip("出している間、ゲームを止める（時間が0・Script を呼ばない）。この画面の中の Script は動く"));
	ui.BeginDisabled(value.pauseGame);
	ui.Field("Time Mode###timeMode", value.timeMode, Tip("出す・消す動きの時間。Real：ゲームが止まっていても動く / Game：ポーズ・スローで止まる（Pause Game を入れた画面は、いつも Real）"));
	ui.EndDisabled();
	ui.Separator("Show");
	ui.AssetField("Timeline###showTimeline", value.showTimeline, AssetType::Timeline, Tip("出すときに再生する Timeline（この Entity を再生する Entity にする）。選ぶと下の Motion は使わない"));
	ui.BeginDisabled(value.showTimeline != 0);
	DescribeMotion(ui, value.show, "show");
	ui.Field("Stagger###showStagger", value.showStagger, AtLeast(0.0f).Tip("要素1つごとに遅らせる秒数（上から2つ目はこの秒、3つ目は2倍遅れて動き出す）"));
	ui.EndDisabled();
	ui.Separator("Hide");
	ui.AssetField(
	    "Timeline###hideTimeline", value.hideTimeline, AssetType::Timeline,
	    Tip("消すときに再生する Timeline。出している途中で消しても値が飛ばないように、Tween の From は Current にする。選ぶと下の Motion は使わない"));
	ui.BeginDisabled(value.hideTimeline != 0);
	DescribeMotion(ui, value.hide, "hide");
	ui.Field("Stagger###hideStagger", value.hideStagger, AtLeast(0.0f).Tip("要素1つごとに遅らせる秒数"));
	ui.EndDisabled();
}

void DescribeUIScreenElement(ComponentUI& ui, UIScreenElementComponent& value) {
	ui.Label("画面の一番上の Entity の直下の子に付ける。この要素だけ動き方を変える（Motion で出す・消すときだけ使う）");
	ui.Field("Override Show###overrideShow", value.overrideShow, Tip("出し方を、ここで決めた物にする"));
	if (value.overrideShow) {
		DescribeMotion(ui, value.show, "show");
	}
	ui.Field("Override Hide###overrideHide", value.overrideHide, Tip("消し方を、ここで決めた物にする"));
	if (value.overrideHide) {
		DescribeMotion(ui, value.hide, "hide");
	}
	ui.Field("Delay###delay", value.delay, AtLeast(0.0f).Tip("Stagger の遅れに足す秒数（出す・消すの両方）"));
}
} // namespace

//=============================================================================
// 初期化 / 解放
//=============================================================================
void UIScreen::Initialize() {
	MY_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<UIScreenState>();
	// UI Screen・UI Screen Element の置き場所・保存・Inspector を登録する（見せ方は Describe〜 1つで、保存と Inspector の両方に使う）
	EntityManager::RegisterComponent<UIScreenComponent>();
	ComponentSerializer::Register<UIScreenComponent>("UI Screen", &DescribeUIScreen);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<UIScreenComponent>>("UI Screen", "UI", &DescribeUIScreen));
	EntityManager::RegisterComponent<UIScreenElementComponent>();
	ComponentSerializer::Register<UIScreenElementComponent>("UI Screen Element", &DescribeUIScreenElement);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<UIScreenElementComponent>>("UI Screen Element", "UI", &DescribeUIScreenElement));
}

void UIScreen::Release() { state.reset(); }

//=============================================================================
// ゲームから使う
//=============================================================================
bool UIScreen::Open(const std::string& name) {
	MY_ASSERT_MSG(state != nullptr, "UIScreen::Initialize()を先に呼んでください");
	// 停止中はゲームのコードが動かないので、ふつうは来ない（エディタで開いているシーンに、勝手に Entity を足さないように）
	const SceneManager* main = SceneManager::GetMain();
	if (main == nullptr || main->GetPlayState() == PlayState::Editing) {
		LogManager::Log(std::format("UIScreen::Open(\"{}\")：停止中なので出しません（Play 中だけ効きます）", name));
		return false;
	}
	// もう出ている・消している途中なら出さない（Esc を続けて押したときに、2枚重ならないように）
	if (FindScreen(name) != state->screens.end()) {
		return false;
	}
	// 同じファイルは1回だけ読む（2回目からは同じ番号。エディタで保存し直すと、中身も差し替わる）
	const uint32_t prefab = PrefabManager::Load(ToPath(MakeUIScreenFilePath(name)));
	if (prefab == 0) {
		LogManager::Warning(std::format("UIScreen::Open(\"{}\")：{} が読めません", name, MakeUIScreenFilePath(name)));
		return false;
	}
	OpenUIScreen screen;
	screen.name = name;
	screen.root = PrefabManager::Instantiate(prefab, {0.0f, 0.0f, 0.0f}); // 一番上の Entity は原点（UI の位置は子に書く）
	state->screens.push_back(std::move(screen));
	return true;
}

bool UIScreen::Close(const std::string& name) {
	MY_ASSERT_MSG(state != nullptr, "UIScreen::Initialize()を先に呼んでください");
	const auto it = FindScreen(name);
	if (it == state->screens.end() || it->phase == UIScreenPhase::Hiding) {
		return false;
	}
	// まだ Component が付いていない（Open と同じフレームに Close した）なら、動かさずに消す
	if (it->phase == UIScreenPhase::Waiting) {
		EntityManager::Destroy(it->root);
		state->screens.erase(it);
		UpdatePause();
		return true;
	}
	BeginHide(*it);
	UpdatePause(); // 消し始めたら、すぐゲームを動かす（消える動きを待たずに遊べるように）
	return true;
}

bool UIScreen::CloseContaining(Handle<Entity> entity) {
	MY_ASSERT_MSG(state != nullptr, "UIScreen::Initialize()を先に呼んでください");
	const auto it = std::ranges::find_if(state->screens, [entity](const OpenUIScreen& screen) { return entity == screen.root || EntityManager::IsDescendantOf(entity, screen.root); });
	return it != state->screens.end() && Close(std::string(it->name)); // Close が screens を書き換えるので、名前は写して渡す
}

bool UIScreen::IsOpen(const std::string& name) {
	MY_ASSERT_MSG(state != nullptr, "UIScreen::Initialize()を先に呼んでください");
	const auto it = FindScreen(name);
	return it != state->screens.end() && it->phase != UIScreenPhase::Hiding;
}

Handle<Entity> UIScreen::GetRoot(const std::string& name) {
	MY_ASSERT_MSG(state != nullptr, "UIScreen::Initialize()を先に呼んでください");
	const auto it = FindScreen(name);
	return (it != state->screens.end()) ? it->root : Handle<Entity>{};
}

//=============================================================================
// エンジンから使う
//=============================================================================
bool UIScreen::IsInsideOpenScreen(Handle<Entity> entity) {
	return std::ranges::any_of(state->screens, [entity](const OpenUIScreen& screen) { return entity == screen.root || EntityManager::IsDescendantOf(entity, screen.root); });
}

void UIScreen::Flush() {
	MY_ASSERT_MSG(state != nullptr, "UIScreen::Initialize()を先に呼んでください");
	for (OpenUIScreen& screen : state->screens) {
		// Open した次のフレームの頭：PrefabManager::Flush で Component が付いたので、出し始める
		if (screen.phase == UIScreenPhase::Waiting && EntityManager::IsAlive(screen.root)) {
			BeginShow(screen);
		}
	}
	UpdatePause();
}

void UIScreen::Update(bool isGameRunning) {
	MY_ASSERT_MSG(state != nullptr, "UIScreen::Initialize()を先に呼んでください");
	// 停止中：全部忘れる（Stop でシーンが Play を押した瞬間に戻るので、画面の Entity も無くなる）
	if (!isGameRunning) {
		state->screens.clear();
		state->isPausing = false;
		return;
	}
	// エディタの一時停止中は、画面の動きも止める
	const SceneManager* main = SceneManager::GetMain();
	if (main != nullptr && main->GetPlayState() == PlayState::Paused) {
		return;
	}
	// erase_if：条件に合う物を配列から全部消す（C++20）。true を返した画面は、もう忘れる
	std::erase_if(state->screens, [](OpenUIScreen& screen) {
		// シーンが変わった・誰かが Destroy した
		if (!EntityManager::IsAlive(screen.root)) {
			return true;
		}
		return UpdateScreen(screen, Time::GetDeltaTime(screen.timeMode));
	});
	UpdatePause();
}

//=============================================================================
// エディタから使う
//=============================================================================
bool UIScreen::CreateNewFile(const std::filesystem::path& path) {
	// 一番上の Entity 1つ（名前はファイルの名前、UI Screen Component だけ）。書いていない値は、読むときに初期値になる
	SceneJson item = SceneJson::object();
	item["id"] = EntityId{1};
	item["name"] = ToUtf8(path.stem());
	item["parent"] = EntityId{0};
	item["active"] = true;
	item["components"] = {
	    {"UI Screen", SceneJson::object()}
    };
	SceneJson root = SceneJson::object();
	root["version"] = SceneSerializer::kVersion;
	root["entities"] = SceneJson::array({item});
	if (!SceneSerializer::WriteFile(ToUtf8(path), root)) {
		return false;
	}
	LogManager::Log(std::format("画面を作りました: {}", ToUtf8(path)));
	return true;
}