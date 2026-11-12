#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "HTTEngine/Input/InputManager.h"
#include "HTTEngine/Input/Action/InputBinding.h"
#include "HTTEngine/Math/Vector2.h"


/// <summary>
/// Action の値の種類
/// </summary>
enum class InputActionType : uint8_t {
	Button,  // 押す・離す（ジャンプ・Undo・ドラッグ中か）。軸を割り当てたら、その向きに 0.5 以上で押した
	Value,   // 1つの量（ホイール・トリガー）。割り当ての値を足す（向きが - なら引く）
	Vector2, // 2つの量（移動・視点）。割り当ての値を、向き（±X・±Y）の側に足す
};

/// <summary>
/// 修飾キー（Ctrl・Shift・Alt）の合わせ方。表ごとに決める
/// </summary>
enum class InputModifierMatch : uint8_t {
	Exact,   // ちょうど同じときだけ（"Ctrl+Z" は Ctrl+Shift+Z では効かない。"Z" は Ctrl を押していると効かない）。エディタのショートカット向き
	AtLeast, // 書いた修飾キーを押していればよい（ほかを一緒に押していても効く。"Space" は Shift を押しながらでも効く）。ゲーム向き
};

/// <summary>
/// 割り当てられる入力の種類（InputDevice のビットの組）
/// </summary>
enum class InputDeviceMask : uint8_t {
	None = 0,
	Key = 1 << static_cast<uint8_t>(InputDevice::Key),
	MouseButton = 1 << static_cast<uint8_t>(InputDevice::MouseButton),
	MouseMove = 1 << static_cast<uint8_t>(InputDevice::MouseMove),
	MouseWheel = 1 << static_cast<uint8_t>(InputDevice::MouseWheel),
	PadButton = 1 << static_cast<uint8_t>(InputDevice::PadButton),
	PadAxis = 1 << static_cast<uint8_t>(InputDevice::PadAxis),
	Buttons = Key | MouseButton | PadButton,                  // 押す・離すの入力だけ
	All = Buttons | MouseMove | MouseWheel | PadAxis,          // 全部
};
constexpr InputDeviceMask operator|(InputDeviceMask a, InputDeviceMask b) { return static_cast<InputDeviceMask>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b)); }
constexpr InputDeviceMask operator&(InputDeviceMask a, InputDeviceMask b) { return static_cast<InputDeviceMask>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b)); }
// その入力の種類が入っているか
constexpr bool Contains(InputDeviceMask mask, InputDevice device) { return (static_cast<uint8_t>(mask) & (1 << static_cast<uint8_t>(device))) != 0; }

/// <summary>
/// Action 1つの決まり（表に足すときに渡す）
/// </summary>
struct InputActionDesc {
	std::string name;    // 保存に使う名前（"SceneView.Rotate"・"Jump"）。表の中で1つだけ。変えると、保存した割り当てが外れる
	std::string label;   // 画面に出す名前
	std::string group;   // 画面の見出し（続けて足した同じ group を、まとめて出す）
	std::string context; // 効く場所。ぶつかりを調べるのは、同じ context どうしと、空（どこでも効く）との間だけ
	std::string tooltip; // 説明（画面の吹き出し）
	InputActionType type = InputActionType::Button;
	InputDeviceMask accepts = InputDeviceMask::None; // 割り当てられる入力（None なら type で決める：Button は押す入力だけ、ほかは全部）
	bool clampLength = false;                        // Vector2 の長さを 1 までにする（キーの斜め・スティックの移動。マウスの動きには付けない）
	bool isFixed = false;                            // 割り当てを変えられない（表に出してぶつかりを知らせるだけ。ほかの所でもその入力を決め打ちで使っている物）
	std::vector<InputBinding> defaults;              // 既定の割り当て（空なら、割り当てなし）
};

/// <summary>
/// 表の中の Action の番号（足した順。Find で名前から引く）
/// </summary>
struct InputActionId {
	static constexpr uint32_t kInvalid = UINT32_MAX;
	uint32_t index = kInvalid;
	bool IsValid() const { return index != kInvalid; }
	bool operator==(const InputActionId&) const = default;
};

/// <summary>
/// ぶつかっている割り当て1つ（どの Action の何番目か）
/// </summary>
struct InputBindingRef {
	InputActionId action;
	int index = -1;
};


/// <summary>
/// Input Action の表（名前の付いた操作と、それに割り当てた入力の一覧）
/// <para>毎フレーム1回 Update で入力を読み、IsPressed などで Action の名前（番号）から聞く。キー・ボタンを直接書かないので、割り当てを人ごとに変えられる</para>
/// <para>割り当ては「既定」と「人ごとの上書き」に分けて持つ。SaveOverrides は既定から変えた Action だけをファイルに書く</para>
/// <para>エディタの操作（EditorKeymap）とゲームの操作の両方で使う。読む決まり（InputGate）と修飾キーの合わせ方は、表ごとに決める</para>
/// </summary>
class InputActionMap {
public:
	InputActionMap(InputGate gate, InputModifierMatch modifierMatch, InputDeviceMask allowedDevices = InputDeviceMask::All);

	//=========================================================================
	// 登録
	//=========================================================================
	// Action を足す（名前がもうあれば足さずに、その番号を返す）
	InputActionId AddAction(InputActionDesc desc);
	// 名前から番号（無ければ無効な番号）
	InputActionId Find(std::string_view name) const;
	size_t GetActionCount() const { return actions_.size(); }
	const InputActionDesc& GetDesc(InputActionId id) const;
	// その Action に割り当てられる入力（表の allowedDevices と、Action の accepts の両方に入っている物）
	InputDeviceMask GetAcceptedDevices(InputActionId id) const;

