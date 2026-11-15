#include "GameInput.h"

#include <format>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "HTTEngine/Core/UserDataPath.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Input/Action/InputActionFile.h"

namespace {
// 2P 以降が読む入力（キーボード・マウスは 1P だけ。キーボードを2人で分けると、既定の割り当てを人ごとに持たなければならないので）
constexpr InputDeviceMask kPadDevices = InputDeviceMask::PadButton | InputDeviceMask::PadAxis;

// 持ち物
struct State {
	std::vector<InputActionDesc> actions; // ファイルの Action（並びは画面に出す順）
	std::vector<InputActionMap> maps;     // プレイヤーごとの表（数＝プレイヤーの数。1 人以上）
	// --- 割り当てを拾う ---
	InputRebinder rebinder;
	int rebindPlayer = 0;                                         // 拾っている人
	InputRebinder::Result lastResult = InputRebinder::Result::Idle; // このフレームの拾う係の結果
	// --- 知らせ ---
	std::unordered_set<std::string> warnedNames; // もう警告した、知らない Action の名前（毎フレーム出さないように）
	std::unordered_set<int> warnedPlayers;       // もう警告した、居ないプレイヤーの番号
};
State& GetState() {
	static State state;
	return state;
}

// ===== その人の表を作る（ファイルの Action を足し、保存した上書きを読む）=====
InputActionMap MakeMap(int player) {
	const State& state = GetState();
	InputActionMap map(InputGate::Game, InputModifierMatch::AtLeast, (player == 0) ? InputDeviceMask::All : kPadDevices);
	map.SetGamepadIndex(player);
	for (const InputActionDesc& desc : state.actions) {
		map.AddAction(desc);
	}
	map.LoadOverrides(GameInput::GetBindingsPath(player)); // ファイルが無ければ既定のまま
	return map;
}

// ===== 聞く表（居ないプレイヤーなら nullptr。番号ごとに1回だけ警告）=====
const InputActionMap* FindMap(int player) {
	State& state = GetState();
	if (player >= 0 && player < static_cast<int>(state.maps.size())) {
		return &state.maps[player];
	}
	if (state.warnedPlayers.insert(player).second) {
		LogManager::Warning(std::format("[GameInput] {} 番のプレイヤーは居ません（今は {} 人。SetPlayerCount で増やす）", player, state.maps.size()));
	}
	return nullptr;
}

// ===== 名前から番号（知らない名前なら無効な番号。名前ごとに1回だけ警告）=====
InputActionId FindAction(const InputActionMap& map, std::string_view name) {
	const InputActionId id = map.Find(name);
	if (!id.IsValid() && GetState().warnedNames.emplace(name).second) {
		LogManager::Warning(std::format("[GameInput] 知らない Action の名前です（{} に無い）: {}", GameInput::kActionsPath, name));
	}
	return id;
}
} // namespace


//=============================================================================
// エンジンが呼ぶ
//=============================================================================
// ===== 初期化 =====
void GameInput::Initialize() {
	State& state = GetState();
	state.actions = InputActionFile::Load(kActionsPath).value_or(std::vector<InputActionDesc>{});
	LogManager::Log(std::format("[GameInput] Action: {} 個（{}）", state.actions.size(), kActionsPath));
	state.maps.clear();
	state.maps.push_back(MakeMap(0));
}

// ===== 毎フレーム =====
void GameInput::Update() {
	State& state = GetState();
	// --- 割り当てを拾う（表の Update の前。拾えたら、その人の表に入れる）---
	const bool wasRebinding = state.rebinder.IsListening();
	state.lastResult = InputRebinder::Result::Idle;
	if (wasRebinding) {
		state.lastResult = state.rebinder.Update();
		if (state.lastResult == InputRebinder::Result::Captured) {
			state.maps[state.rebindPlayer].SetBinding(state.rebinder.GetAction(), state.rebinder.GetIndex(), *state.rebinder.GetCaptured());
		}
	}
	// --- 全員の表を読む（拾っている人の表は、拾えたフレームも止めておく。拾ったキーで、同じフレームにジャンプしないように）---
	for (int player = 0; player < static_cast<int>(state.maps.size()); ++player) {
		InputActionMap& map = state.maps[player];
		map.SetEnabled(!(wasRebinding && player == state.rebindPlayer));
		map.Update();
	}
}

// ===== Action のファイルを読み直す =====
void GameInput::ReloadActions() {
	State& state = GetState();
	CancelRebind(); // 表を作り直すので、拾う係が古い表を見ないように
	state.actions = InputActionFile::Load(kActionsPath).value_or(std::vector<InputActionDesc>{});
	const int count = GetPlayerCount();
	state.maps.clear();
	for (int player = 0; player < count; ++player) {
		state.maps.push_back(MakeMap(player));
	}
	state.warnedNames.clear(); // 足した Action は、もう知らない名前ではない
	LogManager::Log(std::format("[GameInput] Action を読み直しました: {} 個", state.actions.size()));
}


//=============================================================================
// 聞く
//=============================================================================
bool GameInput::IsPressed(std::string_view action, int player) {
	const InputActionMap* map = FindMap(player);
	return map && map->IsPressed(FindAction(*map, action));
}
bool GameInput::IsTriggered(std::string_view action, int player) {
	const InputActionMap* map = FindMap(player);
	return map && map->IsTriggered(FindAction(*map, action));
}
bool GameInput::IsReleased(std::string_view action, int player) {
	const InputActionMap* map = FindMap(player);
	return map && map->IsReleased(FindAction(*map, action));
}
float GameInput::GetValue(std::string_view action, int player) {
	const InputActionMap* map = FindMap(player);
	return map ? map->GetValue(FindAction(*map, action)) : 0.0f;
}
Vector2 GameInput::GetVector2(std::string_view action, int player) {
	const InputActionMap* map = FindMap(player);
	return map ? map->GetVector2(FindAction(*map, action)) : Vector2{0.0f, 0.0f};
}
std::string GameInput::GetDisplayText(std::string_view action, int player) {
	const InputActionMap* map = FindMap(player);
	return map ? map->GetDisplayText(FindAction(*map, action)) : std::string();
}


//=============================================================================
// プレイヤーの数
//=============================================================================
void GameInput::SetPlayerCount(int count) {
	HTT_ASSERT_MSG(count >= 1 && count <= kMaxPlayers, "プレイヤーの数は 1〜4 です");
	State& state = GetState();
	if (count == GetPlayerCount()) {
		return;
	}
	CancelRebind(); // 表の並びが変わる（拾う係が見ている表が動く）ので
	while (static_cast<int>(state.maps.size()) > count) {
		state.maps.pop_back();
	}
	while (static_cast<int>(state.maps.size()) < count) {
		state.maps.push_back(MakeMap(static_cast<int>(state.maps.size())));
	}
	state.warnedPlayers.clear();
}

int GameInput::GetPlayerCount() { return static_cast<int>(GetState().maps.size()); }


//=============================================================================
// キーコンフィグの画面から
//=============================================================================
// ===== その人の表 =====
InputActionMap& GameInput::GetMap(int player) {
	State& state = GetState();
	HTT_ASSERT_MSG(player >= 0 && player < static_cast<int>(state.maps.size()), "居ないプレイヤーの表です（SetPlayerCount で増やす）");
	return state.maps[player];
}

// ===== 拾い始める =====
void GameInput::StartRebind(std::string_view action, InputBindingRow row, int index, int player) {
	State& state = GetState();
	const InputActionMap* map = FindMap(player);
	if (map == nullptr) {
		return;
	}
	const InputActionId id = FindAction(*map, action);
	if (!id.IsValid() || map->GetDesc(id).isFixed) {
		return; // 知らない・変えられない Action
	}
	const int count = static_cast<int>(map->GetBindings(id).size());
	state.rebindPlayer = player;
	state.rebinder.Start(*map, id, (index >= 0 && index < count) ? index : count, row);
}

void GameInput::CancelRebind() { GetState().rebinder.Cancel(); }
bool GameInput::IsRebinding() { return GetState().rebinder.IsListening(); }
InputRebinder::Result GameInput::GetRebindResult() { return GetState().lastResult; }
InputActionId GameInput::GetRebindAction() { return GetState().rebinder.GetAction(); }
int GameInput::GetRebindIndex() { return GetState().rebinder.GetIndex(); }


//=============================================================================
// 保存（プレイヤーの上書き）
//=============================================================================
// ===== 書く =====
bool GameInput::SaveBindings(int player) { return GetMap(player).SaveOverrides(GetBindingsPath(player)); }

// ===== 保存した割り当てに戻す =====
void GameInput::RevertBindings(int player) { GetMap(player).RevertToSaved(); }

// ===== 保存した状態から変えたか =====
bool GameInput::IsBindingsDirty(int player) { return GetMap(player).IsDirty(); }

// ===== 保存先 =====
std::filesystem::path GameInput::GetBindingsPath(int player) { return UserDataPath::GetDirectory() / "Input" / std::format("Player{}.json", player + 1); }