	//=========================================================================
	// 毎フレーム
	//=========================================================================
	// 入力を読んで、全部の Action の今の状態を決める（フレームの頭に1回。2回呼ぶと、押した瞬間が分からなくなる）
	void Update();
	// false の間は、全部の Action を「押していない」にする（割り当てを拾っている間など）。true に戻したとき押していた物は、一度離すまで効かない
	void SetEnabled(bool enabled);
	bool IsEnabled() const { return isEnabled_; }
	// 読むパッドの番号（0〜3。初めは 0）。2人で遊ぶなら、表を2つ作って番号を変える
	void SetGamepadIndex(int index) { gamepadIndex_ = index; }
	// 読む決まり（拾うとき InputRebinder も同じ決まりで読む）
	InputGate GetGate() const { return gate_; }
	int GetGamepadIndex() const { return gamepadIndex_; }

	//=========================================================================
	// 聞く
	//=========================================================================
	bool IsPressed(InputActionId id) const;   // 押している間
	bool IsTriggered(InputActionId id) const; // 押した瞬間
	bool IsReleased(InputActionId id) const;  // 離した瞬間
	float GetValue(InputActionId id) const;   // Value の量（Button は 0 か 1、Vector2 は長さ）
	Vector2 GetVector2(InputActionId id) const; // Vector2 の量（Button・Value は x だけ）
	// 名前でも聞ける（毎回名前を探すので、毎フレームたくさん聞くなら Find で番号にしておく）
	bool IsPressed(std::string_view name) const { return IsPressed(Find(name)); }
	bool IsTriggered(std::string_view name) const { return IsTriggered(Find(name)); }
	bool IsReleased(std::string_view name) const { return IsReleased(Find(name)); }
	float GetValue(std::string_view name) const { return GetValue(Find(name)); }
	Vector2 GetVector2(std::string_view name) const { return GetVector2(Find(name)); }

	//=========================================================================
	// 割り当て
	//=========================================================================
	const std::vector<InputBinding>& GetBindings(InputActionId id) const;
	// 割り当てを全部入れ替える（入れ替えた Action は、今押している物を一度離すまで効かない。変えられない Action（isFixed）は何もしない。下の3つも同じ）
	void SetBindings(InputActionId id, std::vector<InputBinding> bindings);
	// index 番目を入れ替える（index が数と同じなら後ろに足す）
	void SetBinding(InputActionId id, int index, const InputBinding& binding);
	// index 番目を消す
	void RemoveBinding(InputActionId id, int index);
	// 既定に戻す
	void ResetToDefault(InputActionId id);
	void ResetAllToDefault();
	bool IsDefault(InputActionId id) const;
	// 画面に出す文字（1つ目の割り当て。割り当てが無ければ空）。メニューのショートカットの欄などに
	std::string GetDisplayText(InputActionId id) const;

	//=========================================================================
	// ぶつかり
	//=========================================================================
	// id の index 番目の割り当てと、同時に効いてしまう割り当て（同じ入力・合う修飾キー・効く場所が重なる）の一覧
	std::vector<InputBindingRef> FindConflicts(InputActionId id, int index) const;

	//=========================================================================
	// 入力を読む（表と、拾う係 InputRebinder の両方が使う）
	//=========================================================================
	// 入力1つの今の値（ボタンは 0 か 1、軸はその量。gate の決まりで読む。パッドは gamepadIndex 番）
	static float ReadControl(const InputControl& control, InputGate gate, int gamepadIndex);
	// 今押している修飾キー
	static InputModifiers ReadModifiers(InputGate gate);

	//=========================================================================
	// 保存（人ごとの上書き）
	//=========================================================================
	// 上書きを読む（ファイルが無ければ既定のまま。知らない Action・読めない割り当ては飛ばして、ログに出す）。読んだ状態を「保存した状態」にする
	bool LoadOverrides(const std::filesystem::path& path);
	// 既定から変えた Action だけを書く。書いた状態を「保存した状態」にする
	bool SaveOverrides(const std::filesystem::path& path);
	// 保存した状態から変えたか・保存した状態に戻す
	bool IsDirty() const;
	void RevertToSaved();

private:
	// Action 1つ
	struct Action {
		InputActionDesc desc;
		std::vector<InputBinding> bindings; // 今の割り当て
		std::vector<InputBinding> saved;    // 保存した（読んだ）ときの割り当て
		// --- 今のフレームの状態 ---
		bool isDown = false;       // 押している
		bool wasDown = false;      // 前のフレームに押していた
		bool waitRelease = false;  // 一度離すまで効かせない（割り当てを変えた・止めていたのを戻した）
		Vector2 value = {0.0f, 0.0f};
	};

	const Action* Get(InputActionId id) const;
	// 書き換えてよい Action（無効な番号・変えられない Action なら nullptr）
	Action* GetEditable(InputActionId id);
	// 修飾キーが合っているか（割り当ての入力そのものが修飾キーなら、それは数えない）
	bool MatchesModifiers(const InputBinding& binding, InputModifiers held) const;
	// 2つの割り当てが、同時に効いてしまうか
	bool Overlaps(const Action& a, const InputBinding& ba, const Action& b, const InputBinding& bb) const;
	// 聞いてよい状態か（止めている・離すのを待っている間は false）
	bool IsActive(const Action& action) const { return isEnabled_ && !action.waitRelease; }

	std::vector<Action> actions_;
	InputGate gate_;
	InputModifierMatch modifierMatch_;
	InputDeviceMask allowedDevices_;
	bool isEnabled_ = true;
	int gamepadIndex_ = 0; // 読むパッドの番号（0〜3）
};
