参考：
- 前の段階（土台とエディタの Keymap。0.2 層の分け方・0.8 拾う係を動かす場所・0.12 段階3 への申し送り）：[InputAction.md](InputAction.md)
- Unity Input System の Action・Action の種類（Button・Value・Pass-Through）：https://docs.unity3d.com/Packages/com.unity.inputsystem@1.11/manual/Actions.html
- Unity Input System の割り当て（2D Vector の Composite＝上・下・左・右に分けた割り当て・Interactive Rebinding・上書きの保存）：https://docs.unity3d.com/Packages/com.unity.inputsystem@1.11/manual/ActionBindings.html
- nlohmann/json の ordered_json（書いた順に項目を並べる）：https://json.nlohmann.me/api/ordered_json/

# ゲームのキーの割り当て（Input Action の段階3）

- **パートA UserDataPath（新しい）**（`Core/UserDataPath.h`）：プレイヤーのデータの保存先（ゲームのフォルダの `UserData/`）
- **パートB InputActionMap の直し**：割り当ての行（上・下・左・右・横の軸・縦の軸）・名前の表・`InputActionDesc` の比べ・`GetDisplayText`
- **パートC InputRebinder の直し**：行を渡して拾う・軸を反対に動かしたら反対にする
- **パートD InputActionFile（新しい）**：`Parameters/Input/InputActions.json` の読み書き
- **パートE GameInput（新しい）**：ゲームの表・Script の窓口・キーコンフィグ・保存・2人
- **パートF 呼ぶ所**（`Engine.cpp`・`WindowManager.cpp`・`Script.h`）
- **パートG InputActionTable を行に分ける**（全体を差し替え）
- **パートH GameInputPage（新しい）**：Settings → Project → Input
- **パートI vcxproj**
- **パートJ Sandbox**（`.gitignore`・`InputActions.json`・PlayerController・キーコンフィグのテスト Script・テストシーン）
- **パートK Reference**
- **パートL 確かめ方**
- **パートM 表を右クリックで直す**（2026-11-15 に足した。× と「既定」のボタンをやめ、＋ を行の左端へ）

**A → B → C → D → E → F → G → H → I** の順に写すとビルドが通る（C で `InputRebinder::Start` の引数が変わるので、G を写すまでは InputActionTable.cpp の `rebinder_.Start(...)` が通らない。気になるなら C と G を続けて写す）。J・K は最後。

ブランチは `feature/GameInput`（`develop` 70b52f4 から）。答えは写しのブランチ `answer-game-input` にある（Debug・Development・Release でビルドを通し、Sandbox で動きを見た）。

**2026-11-15 本体へ直接入れた**（ブランチ `feature/GameInput`。未コミット）。写しの `70b52f4..answer-game-input` の差分（パートA〜K）をそのまま当てた。写しかけていた6ファイル（vcxproj・filters・InputActionMap.h/.cpp・InputRebinder.h・UserDataPath.h）は答えで上書きした（コメントの言い回しなどの小さな違いだけだった）。

## 0章 決めたこと

### 0.1 層の分け方

```
 ┌ ゲーム（Script）──────────────────┐  ┌ エディタ ─────────────────────────┐
 │ GameInput::IsTriggered("Jump")          │  │ Settings → Project → Input（GameInputPage）│
 │ キーコンフィグの画面（ゲームの UI で作る）│  │ 自分の表（InputGate::Editor）を持つ        │
 └─────────────┬────────────┘  └───────────┬───────────┘
 ┌ Input/Action/GameInput（Release にも入る）─────────────────────┐   │ Save で書いて
 │ プレイヤーごとの表・拾う係・保存（UserData/）・ファイル（InputActionFile）   │ ←┘ ReloadActions
 └────────────────────────┬───────────────────────┘
 ┌ Input/Action/（段階1）InputBinding・InputActionMap（＋行）・InputRebinder（行で拾う）┐
 └──────────────────────────────────────────────────┘
```

### 0.2 表を誰が持つか・Update をどこで呼ぶか

**エンジンの `GameInput`（`Input/Action/`、static）が持つ。** 形は EditorKeymap と同じ。

| 候補 | 選ばない理由 |
|---|---|
| シーン | シーンを変えるたびに作り直すと、上書き・拾っている途中が消える。割り当てはゲーム全体で1つ |
| Script（Component） | どの Entity が持つかを決めねばならず、ほかの Script から探す手間が出る。Unity の PlayerInput のような物は、後から GameInput の上に載せられる |
| System | System は「Component を読んで処理する係」（ARCHITECTURE のフォルダの決まり）。表は Component ではない |

**`GameInput::Update` は `WindowManager::UpdateAll` の窓のループで、メインのウィンドウ（`windows_.front()`。ゲーム画面がある窓。`UpdateCursor` と同じ）の `SetActiveWindow` の後・`sceneManager->Update()` の前に呼ぶ。**

- SceneManager::Update の中で「シーンの Update → Script → SYSTEM」と回るので、その前ならどこから聞いても同じ値。
- 停止中・一時停止中も呼ぶ。止めると「前のフレームに押していたか」が古くなり、Play を押した最初のフレームに、ずっと押していたキーが「押した瞬間」になる。
- SceneManager の中に置かない（窓ごとにあるので、2回呼ばれうる）。
- **拾う係（InputRebinder）も GameInput が持ち、Update の頭で動かす**（EditorKeymap と同じ順番。InputAction.md の 0.8）。ゲームの UI は `StartRebind` で頼み、`GetRebindResult` で結果を聞くだけ。0.8 の順番（拾う→表の Update、拾ったフレームも止める）をゲームごとに書かせると、段階2 と同じ間違い（描く所で拾って何も読めない）が起きる。

### 0.3 既定の割り当てのファイル

**`Parameters/Input/InputActions.json`**（git に入る・製品版も読む。GlobalVariables の Project と同じ所）。

```json
{
    "version": 1,
    "actions": [
        { "name": "Jump", "label": "ジャンプ", "group": "動き", "context": "Gameplay", "bindings": ["Key.Space", "Pad.A"] },
        { "name": "Move", "type": "Vector2", "clampLength": true, "bindings": ["Key.W:+Y", "Key.S:-Y", "Key.A:-X", "Key.D", "Pad.LeftStickX", "Pad.LeftStickY:+Y"] }
    ]
}
```

- `actions` は配列（並び＝画面の順）。Action 1つ＝1つの塊なので、チームで別々の Action を直してもぶつかりにくい。書く順は `name` を先頭に（`ordered_json`）。
- 項目は `InputActionDesc` と同じ：`name`・`label`・`group`・`context`・`tooltip`・`type`・`accepts`（`["Key", "PadButton"]`）・`clampLength`・`fixed`・`bindings`。**書いていない項目は既定の値**（`label` が無ければ名前、`type` が無ければ Button）。Save は既定の値の項目を書かない。
- 読み書きは新しい `InputActionFile`（`Input/Action/`）。InputActionMap はファイルを知らないまま（エディタの Keymap はコードで作るので）。
- ファイルが無ければ Action 0 個で起動する（AL4_01・TD2_2 は今のまま動く）。名前が空・重なり・知らない種類・読めない割り当ては、飛ばしてログに出す。

### 0.4 Script からの窓口

```cpp
if (GameInput::IsTriggered("Jump")) { … }
const Vector2 move = GameInput::GetVector2("Move");
if (GameInput::IsPressed("Fire", 1)) { … } // 2P（最後の引数がプレイヤー。既定は 0）
```

- **名前で聞く。番号（InputActionId）はゲームに出さない。** Input ページで Action を足す・消すと番号がずれる（Play 中に保存しても）。Script が覚えた番号が別の Action を指してしまう。
- 中は名前→番号の表（`unordered_map`。パートB）で引くので、毎フレーム何十回聞いても軽い。
- 知らない名前は false・0 を返し、名前ごとに1回だけ警告を出す。enum をコードで作る案は採らない（ファイルを変えるたびにビルドが要り、誰かが Action を消すとほかの人のビルドが通らない）。
- `Script.h` に `GameInput.h` を入れる（Script を書く人は include しなくてよい）。

### 0.5 プレイヤーの上書きの保存先

**ゲームのフォルダ（起動したフォルダ＝ `Parameters/` を読むのと同じ所）の `UserData/`。エディタも製品版も同じ。** 決めているのは `Core/UserDataPath.h` の `UserDataPath::GetDirectory()` だけ（作りはしない。書く側が `create_directories` する）。

- 中身は `UserData/Input/Player1.json`（2P は `Player2.json`。形は今の `SaveOverrides` のまま。既定から変えた Action だけ）。
- **git には入れない**（遊んだ人ごとの物）。エンジンの `.gitignore` に `/project/Sandbox/UserData/` を足す。ゲームのリポジトリにも `UserData/` を足す（Reference の 11 章に書く）。
- 最初は製品版を `%LOCALAPPDATA%\<ゲームの名前>\` にする案だった（市販のゲームのやり方。Program Files に入れると exe の隣に書けない・上書きの更新で消える・Windows のユーザーごとに分かれる）。今の配り方（zip で配って、展開したフォルダから起動する）ではその利点が出ず、保存の場所が見つけにくい・フォルダごと持ち運べない・ゲームを消してもゴミが残る、の方が困るのでやめた。そのためゲームの名前（`WindowConfig::appName`）も要らない。
- 書けない所（Program Files など）に置くと保存できない（`SaveOverrides` がログに出し、`SaveBindings` が false）。黙って別の場所に逃がさない。配り方が変わったら `UserDataPath::GetDirectory` だけを直す。

### 0.6 Vector2・Value の向きを選ぶ（割り当ての行）

**割り当てを行に分けて見せる**（Unity の 2D Vector Composite の Up・Down・Left・Right と同じ見せ方）。

| 種類 | 行 |
|---|---|
| Button | All（1行。今まで通り） |
| Value | ＋・−（キー・ボタン）・軸（ホイール・トリガー・スティック） |
| Vector2 | 上・下・左・右（キー・ボタン）・横の軸・縦の軸（スティック・マウスの動き） |

- 中の持ち方は変えない（割り当ての平らな一覧＋向き）。行は「入力が軸か」と「向き」から決まる（`InputBindingRows::Of`）。
- 行の ＋ は、その行の向きで拾う。そのため **`InputRebinder::Start` に行を渡す**（拾ってよい入力と向きが行で決まる。上の行ならキー・ボタンだけで向きは +Y）。今までの「元の割り当ての向きを引き継ぐ」はやめた（足すときに向きを選べなかった）。
- **軸の行で軸を − の側へ動かして拾ったら、反対に足す割り当てにする**（縦の軸の行でマウスを上へ動かす → `Mouse.MoveY:-Y`＝上へ動かすと上）。今まで Button の Action だけしていた「動かした向きを使う」を、Value・Vector2 にも広げた。表では「（反対）」と出す。
- 軸は両側に効くので、横・縦の行に入れた物は1つで左右（上下）とも動く。

### 0.7 2人で遊ぶとき

**API を後で変えなくて済むように、形は作る。参加の仕組みはやらない。**

- GameInput はプレイヤーごとに表を持つ。`SetPlayerCount(n)`（1〜4。既定 1）。全員が同じファイルの既定から作り、上書きの保存は人ごと（`Player2.json`）。
- 1P はキーボード・マウス・パッド0、2P 以降はパッド n だけ（`allowedDevices` と `SetGamepadIndex`）。キーボードを2人で分けると、既定の割り当てを人ごとに持たねばならない。
- 2P の表では、キーの割り当ては読まないし、`GetDisplayText` にも出さない（パートB）。
- やらない：ボタンを押した人がパッドを取る「参加」・パッドの付け替え・キーボードを2人で分ける（Reference の 19 章に書く）。

### 0.8 GetMoveAxis

**残し、中身も変えない**（GameInput を通さない）。通すと、ファイルに Move が無いゲームで動かなくなる。Reference には「新しく書くなら GameInput」と書く。Sandbox の PlayerController は GameInput にする（使い方の見本）。

**2026-11-15 消した**（本体へ入れた後に決め直した）。`GetMoveAxis` の中で GameInput を読む形は、下の層（InputManager）が上の層（Input/Action）を読むことになり、割り当ても決め打ちとファイルの2か所に分かれるのでやめた。使っていたのは AL4_01（PlayerController の1か所）と TD2_2（PlayerController の2か所）だけで、チームのメンバーは使っていないので、非推奨を挟まずに消した。

- `InputManager.h/.cpp` の `GetMoveAxis` を消し、`GameInput::GetVector2("Move")` を使うようコメントを置いた
- `CameraRelativeInput.h` のコメント、Reference の 10・11・22・24 章の例を `GameInput::GetVector2("Move")`（ジャンプは `GameInput::IsTriggered("Jump")`）にした
- ゲーム側：エンジンのサブモジュールを上げるときに、`Parameters/Input/InputActions.json` に Move（Vector2・長さを 1 まで・`Key.W:+Y`・`Key.S:-Y`・`Key.A:-X`・`Key.D`・矢印・`Pad.LeftStickX`・`Pad.LeftStickY:+Y`）を作り、`InputManager::GetMoveAxis()` を `GameInput::GetVector2("Move")` にする（`.gitignore` に `UserData/` も足す）

### 0.9 エディタの Input ページ

- **ゲームの表（GameInput）を直接直さず、自分の表（`InputGate::Editor`）を持つ。** ゲームの表は Game の決まりで読むので、ImGui を触っている間（＝ページを触っている間）は何も拾えない。修飾キーの合わせ方はゲームと同じ AtLeast（ぶつかりの見え方をゲームとそろえる）。
- ページは「ファイルの中身（saved）」と「直している中身（edited。defaults が今の割り当て）」を持つ。表はそこから作り直す見せ物で、表の Action の番号＝ edited の番号。**表の中の Action の名前は番号にする**（直している途中の名前は空・重なりがありうる。表の名前は1つだけでないといけない）。
- 表の「既定」は**保存した割り当てに戻す**（`InputActionTable` の吹き出しを変えられるようにした）。「変えられない」Action もページでは直せる（変えられないのはプレイヤー）。
- Action を足す・消す・名前や種類を変えたら表を作り直す（拾うのはやめる）。割り当ての変更は、毎フレーム表から edited に写す。
- Save は名前が空・重なりがあると保存しない（ページの上に赤で出す）。保存したら `GameInput::ReloadActions()`（Play 中でもすぐ効く。保存していないプレイヤーの上書きは消える）。
- 一番下に「プレイヤーの上書き」（エディタで遊んで保存したキーコンフィグ）の有無と、消すボタン。消さないと、既定を直してもその Action はプレイヤーの割り当てのまま（上書きは Action ごと）。

### 0.10 やらないこと・知っておくこと

| やらない物・知っておくこと | 理由・今どうするか |
|---|---|
| マウスの動きとスティックを1つの Action に混ぜる | 単位が違う（マウスはそのフレームに動いた数、スティックは倒した量で時間を掛けて使う）。`LookMouse`（MouseMove だけ）と `LookStick`（PadAxis だけ）に分けるよう Reference に書く。割り当てごとの倍率（Unity の Processor）は入れない |
| Action のまとまりごとに止める（Unity の Action Map） | 入れない。context と group で足り、メニューの間は Script が聞かなければよい |
| 上書きは Action ごと | 変えた Action は割り当ての全部を持つ。既定に足した割り当ては、上書きのある Action には届かない（段階1 の 0.9 のまま） |
| 行の名前（「上」「横の軸」）をエンジンが出す | ゲームの画面の文字なので、ゲームが決める（エディタの表は自分で持つ） |
| キーコンフィグの画面の部品 | エンジンには作らない（ゲームの UI で作る）。Sandbox の `KeyConfigTest` が Text だけで作った見本 |
| Play 中に Input ページを保存 | ゲームの表を作り直すので、プレイヤーが保存していない上書きと、拾っている途中は消える |

# パートA UserDataPath（新しい）

## A-1 `Core/UserDataPath.h`（新しい。.cpp は無い）

```cpp
#pragma once
#include <filesystem>

/// <summary>
/// プレイヤーのデータの場所（プレイヤーが変えたキーの割り当てなど、遊ぶ人ごとに残す物の保存先）
/// <para>ゲームのフォルダ（起動したフォルダ＝ Parameters/ を読むのと同じ所）の UserData/。エディタも製品版も同じ</para>
/// <para>git には入れない（ゲームのリポジトリの .gitignore に UserData/ を足す。チームの既定の割り当ては Parameters/ の方）</para>
/// <para>書けない所（Program Files など）にゲームを置くと保存できない（書く側がログに出す）。保存先を変えるときは、ここだけ直す</para>
/// </summary>
namespace UserDataPath {
// 保存先のフォルダ（作りはしない。書く側が create_directories する）
inline const std::filesystem::path& GetDirectory() {
	static const std::filesystem::path directory = "UserData";
	return directory;
}
} // namespace UserDataPath
```

# パートB InputActionMap の直し

## B-1 `Input/Action/InputActionMap.h`

include に3つ足す。

```cpp
#include <cstdint>
#include <filesystem>
#include <functional>    // ← 足す（std::hash・std::equal_to）
#include <span>          // ← 足す
#include <string>
#include <string_view>
#include <unordered_map> // ← 足す
#include <vector>
```

`InputActionDesc` の最後に比べる関数を足し（Input ページで「保存した中身と同じか」を見る）、その後ろに行の enum と namespace を足す。

```cpp
	std::vector<InputBinding> defaults;              // 既定の割り当て（空なら、割り当てなし）

	bool operator==(const InputActionDesc&) const = default; // ← 足す
};

// ↓ ここから足す
/// <summary>
/// 割り当ての行（画面で割り当てを並べるときの1行。Value・Vector2 は、向きごとのキー・ボタンと、軸に分けて見せる）
/// <para>Vector2 なら 上・下・左・右（キー・ボタン）と 横・縦（スティック・マウスの動き）。どの行かは、割り当ての入力の種類と向きで決まる（InputBindingRows::Of）</para>
/// <para>中の持ち方は変えない（割り当ては Action ごとの平らな一覧＋向き）。行は、見せるときと足すときに使うだけ</para>
/// </summary>
enum class InputBindingRow : uint8_t {
	All,        // Button：全部（向きは見ない）
	Positive,   // Value：押すと＋（キー・ボタン）
	Negative,   // Value：押すと−（キー・ボタン）
	Axis,       // Value：軸（ホイール・トリガー・スティック。そのまま足す）
	Up,         // Vector2：押すと上（キー・ボタン）
	Down,       // Vector2：押すと下
	Left,       // Vector2：押すと左
	Right,      // Vector2：押すと右
	Horizontal, // Vector2：横の軸（スティック・マウスの動き。＋に倒すと右）
	Vertical,   // Vector2：縦の軸（＋に倒すと上）
};

/// <summary>
/// 割り当ての行の決まり（エディタの表と、ゲームのキーコンフィグの画面の両方で使う）
/// </summary>
namespace InputBindingRows {
// 種類ごとの行の並び（Button は All だけ、Value は ＋・−・軸、Vector2 は 上・下・左・右・横・縦）
std::span<const InputBindingRow> Get(InputActionType type);
// 割り当てがどの行か（キー・ボタンは向き、軸は横か縦か）
InputBindingRow Of(InputActionType type, const InputBinding& binding);
// 行に新しく足す割り当ての向き（軸の行は、＋に倒したときの向き。−に倒して拾ったら、InputRebinder が反対にする）
InputDirection DirectionOf(InputBindingRow row);
// 行に入れてよい入力（向きの行はキー・ボタンだけ、軸の行は軸だけ、All は全部）
InputDeviceMask DevicesOf(InputBindingRow row);
// 軸の行か（Axis・Horizontal・Vertical）
bool IsAxisRow(InputBindingRow row);
} // namespace InputBindingRows
// ↑ ここまで
```

コメントを2つ直す。

```cpp
	// 名前でも聞ける（名前の表で引くので、毎フレーム何十回聞いても軽い。知らない名前なら false・0）
	bool IsPressed(std::string_view name) const { return IsPressed(Find(name)); }
```

```cpp
	// 画面に出す文字（この表で読む入力の、1つ目の割り当て。割り当てが無ければ空）。メニューのショートカットの欄などに
	std::string GetDisplayText(InputActionId id) const;
```

private の `actions_` の前に、名前の表を足す。

```cpp
	// 名前の表のハッシュ（std::string_view のまま引けるように。is_transparent があると、find に string を作らずに渡せる）
	struct NameHash {
		using is_transparent = void;
		size_t operator()(std::string_view name) const noexcept { return std::hash<std::string_view>{}(name); }
	};

	std::vector<Action> actions_;
	std::unordered_map<std::string, uint32_t, NameHash, std::equal_to<>> indexByName_; // 名前 → actions_ の番号（名前で聞くのを、毎フレーム何十回でも軽くする）
	InputGate gate_;
```

## B-2 `Input/Action/InputActionMap.cpp`

無名の namespace の終わり（`IsVertical` の後ろ）に、行の並びを足す。

```cpp
// 向きが Y か（+Y・-Y）
bool IsVertical(InputDirection direction) { return direction == InputDirection::PositiveY || direction == InputDirection::NegativeY; }

// ↓ ここから足す
// 種類ごとの行の並び
constexpr InputBindingRow kButtonRows[] = {InputBindingRow::All};
constexpr InputBindingRow kValueRows[] = {InputBindingRow::Positive, InputBindingRow::Negative, InputBindingRow::Axis};
constexpr InputBindingRow kVector2Rows[] = {InputBindingRow::Up, InputBindingRow::Down, InputBindingRow::Left, InputBindingRow::Right, InputBindingRow::Horizontal, InputBindingRow::Vertical};
// 軸の入力（マウスの動き・ホイール・パッドの軸）
constexpr InputDeviceMask kAxisDevices = InputDeviceMask::MouseMove | InputDeviceMask::MouseWheel | InputDeviceMask::PadAxis;
// ↑ ここまで
} // namespace
```

その後ろ（「作る・登録」の大見出しの前）に、行の関数を足す。

```cpp
//=============================================================================
// 割り当ての行
//=============================================================================
// ===== 種類ごとの行の並び =====
std::span<const InputBindingRow> InputBindingRows::Get(InputActionType type) {
	switch (type) {
	case InputActionType::Button:
		return kButtonRows;
	case InputActionType::Value:
		return kValueRows;
	case InputActionType::Vector2:
		return kVector2Rows;
	}
	return kButtonRows;
}

// ===== 割り当てがどの行か =====
InputBindingRow InputBindingRows::Of(InputActionType type, const InputBinding& binding) {
	const bool isAxis = binding.control.IsAxis();
	switch (type) {
	case InputActionType::Button:
		return InputBindingRow::All;
	case InputActionType::Value:
		if (isAxis) {
			return InputBindingRow::Axis;
		}
		return IsNegative(binding.direction) ? InputBindingRow::Negative : InputBindingRow::Positive;
	case InputActionType::Vector2:
		// 軸は、どちらの向き（X か Y）に足すかで横・縦。-X・-Y は「反対に足す軸」で、同じ行に入る
		if (isAxis) {
			return IsVertical(binding.direction) ? InputBindingRow::Vertical : InputBindingRow::Horizontal;
		}
		switch (binding.direction) {
		case InputDirection::PositiveY:
			return InputBindingRow::Up;
		case InputDirection::NegativeY:
			return InputBindingRow::Down;
		case InputDirection::NegativeX:
			return InputBindingRow::Left;
		case InputDirection::PositiveX:
			return InputBindingRow::Right;
		}
		return InputBindingRow::Right;
	}
	return InputBindingRow::All;
}

// ===== 行に新しく足す割り当ての向き =====
InputDirection InputBindingRows::DirectionOf(InputBindingRow row) {
	switch (row) {
	case InputBindingRow::Negative:
	case InputBindingRow::Left:
		return InputDirection::NegativeX;
	case InputBindingRow::Up:
	case InputBindingRow::Vertical:
		return InputDirection::PositiveY;
	case InputBindingRow::Down:
		return InputDirection::NegativeY;
	default:
		return InputDirection::PositiveX; // All・Positive・Axis・Right・Horizontal
	}
}

// ===== 行に入れてよい入力 =====
InputDeviceMask InputBindingRows::DevicesOf(InputBindingRow row) {
	if (row == InputBindingRow::All) {
		return InputDeviceMask::All;
	}
	return IsAxisRow(row) ? kAxisDevices : InputDeviceMask::Buttons;
}

// ===== 軸の行か =====
bool InputBindingRows::IsAxisRow(InputBindingRow row) {
	return row == InputBindingRow::Axis || row == InputBindingRow::Horizontal || row == InputBindingRow::Vertical;
}
```

`AddAction` の後半と `Find` を、名前の表を使う形にする。

```cpp
// ===== Action を足す =====
InputActionId InputActionMap::AddAction(InputActionDesc desc) {
	HTT_ASSERT_MSG(!desc.name.empty(), "Input Action の名前が空です");
	const InputActionId existing = Find(desc.name);
	if (existing.IsValid()) {
		LogManager::Warning("[InputActionMap] 同じ名前の Action がもうあります: " + desc.name);
		return existing;
	}
	const uint32_t index = static_cast<uint32_t>(actions_.size()); // ← ここから
	indexByName_.emplace(desc.name, index);
	Action action;
	action.bindings = desc.defaults;
	action.saved = desc.defaults;
	action.desc = std::move(desc);
	actions_.push_back(std::move(action));
	return {index};                                                // ← ここまで（index を先に決めて、名前の表に入れる）
}

// ===== 名前から番号 =====
InputActionId InputActionMap::Find(std::string_view name) const {
	const auto it = indexByName_.find(name);
	return (it != indexByName_.end()) ? InputActionId{it->second} : InputActionId{};
}
```

`GetDisplayText` は、この表で読まない入力（2P の表のキー）を飛ばす。

```cpp
	for (const InputBinding& binding : action->bindings) {
		// 表で読まない入力（2P の表のキーなど）は出さない
		if (!binding.IsEmpty() && Contains(allowedDevices_, binding.control.device)) {
			return InputBindingText::ToDisplayString(binding);
		}
	}
```

# パートC InputRebinder の直し

## C-1 `Input/Action/InputRebinder.h`

`Start` に行を足す。

```cpp
	// 拾い始める（map の id の、index 番目の割り当てを作る。index が割り当ての数と同じなら、新しく足す分）
	// row は割り当てを入れる行（InputBindingRows::Get の1つ）。拾ってよい入力と、作る割り当ての向きが行で決まる（Vector2 の「上」の行ならキー・ボタンだけで、向きは +Y）
	// 軸の行（横・縦・Value の軸）で、軸を − の側へ動かして拾ったら、反対に足す割り当てにする（上下が逆のスティックなど。動かした向きが、その行の向きになる）
	void Start(const InputActionMap& map, InputActionId id, int index, InputBindingRow row);
```

```cpp
	int GetIndex() const { return index_; }
	InputBindingRow GetRow() const { return row_; } // ← 足す
	// 拾った割り当て（Captured を返した後だけ。向きは行で決まる）
	const std::optional<InputBinding>& GetCaptured() const { return captured_; }
```

```cpp
	InputDeviceMask accepts_ = InputDeviceMask::None; // 拾ってよい入力
	InputBindingRow row_ = InputBindingRow::All;       // 割り当てを入れる行（← 足す）
	InputDirection direction_ = InputDirection::PositiveX; // 作る割り当ての向き（行から）
```

## C-2 `Input/Action/InputRebinder.cpp`

無名の namespace に `Reverse` を足す。

```cpp
constexpr InputControl kCancelKey = {InputDevice::Key, DIK_ESCAPE}; // やめるキー

// 反対の向き（+X ⇔ -X、+Y ⇔ -Y）
InputDirection Reverse(InputDirection direction) {
	switch (direction) {
	case InputDirection::PositiveX:
		return InputDirection::NegativeX;
	case InputDirection::NegativeX:
		return InputDirection::PositiveX;
	case InputDirection::PositiveY:
		return InputDirection::NegativeY;
	case InputDirection::NegativeY:
		return InputDirection::PositiveY;
	}
	return direction;
}
} // namespace
```

`Start` の頭を差し替える（元の割り当ての向きを引き継ぐ3行は消す）。

```cpp
void InputRebinder::Start(const InputActionMap& map, InputActionId id, int index, InputBindingRow row) {
	map_ = &map;
	action_ = id;
	index_ = index;
	row_ = row;
	// 拾ってよい入力と向きは、行で決める（Vector2 の「上」の行なら、キー・ボタンだけで、向きは +Y）
	accepts_ = map.GetAcceptedDevices(id) & InputBindingRows::DevicesOf(row);
	direction_ = InputBindingRows::DirectionOf(row);
	isFirstUpdate_ = true;
	…（ここから下は今のまま）
```

`Update` の、割り当てを作る所を差し替える。

```cpp
			InputBinding binding = {control, held, direction_};
			if (control.IsAxis()) {
				const bool isMovedNegative = InputActionMap::ReadControl(control, map_->GetGate(), map_->GetGamepadIndex()) < 0.0f;
				if (isButtonAction) {
					// ボタンの Action に軸（ホイールなど）を割り当てるときは、動かした向きを使う（ホイールの下で「次へ」など）
					binding.direction = isMovedNegative ? InputDirection::NegativeX : InputDirection::PositiveX;
				} else if (isMovedNegative) {
					// 軸の行で − の側へ動かした：反対に足す（動かした向きが、その行の向きになる）
					binding.direction = Reverse(direction_);
				}
			}
			Finish(binding);
			return Result::Captured;
```

# パートD InputActionFile（新しい）

## D-1 `Input/Action/InputActionFile.h`（新しい）

```cpp
#pragma once
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

#include "HTTEngine/Input/Action/InputActionMap.h"

/// <summary>
/// ゲームの Action の一覧と既定の割り当てのファイル（Parameters/Input/InputActions.json。git に入れて、チームで1つ）
/// <para>Action 1つの決まり（InputActionDesc）を、並び（画面に出す順）のまま読み書きする。割り当ては段階1の文字（"Key.Space"・"Key.W:+Y"）</para>
/// <para>書いていない項目は既定の値（label が無ければ名前、type が無ければ Button）。Save は既定の値の項目を書かない（ファイルを短く、差分を読みやすく）</para>
/// <para>表（InputActionMap）はファイルを知らない。ゲームの表（GameInput）と、エディタの Input ページ（GameInputPage）がこれで読む</para>
/// </summary>
/// <example>
/// {
///     "version": 1,
///     "actions": [
///         { "name": "Jump", "label": "ジャンプ", "group": "動き", "bindings": ["Key.Space", "Pad.A"] },
///         { "name": "Move", "type": "Vector2", "clampLength": true, "bindings": ["Key.W:+Y", "Key.S:-Y", "Key.A:-X", "Key.D"] }
///     ]
/// }
/// </example>
namespace InputActionFile {
// 読む（ファイルが無い・読めなければ nullopt。読めないときだけログに出す）
// 名前が空・重なっている Action、知らない種類・読めない割り当ては飛ばしてログに出す（落とさない）
std::optional<std::vector<InputActionDesc>> Load(const std::filesystem::path& path);
// 書く（フォルダが無ければ作る）。書けなければ false
bool Save(const std::filesystem::path& path, std::span<const InputActionDesc> actions);
} // namespace InputActionFile
```

## D-2 `Input/Action/InputActionFile.cpp`（新しい）

```cpp
#include "InputActionFile.h"

#include <fstream>
#include <iomanip>
#include <string>
#include <unordered_set>

#include <externals/magic_enum/magic_enum.hpp>
#include <externals/nlohmann/json.hpp>

#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/String/ConvertString.h"

namespace {
using json = nlohmann::json;
using ordered_json = nlohmann::ordered_json; // 書いた順に項目を並べる json（name を先頭にして、読みやすく）

constexpr int kFileVersion = 1; // ファイルの形の版（形を変えたら上げる）

//=============================================================================
// 読む
//=============================================================================
// ===== 文字の項目（無い・文字でなければ fallback）=====
std::string ReadString(const json& object, const char* key, const std::string& fallback = {}) {
	const auto it = object.find(key);
	return (it != object.end() && it->is_string()) ? it->get<std::string>() : fallback;
}

// ===== bool の項目（無ければ false）=====
bool ReadBool(const json& object, const char* key) {
	const auto it = object.find(key);
	return it != object.end() && it->is_boolean() && it->get<bool>();
}

// ===== 割り当てられる入力（"accepts": ["Key", "PadButton"]。無ければ None＝種類で決める）=====
InputDeviceMask ReadAccepts(const json& object, const std::string& actionName) {
	InputDeviceMask mask = InputDeviceMask::None;
	const auto it = object.find("accepts");
	if (it == object.end() || !it->is_array()) {
		return mask;
	}
	for (const json& item : *it) {
		const std::optional<InputDevice> device = item.is_string() ? magic_enum::enum_cast<InputDevice>(item.get<std::string>()) : std::nullopt;
		if (!device || *device == InputDevice::None) {
			LogManager::Warning("[InputActionFile] 知らない入力の種類を飛ばしました: " + actionName + " " + item.dump());
			continue;
		}
		mask = mask | static_cast<InputDeviceMask>(1 << static_cast<uint8_t>(*device));
	}
	return mask;
}

// ===== 割り当ての一覧（読めない物だけ飛ばす）=====
std::vector<InputBinding> ReadBindings(const json& object, const std::string& actionName) {
	std::vector<InputBinding> bindings;
	const auto it = object.find("bindings");
	if (it == object.end() || !it->is_array()) {
		return bindings;
	}
	for (const json& text : *it) {
		const std::optional<InputBinding> binding = text.is_string() ? InputBindingText::Parse(text.get<std::string>()) : std::nullopt;
		if (!binding) {
			LogManager::Warning("[InputActionFile] 読めない割り当てを飛ばしました: " + actionName + " " + text.dump());
			continue;
		}
		bindings.push_back(*binding);
	}
	return bindings;
}

// ===== Action 1つ（名前が無ければ nullopt）=====
std::optional<InputActionDesc> ReadAction(const json& object) {
	if (!object.is_object()) {
		return std::nullopt;
	}
	InputActionDesc desc;
	desc.name = ReadString(object, "name");
	if (desc.name.empty()) {
		return std::nullopt;
	}
	desc.label = ReadString(object, "label", desc.name);
	desc.group = ReadString(object, "group");
	desc.context = ReadString(object, "context");
	desc.tooltip = ReadString(object, "tooltip");
	const std::string typeName = ReadString(object, "type", std::string(magic_enum::enum_name(InputActionType::Button)));
	const std::optional<InputActionType> type = magic_enum::enum_cast<InputActionType>(typeName);
	if (!type) {
		LogManager::Warning("[InputActionFile] 知らない種類なので Button にしました: " + desc.name + " " + typeName);
	}
	desc.type = type.value_or(InputActionType::Button);
	desc.accepts = ReadAccepts(object, desc.name);
	desc.clampLength = ReadBool(object, "clampLength");
	desc.isFixed = ReadBool(object, "fixed");
	desc.defaults = ReadBindings(object, desc.name);
	return desc;
}


//=============================================================================
// 書く
//=============================================================================
// ===== Action 1つ（既定の値の項目は書かない）=====
ordered_json WriteAction(const InputActionDesc& desc) {
	ordered_json object;
	object["name"] = desc.name;
	if (desc.label != desc.name) {
		object["label"] = desc.label;
	}
	if (!desc.group.empty()) {
		object["group"] = desc.group;
	}
	if (!desc.context.empty()) {
		object["context"] = desc.context;
	}
	if (!desc.tooltip.empty()) {
		object["tooltip"] = desc.tooltip;
	}
	if (desc.type != InputActionType::Button) {
		object["type"] = std::string(magic_enum::enum_name(desc.type));
	}
	if (desc.accepts != InputDeviceMask::None) {
		ordered_json accepts = ordered_json::array();
		for (const InputDevice device : magic_enum::enum_values<InputDevice>()) {
			if (device != InputDevice::None && Contains(desc.accepts, device)) {
				accepts.push_back(std::string(magic_enum::enum_name(device)));
			}
		}
		object["accepts"] = std::move(accepts);
	}
	if (desc.clampLength) {
		object["clampLength"] = true;
	}
	if (desc.isFixed) {
		object["fixed"] = true;
	}
	ordered_json bindings = ordered_json::array();
	for (const InputBinding& binding : desc.defaults) {
		bindings.push_back(InputBindingText::ToString(binding));
	}
	object["bindings"] = std::move(bindings);
	return object;
}
} // namespace


//=============================================================================
// 読む・書く
//=============================================================================
// ===== 読む =====
std::optional<std::vector<InputActionDesc>> InputActionFile::Load(const std::filesystem::path& path) {
	std::ifstream ifs(path);
	if (!ifs) {
		return std::nullopt; // まだ作っていない（ログは出さない。Action 0 個で動く）
	}
	// 例外を投げない版のパース
	const json root = json::parse(ifs, nullptr, false);
	if (root.is_discarded() || !root.is_object()) {
		LogManager::Error("[InputActionFile] Action のファイルを読めません: " + ToUtf8(path));
		return std::nullopt;
	}
	if (root.value("version", 0) != kFileVersion) {
		LogManager::Warning("[InputActionFile] Action のファイルの版が違います: " + ToUtf8(path));
	}

	std::vector<InputActionDesc> actions;
	std::unordered_set<std::string> names; // もう読んだ名前（重なりを飛ばす）
	const auto list = root.find("actions");
	if (list == root.end() || !list->is_array()) {
		return actions;
	}
	for (const json& object : *list) {
		std::optional<InputActionDesc> desc = ReadAction(object);
		if (!desc) {
			LogManager::Warning("[InputActionFile] 名前の無い Action を飛ばしました: " + object.dump());
			continue;
		}
		if (!names.insert(desc->name).second) {
			LogManager::Warning("[InputActionFile] 同じ名前の Action を飛ばしました: " + desc->name);
			continue;
		}
		actions.push_back(std::move(*desc));
	}
	return actions;
}

// ===== 書く =====
bool InputActionFile::Save(const std::filesystem::path& path, std::span<const InputActionDesc> actions) {
	ordered_json list = ordered_json::array();
	for (const InputActionDesc& desc : actions) {
		list.push_back(WriteAction(desc));
	}
	ordered_json root;
	root["version"] = kFileVersion;
	root["actions"] = std::move(list);

	std::error_code ec;
	std::filesystem::create_directories(path.parent_path(), ec);
	std::ofstream ofs(path);
	if (!ofs) {
		LogManager::Error("[InputActionFile] Action のファイルを書けません: " + ToUtf8(path));
		return false;
	}
	ofs << std::setw(4) << root << std::endl;
	LogManager::Log("[InputActionFile] 保存しました: " + ToUtf8(path));
	return true;
}
```

# パートE GameInput（新しい）

## E-1 `Input/Action/GameInput.h`（新しい）

```cpp
#pragma once
#include <filesystem>
#include <string_view>

#include "HTTEngine/Input/Action/InputActionMap.h"
#include "HTTEngine/Input/Action/InputRebinder.h"
#include "HTTEngine/Math/Vector2.h"

/// <summary>
/// ゲームの操作の割り当て（Input Action）。Script から、キー・ボタンではなく Action の名前で入力を読む
/// <para>Action と既定の割り当ては Parameters/Input/InputActions.json（git に入れて、チームで1つ。エディタの Settings → Project → Input で直す）</para>
/// <para>プレイヤーが変えた割り当ては SaveBindings で、ゲームのフォルダの UserData/Input/Player1.json に保存する（既定から変えた Action だけ。UserDataPath.h）</para>
/// <para>読む決まりは InputGate::Game（エディタのパネルを触っている間は押していない）。修飾キーは「書いた物を押していればよい」（Shift を押しながらでもジャンプできる）</para>
/// <para>プレイヤーは最大4人。1P はキーボード・マウス・パッド0、2P 以降はパッド n だけ（n 人目＝n 番のパッド）。全員が同じファイルの既定から始まり、上書きは人ごとに保存する</para>
/// </summary>
/// <example>
/// if (GameInput::IsTriggered("Jump")) { … }
/// const Vector2 move = GameInput::GetVector2("Move");
/// if (GameInput::IsPressed("Fire", 1)) { … } // 2P
/// </example>
class GameInput {
public:
	static constexpr int kMaxPlayers = 4;                                       // プレイヤーの数の上限（XInput のパッドの数）
	static constexpr const char* kActionsPath = "Parameters/Input/InputActions.json"; // Action と既定の割り当てのファイル

	//=========================================================================
	// エンジンが呼ぶ
	//=========================================================================
	// Action のファイルを読み、1P の表を作る（Engine::Initialize から1回）
	static void Initialize();
	// 入力を読む（毎フレーム1回。WindowManager::UpdateAll の、メインのウィンドウのシーンの Update の前。停止中・一時停止中も呼ぶ）
	// 割り当てを拾っていれば、表の Update の前に拾う係を動かし、拾えたら表に入れる
	static void Update();
	// Action のファイルを読み直す（エディタの Input ページで保存したとき）。表を作り直し、保存した上書きを読み直す（保存していない上書きは消える）
	static void ReloadActions();

	//=========================================================================
	// 聞く（action は Action の名前。player は 0 が 1P。知らない名前なら false・0 で、名前ごとに1回だけ警告を出す）
	//=========================================================================
	static bool IsPressed(std::string_view action, int player = 0);   // 押している間
	static bool IsTriggered(std::string_view action, int player = 0); // 押した瞬間
	static bool IsReleased(std::string_view action, int player = 0);  // 離した瞬間
	static float GetValue(std::string_view action, int player = 0);   // Value の量（Button は 0 か 1、Vector2 は長さ）
	static Vector2 GetVector2(std::string_view action, int player = 0); // Vector2 の量（x が右・y が上が＋）
	// 画面に出す割り当ての文字（"Space"・"Pad A"。1つ目の割り当て。無ければ空）。「Space でジャンプ」の案内に
	static std::string GetDisplayText(std::string_view action, int player = 0);

	//=========================================================================
	// プレイヤーの数
	//=========================================================================
	// 1〜kMaxPlayers。増やした人は既定＋保存した上書きから始まる。減らした人の保存していない上書きは消える
	static void SetPlayerCount(int count);
	static int GetPlayerCount();

	//=========================================================================
	// キーコンフィグの画面から
	//=========================================================================
	// その人の表（割り当ての一覧 GetBindings・ぶつかり FindConflicts・既定に戻す ResetToDefault・外す RemoveBinding など。直接書き換えてよい）
	// Action を足す・消すのはファイルで（表に AddAction しない）。表は ReloadActions・SetPlayerCount で作り直すので、参照をフレームをまたいで持たない
	static InputActionMap& GetMap(int player = 0);
	// 割り当てを拾い始める（次に押したキー・ボタンを、action の row の行に入れる）。index は入れ替える割り当ての番号（GetBindings の番号。-1 なら足す）
	// 拾っている間は、その人の表の全部の Action が効かない（拾うキーでジャンプしないように）。拾えたフレームも効かず、戻したときに押していた物は離すまで効かない
	// Esc でやめる（Esc は割り当てられない）。拾い始めたときに押していた物（決定のボタン）は、離して押し直すまで拾わない
	static void StartRebind(std::string_view action, InputBindingRow row, int index = -1, int player = 0);
	// 拾うのをやめる
	static void CancelRebind();
	// 拾っているか
	static bool IsRebinding();
	// このフレームの拾う係の結果（Listening の間・拾えた（Captured）・やめた（Canceled）。Captured と Canceled はそのフレームだけ）
	// Captured のフレームには、もう表に入っている（ぶつかりは GetMap().FindConflicts で調べて見せる）
	static InputRebinder::Result GetRebindResult();
	// 拾っている・拾った Action（GetMap().GetDesc で名前などを聞く）と、割り当ての番号
	static InputActionId GetRebindAction();
	static int GetRebindIndex();

	//=========================================================================
	// 保存（プレイヤーの上書き）
	//=========================================================================
	// 既定から変えた Action だけを書く。書けなければ（ゲームを書けない所に置いた）false で、ログに出す
	static bool SaveBindings(int player = 0);
	// 保存した割り当てに戻す（キーコンフィグの画面の「やめる」）
	static void RevertBindings(int player = 0);
	// 保存したか（保存した状態から変えていないか）
	static bool IsBindingsDirty(int player = 0);
	// 保存先（UserData/Input/Player1.json。2P は Player2.json）
	static std::filesystem::path GetBindingsPath(int player = 0);
};
```

## E-2 `Input/Action/GameInput.cpp`（新しい）

- 表は `std::vector<InputActionMap>`（数＝プレイヤーの数）。人数を変える・Action を読み直すときは、表が動くので、先に拾うのをやめる（拾う係は表をポインタで見ている）。
- 拾う係の結果（`lastResult`）は、Update の頭で毎フレーム Idle に戻す。Captured・Canceled は、そのフレームの Script だけが見る。

```cpp
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
```

# パートF 呼ぶ所

## F-1 `Engine.cpp`

```cpp
// Input
#include "HTTEngine/Input/InputManager.h"
#include "HTTEngine/Input/Action/GameInput.h" // ← 足す
```

`EngineSystems::Initialize()` の次に1行足す（ウィンドウ・シーンを作る `AddWindow` より前。エディタの Input ページは `AddWindow` の中の `ImGuiManager::Initialize` で作られ、保存のときに GameInput を読み直させる）。

```cpp
	EngineSystems::Initialize(); // EntityManager と全 System（Component の登録もここ）
	GameInput::Initialize();     // ゲームの操作の割り当て（ウィンドウ・シーンを作る前。エディタの Input ページもこれを読み直す）
```

## F-2 `Window/WindowManager.cpp`

```cpp
#include "HTTEngine/Input/InputManager.h"
#include "HTTEngine/Input/Action/GameInput.h" // ← 足す
```

```cpp
#ifdef USE_IMGUI
#include "HTTEngine/Editor/Keymap/EditorKeymap.h"
#include "HTTEngine/Editor/Keymap/GameInputPage.h" // ← 足す（パートH）
#include "HTTEngine/Editor/Windows/ControlWindow.h"
#endif
```

`UpdateAll` の窓のループ。

```cpp
	for (WindowSet& w : windows_) {
		bool isFocused = (focused == w.window->GetHWND());
		InputManager::SetActiveWindow(isFocused ? w.window->GetHWND() : nullptr);
		// ↓ ここから足す
		// ゲームの操作の割り当てを読む（ゲームの画面があるメインのウィンドウの番に、1フレームに1回）
		// シーンの Update・Script・SYSTEM より前（どれから聞いても同じ値）。停止中も呼ぶ（Play を押した最初のフレームに、押していたキーを「押した瞬間」にしない）
		if (&w == &windows_.front()) {
			GameInput::Update();
		}
		// ↑ ここまで
		// シーンマネージャの基底クラス
		if (w.sceneManager) {
			w.sceneManager->Update();
		}

		#ifdef USE_IMGUI
		// エディタの操作の割り当てを読む（ImGui のウィンドウは1つだけなので、1フレームに1回。このウィンドウが手前かを見るので、SetActiveWindow の後）
		// Settings → Project → Input の表の、割り当てを拾う係も動かす（入力を読めるのは、UpdateAll の中だけ）
		if (w.window->GetWindowConfig().isImGui) {
			EditorKeymap::Update();
			GameInputPage::Update(); // ← 足す
		}
```

## F-3 `ECS/Script.h`

```cpp
#include "HTTEngine/ECS/GameObject.h"
#include "HTTEngine/Input/Action/GameInput.h" // ← 足す
```

```cpp
// （GameComponent.h も入っているので、Collider の当たり・音・カメラ・LoadScene もこれだけで使える。入力は GameInput（Action の名前で読む）も入っている。キーを直接読むなら InputManager.h を足す）
```

# パートG InputActionTable を行に分ける

Button は今まで通り1行。Value・Vector2 は行（0.6）ごとに1行で、1行目にだけ名前と「既定」を出す。入れられる入力が無い行（ホイールだけの Value の ＋・− など）は出さない。

## G-1 `Editor/Widgets/InputActionTable.h`（全体を差し替え）

```cpp
#pragma once
#include <string>

#include "HTTEngine/Input/Action/InputActionMap.h"
#include "HTTEngine/Input/Action/InputRebinder.h"

/// <summary>
/// Input Action の表の割り当てを直す ImGui の表（Settings の Keymap ページ・Input ページ。表1つに1つ持つ）
/// <para>Action を group ごとに見出しを付けて並べ、割り当てをボタンで出す。ボタンを押すと、次に押したキー・ボタンを拾って入れ替える</para>
/// <para>Value・Vector2 は、割り当てを行に分けて出す（Vector2 なら 上・下・左・右 のキー・ボタンと、横・縦 の軸。InputBindingRow）。各行の ＋ は、その行の向きで拾う</para>
/// <para>ぶつかっている割り当ては赤くし、相手の名前と「相手の方を外す」ボタンを出す</para>
/// </summary>
class InputActionTable {
public:
	// resetTooltip は、行の右の「既定」ボタンの説明（Input ページでは「保存した割り当てに戻す」）
	explicit InputActionTable(std::string resetTooltip = "既定の割り当てに戻す") : resetTooltip_(std::move(resetTooltip)) {}

	// 表を描く（ページの中から毎フレーム。拾っている間は、案内のモーダルも出す）
	void Draw(InputActionMap& map);
	// 拾う係を動かし、拾えたら map に入れる（毎フレーム1回、フレームの頭に。表の Update の前）
	// <para>入力を読めるのはフレームの頭（WindowManager::UpdateAll の中）だけなので、Draw の中では動かさない</para>
	// <para>前のフレームに表を描いていなければ（ページを閉じた・ほかのページにした）、拾うのをやめる</para>
	void UpdateListening(InputActionMap& map);
	// 割り当てを拾っているか（その間、表の SetEnabled(false) にしておく）
	bool IsListening() const { return rebinder_.IsListening(); }
	// 拾うのをやめる（表の Action を足す・消すなど、番号が変わるとき）
	void CancelListening() { rebinder_.Cancel(); }

private:
	// Action 1つ（行ごとに、名前・割り当て・既定に戻す）
	void DrawAction(InputActionMap& map, InputActionId id);
	// 割り当ての1行（行の名前・その行の割り当て・＋）
	void DrawRow(InputActionMap& map, InputActionId id, InputBindingRow row);
	// 割り当て1つのボタン（押すと拾い始める。右の × で外す）。外したら true（後ろが詰まるので、その行の残りを描かない）
	bool DrawBinding(InputActionMap& map, InputActionId id, int index, InputBindingRow row);
	// ぶつかっている相手（行の下に出す）
	void DrawConflicts(InputActionMap& map, InputActionId id, int index);
	// 拾っている間の案内（モーダル。ほかを触れないようにして、押したクリックが別の所に効かないように）
	void DrawListening(const InputActionMap& map);

	InputRebinder rebinder_;
	std::string resetTooltip_; // 「既定」ボタンの説明
	int lastDrawFrame_ = -1;   // 最後に表を描いたフレーム（ImGui::GetFrameCount）
};
```

## G-2 `Editor/Widgets/InputActionTable.cpp`（全体を差し替え）

- 行の名前の幅は `SetCursorPosX(行の頭 + kRowLabelWidth)` でそろえる（`SameLine(x)` の x は表の列の頭から数えないので、最初はボタンが右へずれた）。

```cpp
#ifdef USE_IMGUI
#include "InputActionTable.h"

#include <format>
#include <string>
#include <vector>

#include <externals/imgui/imgui.h>

namespace {
constexpr float kLabelWidth = 200.0f;                               // Action の名前の列の幅
constexpr float kRowLabelWidth = 56.0f;                             // 行の名前（上・下・横の軸など）の幅
constexpr ImVec4 kConflictButtonColor = {0.62f, 0.20f, 0.20f, 1.0f}; // ぶつかっている割り当てのボタン
constexpr ImVec4 kConflictTextColor = {1.0f, 0.50f, 0.45f, 1.0f};    // ぶつかりの説明の文字
constexpr const char* kListeningPopupId = "割り当てを拾う";          // 拾っている間のモーダルの名前（ID にもなる）
constexpr float kSmallButtonScale = 0.75f;                          // × と ＋ の文字の大きさ（ふつうの文字の何倍か）
constexpr float kRemoveButtonSpacing = 4.0f;                        // 割り当てのボタンと × の間（ピクセル）

// 小さいボタン（× と ＋）。文字を小さくし、同じ行の割り当てのボタンと上下の真ん中をそろえる
bool SmallRowButton(const char* label) {
	const float rowHeight = ImGui::GetFrameHeight(); // 割り当てのボタン（ふつうの文字）の高さ
	ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * kSmallButtonScale);
	// 小さいボタンの高さ（小さい文字の高さ）との差の半分だけ下げる
	// SmallButton は、同じ行の文字の高さに合わせて自分でも FramePadding.y だけ下がるので、その分は引く
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f - ImGui::GetStyle().FramePadding.y);
	const bool isPressed = ImGui::SmallButton(label);
	ImGui::PopFont();
	return isPressed;
}

// 割り当ての一覧を "A / B" の文字にする（既定に戻すボタンの説明）
std::string JoinBindings(const std::vector<InputBinding>& bindings) {
	std::string text;
	for (const InputBinding& binding : bindings) {
		text += text.empty() ? "" : " / ";
		text += InputBindingText::ToDisplayString(binding);
	}
	return text.empty() ? "（なし）" : text;
}

// 行の名前（All は出さない）
const char* RowLabel(InputBindingRow row) {
	switch (row) {
	case InputBindingRow::All:
		return "";
	case InputBindingRow::Positive:
		return "＋";
	case InputBindingRow::Negative:
		return "−";
	case InputBindingRow::Axis:
		return "軸";
	case InputBindingRow::Up:
		return "上";
	case InputBindingRow::Down:
		return "下";
	case InputBindingRow::Left:
		return "左";
	case InputBindingRow::Right:
		return "右";
	case InputBindingRow::Horizontal:
		return "横の軸";
	case InputBindingRow::Vertical:
		return "縦の軸";
	}
	return "";
}

// 行の説明（行の名前の吹き出し）
const char* RowTooltip(InputBindingRow row) {
	switch (row) {
	case InputBindingRow::Positive:
	case InputBindingRow::Negative:
		return "押している間、この向きに 1 を足すキー・ボタン";
	case InputBindingRow::Axis:
		return "量をそのまま足す軸（トリガー・ホイール・スティック）。（反対）は、引いて足す";
	case InputBindingRow::Up:
	case InputBindingRow::Down:
	case InputBindingRow::Left:
	case InputBindingRow::Right:
		return "押している間、この向きに 1 を足すキー・ボタン";
	case InputBindingRow::Horizontal:
		return "横の量をそのまま足す軸（スティックの横・マウスの横の動き）。両側に効く（＋に倒すと右）。（反対）は左右が逆";
	case InputBindingRow::Vertical:
		return "縦の量をそのまま足す軸（スティックの縦・マウスの縦の動き）。両側に効く（＋に倒すと上）。（反対）は上下が逆";
	default:
		return "";
	}
}

// 行に何か出すか（入れられる入力があるか、もう割り当てが入っている）
bool HasRow(const InputActionMap& map, InputActionId id, InputBindingRow row) {
	if ((map.GetAcceptedDevices(id) & InputBindingRows::DevicesOf(row)) != InputDeviceMask::None) {
		return true;
	}
	const InputActionType type = map.GetDesc(id).type;
	for (const InputBinding& binding : map.GetBindings(id)) {
		if (InputBindingRows::Of(type, binding) == row) {
			return true;
		}
	}
	return false;
}
} // namespace


//=============================================================================
// 表
//=============================================================================
// ===== 全部 =====
void InputActionTable::Draw(InputActionMap& map) {
	lastDrawFrame_ = ImGui::GetFrameCount();

	// --- group ごとに見出しを付け、表を分ける（同じ group が続く間は、同じ表）---
	std::string currentGroup;
	bool isTableOpen = false;
	for (uint32_t i = 0; i < map.GetActionCount(); ++i) {
		const InputActionId id = {i};
		const InputActionDesc& desc = map.GetDesc(id);
		if (i == 0 || desc.group != currentGroup) {
			if (isTableOpen) {
				ImGui::EndTable();
			}
			currentGroup = desc.group;
			ImGui::SeparatorText(currentGroup.c_str());
			isTableOpen = ImGui::BeginTable(std::format("##group{}", i).c_str(), 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit);
			if (isTableOpen) {
				ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, kLabelWidth);
				ImGui::TableSetupColumn("bindings", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("reset", ImGuiTableColumnFlags_WidthFixed);
			}
		}
		if (isTableOpen) {
			ImGui::PushID(static_cast<int>(i));
			DrawAction(map, id);
			ImGui::PopID();
		}
	}
	if (isTableOpen) {
		ImGui::EndTable();
	}

	DrawListening(map);
}

// ===== 拾う係を動かす（フレームの頭）=====
void InputActionTable::UpdateListening(InputActionMap& map) {
	if (!rebinder_.IsListening()) {
		return;
	}
	// --- 前のフレームに表を描いていなければ（ページを閉じた・ほかのページにした）、やめる ---
	// フレームの頭（ImGui::NewFrame の前）では、GetFrameCount は前のフレームの番号。前のフレームに描いていれば同じ
	if (lastDrawFrame_ < ImGui::GetFrameCount()) {
		rebinder_.Cancel();
		return;
	}
	// --- 拾えたら、表に入れる ---
	const InputActionId id = rebinder_.GetAction();
	const int index = rebinder_.GetIndex();
	if (rebinder_.Update() == InputRebinder::Result::Captured) {
		map.SetBinding(id, index, *rebinder_.GetCaptured());
	}
}


//=============================================================================
// Action 1つ
//=============================================================================
void InputActionTable::DrawAction(InputActionMap& map, InputActionId id) {
	const InputActionDesc& desc = map.GetDesc(id);
	bool isFirstRow = true;
	for (const InputBindingRow row : InputBindingRows::Get(desc.type)) {
		if (!HasRow(map, id, row)) {
			continue; // 入れられる入力が無い行（ホイールだけの Value の ＋・− など）
		}
		ImGui::TableNextRow();
		ImGui::PushID(static_cast<int>(row));

		// --- 名前（1行目だけ）---
		ImGui::TableNextColumn();
		if (isFirstRow) {
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(desc.label.c_str());
			if (!desc.tooltip.empty()) {
				ImGui::SetItemTooltip("%s", desc.tooltip.c_str());
			}
		}

		// --- 割り当て ---
		ImGui::TableNextColumn();
		DrawRow(map, id, row);

		// --- 既定に戻す（1行目だけ）---
		ImGui::TableNextColumn();
		if (isFirstRow && !desc.isFixed) {
			ImGui::BeginDisabled(map.IsDefault(id));
			if (ImGui::Button("既定")) {
				map.ResetToDefault(id);
			}
			ImGui::EndDisabled();
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
				ImGui::SetTooltip("%s（%s）", resetTooltip_.c_str(), JoinBindings(desc.defaults).c_str());
			}
		}
		ImGui::PopID();
		isFirstRow = false;
	}
}

// ===== 割り当ての1行 =====
void InputActionTable::DrawRow(InputActionMap& map, InputActionId id, InputBindingRow row) {
	const InputActionDesc& desc = map.GetDesc(id);
	// --- 行の名前（Button は出さない）---
	if (row != InputBindingRow::All) {
		const float startX = ImGui::GetCursorPosX();
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("%s", RowLabel(row));
		ImGui::SetItemTooltip("%s", RowTooltip(row));
		ImGui::SameLine();
		ImGui::SetCursorPosX(startX + kRowLabelWidth); // 行の名前の幅をそろえる（割り当てのボタンが縦に並ぶように）
	}

	// --- この行の割り当て（ボタンを横に並べる）---
	bool hasAny = false;
	for (int index = 0; index < static_cast<int>(map.GetBindings(id).size()); ++index) {
		if (InputBindingRows::Of(desc.type, map.GetBindings(id)[index]) != row) {
			continue;
		}
		hasAny = true;
		if (DrawBinding(map, id, index, row)) {
			return; // 外したので、後ろが詰まった（次のフレームで描き直す）
		}
		ImGui::SameLine();
	}
	if (!hasAny) {
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("（なし）");
		ImGui::SameLine();
	}

	// --- 足す ---
	if (desc.isFixed) {
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("変えられません");
	} else {
		if (SmallRowButton("+")) {
			rebinder_.Start(map, id, static_cast<int>(map.GetBindings(id).size()), row);
		}
		ImGui::SetItemTooltip("%s", InputBindingRows::IsAxisRow(row) ? "軸を足す（押してから、スティックを ＋ の向き（右・上）へ倒す。反対へ倒すと、反対に足す）" : "割り当てを足す（押してから、足したいキー・ボタンを押す）");
	}

	// --- ぶつかり（この行の割り当てごとに、並びの下に1行ずつ）---
	for (int index = 0; index < static_cast<int>(map.GetBindings(id).size()); ++index) {
		if (InputBindingRows::Of(desc.type, map.GetBindings(id)[index]) == row) {
			DrawConflicts(map, id, index);
		}
	}
}

// ===== 割り当て1つのボタン =====
bool InputActionTable::DrawBinding(InputActionMap& map, InputActionId id, int index, InputBindingRow row) {
	const InputActionDesc& desc = map.GetDesc(id);
	const InputBinding binding = map.GetBindings(id)[index]; // 写しておく（外すと、元の物は消える）
	const bool isListeningHere = rebinder_.IsListening() && rebinder_.GetAction() == id && rebinder_.GetIndex() == index;
	const bool hasConflict = !map.FindConflicts(id, index).empty();

	ImGui::PushID(index);
	// --- ボタン（押すと拾い始める。ぶつかっていれば赤）---
	std::string text = isListeningHere ? std::string("押してください…") : InputBindingText::ToDisplayString(binding);
	if (text.empty()) {
		text = "?"; // 名前の無い入力（ふつうは無い）
	}
	// 軸の行で、反対に足す割り当て（-X・-Y）
	if (!isListeningHere && InputBindingRows::IsAxisRow(row) && InputBindingRows::DirectionOf(row) != binding.direction) {
		text += "（反対）";
	}
	if (hasConflict) {
		ImGui::PushStyleColor(ImGuiCol_Button, kConflictButtonColor);
	}
	ImGui::BeginDisabled(desc.isFixed);
	if (ImGui::Button(text.c_str())) {
		rebinder_.Start(map, id, index, row);
	}
	ImGui::EndDisabled();
	if (hasConflict) {
		ImGui::PopStyleColor();
	}
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
		ImGui::SetTooltip("%s", desc.isFixed ? "ほかの所でも、この入力を決め打ちで使っているので変えられません" : "押してから、新しいキー・ボタンを押すと入れ替わります");
	}

	// --- 外す ---
	bool isRemoved = false;
	if (!desc.isFixed) {
		ImGui::SameLine(0.0f, kRemoveButtonSpacing);
		if (SmallRowButton("×")) {
			map.RemoveBinding(id, index);
			isRemoved = true;
		}
		ImGui::SetItemTooltip("この割り当てを外す");
	}
	ImGui::PopID();
	return isRemoved;
}

// ===== ぶつかっている相手 =====
void InputActionTable::DrawConflicts(InputActionMap& map, InputActionId id, int index) {
	const std::vector<InputBindingRef> conflicts = map.FindConflicts(id, index);
	const std::string bindingText = InputBindingText::ToDisplayString(map.GetBindings(id)[index]);
	for (const InputBindingRef& other : conflicts) {
		const InputActionDesc& otherDesc = map.GetDesc(other.action);
		ImGui::PushID(index);
		ImGui::PushID(static_cast<int>(other.action.index));
		ImGui::PushID(other.index);
		ImGui::AlignTextToFramePadding();
		ImGui::TextColored(kConflictTextColor, "! %s は「%s」とぶつかっています", bindingText.c_str(), otherDesc.label.c_str());
		bool isRemoved = false;
		if (!otherDesc.isFixed) {
			ImGui::SameLine();
			if (ImGui::SmallButton(std::format("「{}」の方を外す", otherDesc.label).c_str())) {
				map.RemoveBinding(other.action, other.index);
				isRemoved = true;
			}
		}
		ImGui::PopID();
		ImGui::PopID();
		ImGui::PopID();
		if (isRemoved) {
			break; // 相手の並びが詰まったので、残りの一覧は古い（次のフレームで描き直す）
		}
	}
}


//=============================================================================
// 拾っている間
//=============================================================================
void InputActionTable::DrawListening(const InputActionMap& map) {
	// 拾う係はここでは動かさない（UpdateListening）。ImGui を描く間は、InputManager がウィンドウを手前でない扱いにしていて、何も読めない
	// --- 案内のモーダル（拾い終えたら閉じる）---
	if (rebinder_.IsListening() && !ImGui::IsPopupOpen(kListeningPopupId)) {
		ImGui::OpenPopup(kListeningPopupId);
	}
	if (ImGui::BeginPopupModal(kListeningPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		if (!rebinder_.IsListening()) {
			ImGui::CloseCurrentPopup();
		} else {
			const InputBindingRow row = rebinder_.GetRow();
			const std::string& label = map.GetDesc(rebinder_.GetAction()).label;
			if (row == InputBindingRow::All) {
				ImGui::Text("「%s」に割り当てるキー・ボタンを押してください", label.c_str());
			} else if (InputBindingRows::IsAxisRow(row)) {
				ImGui::Text("「%s」の「%s」に割り当てる軸を、＋ の向き（右・上）へ動かしてください", label.c_str(), RowLabel(row));
				ImGui::TextDisabled("反対の向きへ動かすと、反対に足す割り当てになります");
			} else {
				ImGui::Text("「%s」の「%s」に割り当てるキー・ボタンを押してください", label.c_str(), RowLabel(row));
			}
			ImGui::TextDisabled("Ctrl・Shift・Alt を押しながら押すと、組にして割り当てます（Ctrl+Z など）");
			ImGui::TextDisabled("Esc でやめる");
		}
		ImGui::EndPopup();
	}
}
#endif
```

EditorKeymap は何も変えなくてよい（Keymap の「前後に動く（ホイール）」は、軸の行だけが出る）。

# パートH GameInputPage（新しい）

## H-1 `Editor/Keymap/GameInputPage.h`（新しい）

```cpp
#pragma once

/// <summary>
/// Settings → Project → Input のページ（ゲームの Action と既定の割り当てを直す。保存は Parameters/Input/InputActions.json）
/// <para>Action を足す・消す・並べ替える、名前・種類（Button・Value・Vector2）などを決める所と、割り当ての表（InputActionTable）</para>
/// <para>ゲームの表（GameInput）を直接は直さず、自分の表（InputGate::Editor で読む）を持つ。ゲームの表は Game の決まりで読むので、ImGui を触っている間（＝このページを触っている間）は何も拾えないため</para>
/// <para>Save でファイルに書き、GameInput に読み直させる（Play 中でも、すぐゲームに効く）</para>
/// </summary>
class GameInputPage {
public:
	// ファイルを読み、Settings にページを足す（ImGuiManager::Initialize から1回）
	static void Initialize();
	// 割り当てを拾う係を動かす（毎フレーム1回。WindowManager::UpdateAll の、ImGui のウィンドウの番。入力を読めるのはここだけ）
	static void Update();

private:
	// ページの中身
	static void Draw();
};
```

## H-2 `Editor/Keymap/GameInputPage.cpp`（新しい）

```cpp
#ifdef USE_IMGUI
#include "GameInputPage.h"

#include <filesystem>
#include <format>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <externals/imgui/imgui.h>
#include <externals/magic_enum/magic_enum.hpp>

#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Editor/Widgets/EditorWidgets.h"
#include "HTTEngine/Editor/Widgets/InputActionTable.h"
#include "HTTEngine/Editor/Windows/SettingsWindow.h"
#include "HTTEngine/Input/Action/GameInput.h"
#include "HTTEngine/Input/Action/InputActionFile.h"
#include "HTTEngine/String/ConvertString.h"
#include "HTTEngine/UI/GlobalVariables.h"

namespace {
constexpr const char* kPageName = "Input";              // Settings の一覧の名前
constexpr const char* kNewActionName = "NewAction";      // 足した Action の名前（重なれば後ろに番号）
constexpr float kListHeight = 180.0f;                    // Action の一覧の高さ（ピクセル。多ければスクロール）
constexpr ImVec4 kErrorColor = {1.0f, 0.50f, 0.45f, 1.0f}; // 保存できない理由の文字

//=============================================================================
// 持ち物
//=============================================================================
struct PageState {
	std::vector<InputActionDesc> saved;  // ファイルの中身（Revert で戻る先。表の「既定」もこれ）
	std::vector<InputActionDesc> edited; // 直している中身（defaults が、今の割り当て）
	bool hasFile = false;                // ファイルがあるか（無ければ Save で作る）
	// 表に見せる表（edited から作る。Action の番号＝ edited の番号）
	// エディタの決まりで読む（このページを触っている間も拾える）。修飾キーの合わせ方はゲームと同じ（ぶつかりの見え方をゲームとそろえる）
	InputActionMap map{InputGate::Editor, InputModifierMatch::AtLeast};
	InputActionTable table{"保存した割り当てに戻す"};
	int selected = -1; // 詳しい所を出している Action（edited の番号。無ければ -1）
};
PageState& State() {
	static PageState state;
	return state;
}

//=============================================================================
// 表と、直している中身の行き来
//=============================================================================
// ===== 保存した中身から、同じ名前の Action を探す（無ければ nullptr）=====
const InputActionDesc* FindSaved(const std::string& name) {
	for (const InputActionDesc& desc : State().saved) {
		if (desc.name == name) {
			return &desc;
		}
	}
	return nullptr;
}

// ===== 直している中身から、表を作り直す（Action を足す・消す・名前や種類を変えたとき）=====
// 表の「既定」は保存した割り当て（「既定」のボタン＝保存した割り当てに戻す）。今の割り当ては SetBindings で入れる
void RebuildMap() {
	PageState& state = State();
	state.table.CancelListening(); // 表の Action の番号が変わるかもしれないので
	state.map = InputActionMap(InputGate::Editor, InputModifierMatch::AtLeast);
	for (size_t i = 0; i < state.edited.size(); ++i) {
		const InputActionDesc& desc = state.edited[i];
		InputActionDesc view = desc;
		// 表の中の名前は番号にする（直している途中の名前は、空・重なりがありうる。表の名前は1つだけでないといけない）
		view.name = std::to_string(i);
		view.label = desc.label.empty() ? desc.name : desc.label;
		view.isFixed = false; // ページでは「変えられない」Action の既定も直す（変えられないのはプレイヤー）
		const InputActionDesc* saved = FindSaved(desc.name);
		view.defaults = saved ? saved->defaults : std::vector<InputBinding>{};
		const InputActionId id = state.map.AddAction(std::move(view));
		state.map.SetBindings(id, desc.defaults);
	}
}

// ===== 表で直した割り当てを、直している中身へ写す =====
void SyncFromMap() {
	PageState& state = State();
	for (size_t i = 0; i < state.edited.size() && i < state.map.GetActionCount(); ++i) {
		state.edited[i].defaults = state.map.GetBindings({static_cast<uint32_t>(i)});
	}
}

// ===== 保存できない理由（名前が空・重なっている。無ければ空）=====
std::string FindNameError() {
	std::unordered_set<std::string> names;
	for (const InputActionDesc& desc : State().edited) {
		if (desc.name.empty()) {
			return "名前が空の Action があります";
		}
		if (!names.insert(desc.name).second) {
			return std::format("同じ名前の Action が2つあります: {}", desc.name);
		}
	}
	return {};
}

//=============================================================================
// 読む・保存・戻す
//=============================================================================
// ===== ファイルを読む =====
void LoadFile() {
	PageState& state = State();
	const std::optional<std::vector<InputActionDesc>> loaded = InputActionFile::Load(GameInput::kActionsPath);
	state.hasFile = loaded.has_value();
	state.saved = loaded.value_or(std::vector<InputActionDesc>{});
	state.edited = state.saved;
	state.selected = state.edited.empty() ? -1 : 0;
	RebuildMap();
}

// ===== 保存（名前に問題があれば保存しない）=====
void Save() {
	PageState& state = State();
	SyncFromMap();
	const std::string error = FindNameError();
	if (!error.empty()) {
		LogManager::Error("[GameInputPage] 保存しませんでした: " + error);
		return;
	}
	if (!InputActionFile::Save(GameInput::kActionsPath, state.edited)) {
		return;
	}
	state.saved = state.edited;
	state.hasFile = true;
	RebuildMap();               // 表の「既定」（保存した割り当て）を新しくする
	GameInput::ReloadActions(); // ゲームの表へ（Play 中でも効く）
}

// ===== 保存した状態に戻す =====
void Revert() {
	PageState& state = State();
	state.edited = state.saved;
	if (state.selected >= static_cast<int>(state.edited.size())) {
		state.selected = static_cast<int>(state.edited.size()) - 1;
	}
	RebuildMap();
}

// ===== 保存していない変更があるか =====
bool IsDirty() {
	SyncFromMap();
	return State().edited != State().saved;
}

//=============================================================================
// Action を足す・消す・並べ替える
//=============================================================================
// ===== 重ならない名前（"NewAction"・"NewAction2"…）=====
std::string MakeUniqueName() {
	std::unordered_set<std::string> names;
	for (const InputActionDesc& desc : State().edited) {
		names.insert(desc.name);
	}
	std::string name = kNewActionName;
	for (int n = 2; names.contains(name); ++n) {
		name = std::format("{}{}", kNewActionName, n);
	}
	return name;
}

// ===== 足す（選んでいる Action の後ろ。同じ group にする）=====
void AddAction() {
	PageState& state = State();
	InputActionDesc desc;
	desc.name = MakeUniqueName();
	desc.label = desc.name;
	const int insertAt = (state.selected >= 0) ? state.selected + 1 : static_cast<int>(state.edited.size());
	if (state.selected >= 0) {
		desc.group = state.edited[state.selected].group;
	}
	state.edited.insert(state.edited.begin() + insertAt, std::move(desc));
	state.selected = insertAt;
	RebuildMap();
}

// ===== 消す =====
void RemoveAction(int index) {
	PageState& state = State();
	state.edited.erase(state.edited.begin() + index);
	if (state.selected >= static_cast<int>(state.edited.size())) {
		state.selected = static_cast<int>(state.edited.size()) - 1;
	}
	RebuildMap();
}

// ===== 1つ前・後ろと入れ替える（offset は -1 か 1）=====
void MoveAction(int index, int offset) {
	PageState& state = State();
	const int other = index + offset;
	if (other < 0 || other >= static_cast<int>(state.edited.size())) {
		return;
	}
	std::swap(state.edited[index], state.edited[other]);
	state.selected = other;
	RebuildMap();
}

//=============================================================================
// 描く
//=============================================================================
// ===== Action の一覧（押すと選ぶ）=====
void DrawActionList() {
	PageState& state = State();
	ImGui::SeparatorText("Action");
	if (ImGui::BeginTable("##actions", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY, ImVec2(0.0f, kListHeight))) {
		ImGui::TableSetupScrollFreeze(0, 1); // 見出しの行はスクロールしない
		ImGui::TableSetupColumn("名前");
		ImGui::TableSetupColumn("種類");
		ImGui::TableSetupColumn("グループ");
		ImGui::TableHeadersRow();
		for (int i = 0; i < static_cast<int>(state.edited.size()); ++i) {
			const InputActionDesc& desc = state.edited[i];
			ImGui::PushID(i);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			const std::string name = desc.name.empty() ? std::string("（名前なし）") : desc.name;
			if (ImGui::Selectable(name.c_str(), state.selected == i, ImGuiSelectableFlags_SpanAllColumns)) {
				state.selected = i;
			}
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(magic_enum::enum_name(desc.type).data());
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(desc.group.c_str());
			ImGui::PopID();
		}
		ImGui::EndTable();
	}
	if (ImGui::Button("Action を足す")) {
		AddAction();
	}
	ImGui::SetItemTooltip("選んでいる Action の後ろに足す（同じグループになる）");
}

// ===== 選んだ Action の詳しい所（変えたら true＝表を作り直す）=====
bool DrawActionDetail(InputActionDesc& desc) {
	bool isChanged = false;
	ImGui::SeparatorText("選んだ Action");
	// --- 名前・見せ方 ---
	isChanged |= EditorWidgets::InputText("名前", desc.name);
	ImGui::SetItemTooltip("Script から読む名前（GameInput::IsTriggered(\"Jump\")）。変えると、Script と、プレイヤーが保存した割り当て（上書き）が当たらなくなる");
	isChanged |= EditorWidgets::InputText("表示名", desc.label);
	ImGui::SetItemTooltip("画面に出す名前（キーコンフィグの画面・この表）。空なら名前");
	isChanged |= EditorWidgets::InputText("グループ", desc.group);
	ImGui::SetItemTooltip("画面の見出し（続けて並んだ同じグループを、まとめて出す）");
	isChanged |= EditorWidgets::InputText("効く場所", desc.context);
	ImGui::SetItemTooltip("ぶつかりを調べる範囲（\"Gameplay\"・\"Menu\" など）。同じ文字どうしと、空（どこでも効く）との間だけ調べる");
	isChanged |= EditorWidgets::InputText("説明", desc.tooltip);
	ImGui::SetItemTooltip("表の名前にマウスを乗せると出る説明");

	// --- 種類 ---
	if (ImGui::BeginCombo("種類", magic_enum::enum_name(desc.type).data())) {
		for (const InputActionType type : magic_enum::enum_values<InputActionType>()) {
			if (ImGui::Selectable(magic_enum::enum_name(type).data(), desc.type == type)) {
				desc.type = type;
				isChanged = true;
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("Button：押す・離す（ジャンプ）　Value：1つの量（トリガー・ホイール）　Vector2：2つの量（移動）");
	if (desc.type == InputActionType::Vector2) {
		isChanged |= ImGui::Checkbox("長さを 1 までにする", &desc.clampLength);
		ImGui::SetItemTooltip("キーの斜め・キーとスティックを一緒に倒したときに、速くならないように（移動に。マウスの動きには付けない）");
	}
	isChanged |= ImGui::Checkbox("プレイヤーが変えられない", &desc.isFixed);
	ImGui::SetItemTooltip("キーコンフィグの画面で変えられない（ここでは直せる）。ほかの操作とのぶつかりを見せるために出す物に");

	// --- 割り当てられる入力（全部外すと、種類で決める）---
	ImGui::TextUnformatted("割り当てられる入力");
	ImGui::SetItemTooltip("1つも選ばなければ、種類で決める（Button はキー・ボタンだけ、ほかは全部）。マウスの動きとスティックは単位が違うので、1つの Action に混ぜない");
	for (const InputDevice device : magic_enum::enum_values<InputDevice>()) {
		if (device == InputDevice::None) {
			continue;
		}
		const InputDeviceMask bit = static_cast<InputDeviceMask>(1 << static_cast<uint8_t>(device));
		bool isOn = Contains(desc.accepts, device);
		ImGui::SameLine();
		if (ImGui::Checkbox(magic_enum::enum_name(device).data(), &isOn)) {
			desc.accepts = isOn ? (desc.accepts | bit) : static_cast<InputDeviceMask>(static_cast<uint8_t>(desc.accepts) & ~static_cast<uint8_t>(bit));
			isChanged = true;
		}
	}
	return isChanged;
}

// ===== 選んだ Action の並べ替え・消す =====
void DrawActionButtons(int index) {
	if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
		MoveAction(index, -1);
	}
	ImGui::SetItemTooltip("1つ前へ（画面に出す順）");
	ImGui::SameLine();
	if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
		MoveAction(index, 1);
	}
	ImGui::SetItemTooltip("1つ後ろへ");
	ImGui::SameLine();
	if (ImGui::Button("消す")) {
		RemoveAction(index);
	}
	ImGui::SetItemTooltip("この Action を消す（Save するまでファイルは変わらない。Revert で戻せる）");
}

// ===== プレイヤーの上書き（エディタで遊んで保存した割り当て）=====
void DrawPlayerOverrides() {
	ImGui::SeparatorText("プレイヤーの上書き");
	std::vector<std::filesystem::path> files; // ある物だけ
	for (int player = 0; player < GameInput::kMaxPlayers; ++player) {
		std::error_code ec;
		const std::filesystem::path path = GameInput::GetBindingsPath(player);
		if (std::filesystem::exists(path, ec)) {
			files.push_back(path);
		}
	}
	ImGui::TextDisabled("保存先: %s", ToUtf8(GameInput::GetBindingsPath(0).parent_path()).c_str());
	if (files.empty()) {
		ImGui::TextDisabled("ありません（Play 中にゲームが保存すると、ここの割り当てが既定より先に効く）");
		return;
	}
	ImGui::Text("%zu 人分あります。Play 中は、既定より先にこちらが効きます", files.size());
	if (ImGui::Button("プレイヤーの上書きを消す")) {
		for (const std::filesystem::path& path : files) {
			std::error_code ec;
			std::filesystem::remove(path, ec);
		}
		GameInput::ReloadActions(); // ゲームの表を既定に戻す
		LogManager::Log("[GameInputPage] プレイヤーの上書きを消しました");
	}
	ImGui::SetItemTooltip("エディタで遊んで保存した割り当てを消し、既定から始まるようにする（UserData/ は git に入れない、自分の PC の物だけ）");
}
} // namespace


//=============================================================================
// 初期化・毎フレーム
//=============================================================================
// ===== 初期化 =====
void GameInputPage::Initialize() {
	LoadFile();
	SettingsWindow::AddPage({
	    .name = kPageName,
	    .section = SettingsSection::Project,
	    .draw = &GameInputPage::Draw,
	    .isDirty = &IsDirty,
	    .revert = &Revert,
	    .save = &Save,
	    .saveLocation = GameInput::kActionsPath,
	    .revertTooltip = "保存した Action と割り当てに戻します",
	});
}

// ===== 毎フレーム =====
void GameInputPage::Update() {
	PageState& state = State();
	// 割り当てを拾う（入力を読めるのはここだけ。ページを閉じていたら、拾うのをやめる）
	state.table.UpdateListening(state.map);
}


//=============================================================================
// ページ
//=============================================================================
void GameInputPage::Draw() {
	PageState& state = State();
	SyncFromMap();

	// --- ファイルの様子・保存できない理由 ---
	if (!state.hasFile) {
		ImGui::TextDisabled("ファイルがまだありません。Action を足して Save すると作ります");
	}
	const std::string error = FindNameError();
	if (!error.empty()) {
		ImGui::TextColored(kErrorColor, "保存できません: %s", error.c_str());
	}
	ImGui::TextDisabled("Script から GameInput::IsTriggered(\"名前\") で読む。Save すると、Play 中でもすぐゲームに効く");

	// --- Action の一覧と、選んだ Action ---
	DrawActionList();
	if (state.selected >= 0 && state.selected < static_cast<int>(state.edited.size())) {
		ImGui::PushID(state.selected);
		const int index = state.selected;
		if (DrawActionDetail(state.edited[index])) {
			RebuildMap();
		}
		DrawActionButtons(index);
		ImGui::PopID();
	}

	// --- 割り当て ---
	ImGui::SeparatorText("割り当て");
	state.table.Draw(state.map);
	SyncFromMap(); // 表で外した・既定に戻した物を写す

	DrawPlayerOverrides();
}
#endif
```

## H-3 `Editor/ImGuiManager.cpp`

```cpp
#include "HTTEngine/Editor/Keymap/EditorKeymap.h"
#include "HTTEngine/Editor/Keymap/GameInputPage.h" // ← 足す
```

```cpp
	EditorKeymap::Initialize();
	// ゲームの操作の割り当て（Parameters/Input/InputActions.json を読み、Settings → Project に Input のページを足す）
	GameInputPage::Initialize();
```

# パートI vcxproj

`HTTEngine.vcxproj` の ClCompile に足す。

```xml
    <ClCompile Include="HTTEngine\Editor\Keymap\GameInputPage.cpp" />
    <ClCompile Include="HTTEngine\Input\Action\GameInput.cpp" />
    <ClCompile Include="HTTEngine\Input\Action\InputActionFile.cpp" />
```

ClInclude に足す。

```xml
    <ClInclude Include="HTTEngine\Core\UserDataPath.h" />
    <ClInclude Include="HTTEngine\Editor\Keymap\GameInputPage.h" />
    <ClInclude Include="HTTEngine\Input\Action\GameInput.h" />
    <ClInclude Include="HTTEngine\Input\Action\InputActionFile.h" />
```

`HTTEngine.vcxproj.filters` に足す（フォルダの Filter はもうある）。

```xml
    <ClCompile Include="HTTEngine\Editor\Keymap\GameInputPage.cpp">
      <Filter>HTTEngine\Editor\Keymap</Filter>
    </ClCompile>
    <ClCompile Include="HTTEngine\Input\Action\GameInput.cpp">
      <Filter>HTTEngine\Input\Action</Filter>
    </ClCompile>
    <ClCompile Include="HTTEngine\Input\Action\InputActionFile.cpp">
      <Filter>HTTEngine\Input\Action</Filter>
    </ClCompile>
```

```xml
    <ClInclude Include="HTTEngine\Core\UserDataPath.h">
      <Filter>HTTEngine\Core</Filter>
    </ClInclude>
    <ClInclude Include="HTTEngine\Editor\Keymap\GameInputPage.h">
      <Filter>HTTEngine\Editor\Keymap</Filter>
    </ClInclude>
    <ClInclude Include="HTTEngine\Input\Action\GameInput.h">
      <Filter>HTTEngine\Input\Action</Filter>
    </ClInclude>
    <ClInclude Include="HTTEngine\Input\Action\InputActionFile.h">
      <Filter>HTTEngine\Input\Action</Filter>
    </ClInclude>
```

# パートJ Sandbox

## J-1 `.gitignore`（エンジンのリポジトリの一番上）

```
/project/Sandbox/Parameters/Editor/
# プレイヤーのデータ（キーコンフィグで変えた割り当てなど。エディタ・製品版で遊んだ人ごと。UserDataPath.h）
/project/Sandbox/UserData/
```

AL4_01・TD2_2 などゲームのリポジトリにも、`Parameters/` と同じ所の `UserData/` を足す（`/project/UserData/` など）。

## J-2 `project/Sandbox/Parameters/Input/InputActions.json`（新しい）

Menu の Action は「プレイヤーが変えられない」（`fixed`）にして、キーコンフィグの画面に出さない。Menu.Up の ↑ と Move の ↑ は、効く場所（`context`）が違うのでぶつからない。

```json
{
    "version": 1,
    "actions": [
        {
            "name": "Move",
            "label": "移動",
            "group": "動き",
            "context": "Gameplay",
            "tooltip": "キャラを歩かせる（W・スティックの上＝画面の奥）",
            "type": "Vector2",
            "clampLength": true,
            "bindings": ["Key.W:+Y", "Key.Up:+Y", "Key.S:-Y", "Key.Down:-Y", "Key.A:-X", "Key.Left:-X", "Key.D", "Key.Right", "Pad.LeftStickX", "Pad.LeftStickY:+Y"]
        },
        {
            "name": "Jump",
            "label": "ジャンプ",
            "group": "動き",
            "context": "Gameplay",
            "bindings": ["Key.Space", "Pad.A"]
        },
        {
            "name": "Menu.Open",
            "label": "キーコンフィグを開く・閉じる",
            "group": "メニュー",
            "context": "Menu",
            "fixed": true,
            "bindings": ["Key.Tab", "Pad.Start"]
        },
        {
            "name": "Menu.Up",
            "label": "上の項目へ",
            "group": "メニュー",
            "context": "Menu",
            "fixed": true,
            "bindings": ["Key.Up", "Pad.DpadUp"]
        },
        {
            "name": "Menu.Down",
            "label": "下の項目へ",
            "group": "メニュー",
            "context": "Menu",
            "fixed": true,
            "bindings": ["Key.Down", "Pad.DpadDown"]
        },
        {
            "name": "Menu.Submit",
            "label": "決定",
            "group": "メニュー",
            "context": "Menu",
            "fixed": true,
            "bindings": ["Key.Enter", "Pad.A"]
        }
    ]
}
```

## J-3 `project/Sandbox/Source/Scripts/Player/PlayerController.cpp`（GameInput にする）

`#include <HTTEngine/Input/InputManager.h>` を消す（GameInput は Script.h に入っている）。

```cpp
	// --- 歩く：Action の Move（WASD・矢印キー・左スティック。-1〜1）を、カメラから見た地面の向きへ直す ---
	// velocity の X・Z は毎フレーム書く（離すと 0 になって止まる）
	// 割り当ては Parameters/Input/InputActions.json（Settings → Project → Input）
	const Vector3 move = moveInput_.ToWorld(GameInput::GetVector2("Move"));
```

```cpp
	// --- 跳ぶ：地面にいて、押した瞬間だけ ---
	// velocity の Y は跳ぶときだけ書く（あとは重力に任せる）
	if (controller->IsGrounded() && GameInput::IsTriggered("Jump")) {
		controller->velocity.y = jumpSpeed_;
	}
```

## J-4 `project/Sandbox/Source/Scripts/Input/KeyConfigTest.h`（新しい）

キーコンフィグの画面の見本（ゲームの UI で作る）。Text を自分で作って出す。Tab（Start）で開く・閉じる、↑↓で選ぶ、Enter（A）で決める。開いている間は `Time::SetGameTimeScale(0)` でプレイヤーを止める。

```cpp
#pragma once
#include <string>
#include <vector>

#include <HTTEngine/ECS/Script.h>

//=============================================================================
// キーコンフィグの画面の確かめ（GameInput の StartRebind・SaveBindings を、ゲームの UI で使う例）
//=============================================================================
// Menu.Open（Tab・Start）で開く・閉じる。Menu.Up・Menu.Down で選び、Menu.Submit（Enter・A）で決める
// Action の行で決めると、次に押したキー・ボタンをその行の1つ目の割り当てと入れ替える（Esc でやめる）
// 開いている間はゲームの時間を止める（プレイヤーが動かない）。文字は Text を自分で作って出す


class KeyConfigTest : public Script {
public:
	void Start() override;
	void Update() override;
	void OnDestroy() override;

private:
	// 選べる項目1つ
	struct Item {
		enum class Kind {
			Binding, // Action の1行（決めると拾う）
			Reset,   // 全部を既定に戻す
			Revert,  // 保存した割り当てに戻す
			Save,    // 保存する
		};
		Kind kind = Kind::Binding;
		InputActionId action; // Binding のとき
		InputBindingRow row = InputBindingRow::All;
	};

	// 項目の一覧を作り直す（Action の並びから）
	void BuildItems();
	// 開く・閉じる
	void SetOpen(bool isOpen);
	// 選んでいる項目を決めた
	void Submit();
	// 文字を書き直す
	void Refresh();
	// 項目1つの文字
	std::string MakeItemText(const Item& item) const;
	// その行の割り当ての番号（無ければ -1）
	int FindFirstBinding(InputActionId action, InputBindingRow row) const;

	// ===== 実行中のみ使用 =====
	std::vector<Item> items_;
	std::vector<GameObject> rowTexts_; // 項目ごとの Text（自分で作る）
	GameObject titleText_;
	GameObject statusText_;
	int cursor_ = 0;
	bool isOpen_ = false;
	std::string status_; // 一番下に出す知らせ
};
```

## J-5 `project/Sandbox/Source/Scripts/Input/KeyConfigTest.cpp`（新しい）

```cpp
#include "KeyConfigTest.h"

#include <format>

#include <HTTEngine/Components/UI/TextComponent.h>

SCRIPT(KeyConfigTest, "Input"); // クラス名が保存の名前なので後から変えない

namespace {
constexpr float kLeft = 40.0f;          // 文字の左端（画面の左からのピクセル）
constexpr float kTop = 40.0f;           // 一番上の文字（画面の上からのピクセル）
constexpr float kLineHeight = 30.0f;    // 1行の高さ（ピクセル）
constexpr float kFontSize = 22.0f;      // 文字の大きさ
constexpr Vector4 kNormalColor = {1.0f, 1.0f, 1.0f, 1.0f};
constexpr Vector4 kSelectedColor = {1.0f, 0.85f, 0.3f, 1.0f};

// 行の名前（ゲームの画面に出す文字なので、ゲームが決める）
const char* RowLabel(InputBindingRow row) {
	switch (row) {
	case InputBindingRow::Positive:
		return "＋";
	case InputBindingRow::Negative:
		return "−";
	case InputBindingRow::Axis:
		return "軸";
	case InputBindingRow::Up:
		return "上";
	case InputBindingRow::Down:
		return "下";
	case InputBindingRow::Left:
		return "左";
	case InputBindingRow::Right:
		return "右";
	case InputBindingRow::Horizontal:
		return "横の軸";
	case InputBindingRow::Vertical:
		return "縦の軸";
	default:
		return "";
	}
}

// 画面の左上から数えた位置に置く Text を作る（Component が付くのは次のフレームの頭）
GameObject CreateText(const std::string& name, GameObject parent, int line) {
	GameObject object = GameObject::Create(name, parent);
	object.transform.translation = {kLeft, -(kTop + kLineHeight * static_cast<float>(line)), 0.0f};
	TextComponent text;
	text.enabled = false;
	text.text = "";
	text.fontSize = kFontSize;
	text.align = TextAlign::Left;
	text.anchor = ScreenAnchor::TopLeft;
	text.pivot = {0.0f, 1.0f}; // 左上を位置に合わせる
	text.outlineWidth = 2.0f;
	object.AddComponent(text);
	return object;
}

// Text の文字・色・出すか（まだ Component が付いていなければ何もしない）
void SetText(GameObject object, bool isVisible, const std::string& value, const Vector4& color = kNormalColor) {
	TextComponent* text = object ? object.GetComponent<TextComponent>() : nullptr;
	if (text == nullptr) {
		return;
	}
	text->enabled = isVisible;
	text->text = value;
	text->color = color;
}
} // namespace


//=============================================================================
// 初期化・更新・終わり
//=============================================================================
// ===== 初期化 =====
void KeyConfigTest::Start() {
	BuildItems();
	titleText_ = CreateText("KeyConfigTitle", gameObject, 0);
	for (size_t i = 0; i < items_.size(); ++i) {
		rowTexts_.push_back(CreateText(std::format("KeyConfigRow{}", i), gameObject, static_cast<int>(i) + 1));
	}
	statusText_ = CreateText("KeyConfigStatus", gameObject, static_cast<int>(items_.size()) + 2);
}

// ===== 更新 =====
void KeyConfigTest::Update() {
	// --- 拾う係の結果（GameInput が、このフレームの Script より前に動かしている）---
	switch (GameInput::GetRebindResult()) {
	case InputRebinder::Result::Captured: {
		const InputActionMap& map = GameInput::GetMap();
		const std::vector<InputBindingRef> conflicts = map.FindConflicts(GameInput::GetRebindAction(), GameInput::GetRebindIndex());
		status_ = conflicts.empty() ? "入れ替えました" : std::format("入れ替えました（「{}」とぶつかっています）", map.GetDesc(conflicts.front().action).label);
		break;
	}
	case InputRebinder::Result::Canceled:
		status_ = "やめました";
		break;
	case InputRebinder::Result::Listening:
	case InputRebinder::Result::Idle:
		break;
	}

	// --- 開く・閉じる（拾っている間は、表が止まっているので効かない）---
	if (GameInput::IsTriggered("Menu.Open")) {
		SetOpen(!isOpen_);
	}

	// --- 選ぶ・決める ---
	if (isOpen_ && !GameInput::IsRebinding() && !items_.empty()) {
		const int count = static_cast<int>(items_.size());
		if (GameInput::IsTriggered("Menu.Up")) {
			cursor_ = (cursor_ + count - 1) % count;
		}
		if (GameInput::IsTriggered("Menu.Down")) {
			cursor_ = (cursor_ + 1) % count;
		}
		if (GameInput::IsTriggered("Menu.Submit")) {
			Submit();
		}
	}
	Refresh();
}

// ===== 終わり =====
void KeyConfigTest::OnDestroy() {
	if (isOpen_) {
		Time::SetGameTimeScale(1.0f); // 止めた時間を戻す
	}
	GameInput::CancelRebind();
}


//=============================================================================
// 項目
//=============================================================================
// ===== 一覧を作る（変えられない Action は出さない）=====
void KeyConfigTest::BuildItems() {
	items_.clear();
	const InputActionMap& map = GameInput::GetMap();
	for (uint32_t i = 0; i < map.GetActionCount(); ++i) {
		const InputActionId id = {i};
		const InputActionDesc& desc = map.GetDesc(id);
		if (desc.isFixed) {
			continue;
		}
		for (const InputBindingRow row : InputBindingRows::Get(desc.type)) {
			if ((map.GetAcceptedDevices(id) & InputBindingRows::DevicesOf(row)) == InputDeviceMask::None) {
				continue; // この行に入れられる入力が無い
			}
			items_.push_back({Item::Kind::Binding, id, row});
		}
	}
	items_.push_back({Item::Kind::Reset});
	items_.push_back({Item::Kind::Revert});
	items_.push_back({Item::Kind::Save});
}

// ===== 開く・閉じる =====
void KeyConfigTest::SetOpen(bool isOpen) {
	isOpen_ = isOpen;
	Time::SetGameTimeScale(isOpen ? 0.0f : 1.0f); // 開いている間はプレイヤーを動かさない
	status_.clear();
	if (!isOpen) {
		GameInput::CancelRebind();
	}
}

// ===== 決める =====
void KeyConfigTest::Submit() {
	const Item& item = items_[cursor_];
	switch (item.kind) {
	case Item::Kind::Binding: {
		// その行の1つ目の割り当てを入れ替える（無ければ足す）
		const std::string name = GameInput::GetMap().GetDesc(item.action).name;
		GameInput::StartRebind(name, item.row, FindFirstBinding(item.action, item.row));
		status_ = "割り当てるキー・ボタンを押してください（Esc でやめる）";
		break;
	}
	case Item::Kind::Reset:
		GameInput::GetMap().ResetAllToDefault();
		status_ = "既定に戻しました（残すなら保存）";
		break;
	case Item::Kind::Revert:
		GameInput::RevertBindings();
		status_ = "保存した割り当てに戻しました";
		break;
	case Item::Kind::Save:
		status_ = GameInput::SaveBindings() ? "保存しました" : "保存できませんでした（ログを見る）";
		break;
	}
}

// ===== その行の1つ目の割り当て =====
int KeyConfigTest::FindFirstBinding(InputActionId action, InputBindingRow row) const {
	const InputActionMap& map = GameInput::GetMap();
	const std::vector<InputBinding>& bindings = map.GetBindings(action);
	for (int i = 0; i < static_cast<int>(bindings.size()); ++i) {
		if (InputBindingRows::Of(map.GetDesc(action).type, bindings[i]) == row) {
			return i;
		}
	}
	return -1;
}


//=============================================================================
// 文字
//=============================================================================
// ===== 全部書き直す =====
void KeyConfigTest::Refresh() {
	SetText(titleText_, isOpen_, std::format("キーコンフィグ{}（Tab で閉じる）", GameInput::IsBindingsDirty() ? " *" : ""));
	for (size_t i = 0; i < rowTexts_.size() && i < items_.size(); ++i) {
		const bool isSelected = (static_cast<int>(i) == cursor_);
		SetText(rowTexts_[i], isOpen_, std::format("{} {}", isSelected ? ">" : " ", MakeItemText(items_[i])), isSelected ? kSelectedColor : kNormalColor);
	}
	// 閉じている間は、開き方とジャンプのキーだけ出す（GetDisplayText の確かめ）
	const std::string closedText = std::format("{} でキーコンフィグ / {} でジャンプ", GameInput::GetDisplayText("Menu.Open"), GameInput::GetDisplayText("Jump"));
	SetText(statusText_, true, isOpen_ ? status_ : closedText);
}

// ===== 項目1つの文字 =====
std::string KeyConfigTest::MakeItemText(const Item& item) const {
	switch (item.kind) {
	case Item::Kind::Reset:
		return "全部を既定に戻す";
	case Item::Kind::Revert:
		return "保存した割り当てに戻す";
	case Item::Kind::Save:
		return "保存する";
	case Item::Kind::Binding:
		break;
	}
	const InputActionMap& map = GameInput::GetMap();
	const InputActionDesc& desc = map.GetDesc(item.action);
	std::string text = desc.label;
	if (item.row != InputBindingRow::All) {
		text += std::format("（{}）", RowLabel(item.row));
	}
	text += ": ";
	// 拾っている行
	if (GameInput::IsRebinding() && GameInput::GetRebindAction() == item.action && items_[cursor_].row == item.row && &items_[cursor_] == &item) {
		return text + "押してください…";
	}
	// その行の割り当てと、ぶつかり
	const std::vector<InputBinding>& bindings = map.GetBindings(item.action);
	bool isFirst = true;
	std::string conflict;
	for (int i = 0; i < static_cast<int>(bindings.size()); ++i) {
		if (InputBindingRows::Of(desc.type, bindings[i]) != item.row) {
			continue;
		}
		text += std::format("{}{}", isFirst ? "" : " / ", InputBindingText::ToDisplayString(bindings[i]));
		isFirst = false;
		const std::vector<InputBindingRef> conflicts = map.FindConflicts(item.action, i);
		if (!conflicts.empty() && conflict.empty()) {
			conflict = std::format("  ！「{}」とぶつかる", map.GetDesc(conflicts.front().action).label);
		}
	}
	if (isFirst) {
		text += "（なし）";
	}
	return text + conflict;
}
```

## J-6 テストシーン `project/Sandbox/Resources/scenes/GameInputTest.scene.json`（新しい）

`Player.scene.json` を写し、最後に KeyConfig の Entity を足した物。エディタで Player シーンを開き、Entity を足して KeyConfigTest（Add Component → Gameplay → Input）を付けて「名前を付けて保存」でもよい。

```json
{
	"version": 5,
	"entities": [
		{
			"id": 1,
			"name": "GameCamera",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 5.0, -8.0],
					"rotation": [30.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Camera": {
					"fovY": 60.0,
					"nearZ": 0.1,
					"farZ": 1000.0,
					"priority": 0,
					"backgroundType": "Skybox",
					"backgroundColor": [0.192, 0.302, 0.475],
					"gradientSkyColor": [0.36, 0.58, 0.86],
					"gradientHorizonColor": [0.86, 0.9, 0.95],
					"gradientGroundColor": [0.42, 0.4, 0.38],
					"gradientHorizonWidth": 0.15,
					"blendStyle": "Cut",
					"blendTime": 1.0,
					"blendEase": "InOutCubic",
					"timeMode": "Game"
				},
				"Camera Follow": {
					"enabled": true,
					"target": 15981731572122588503,
					"plane": "View",
					"mode": "Center",
					"offset": [0.0, 1.5],
					"zoneSpace": "World",
					"deadZone": [0.5, 0.0],
					"softZone": [6.0, 4.0],
					"groundSnapY": true,
					"smoothTime": 0.15,
					"showGizmo": true,
					"useBounds": false,
					"boundsMin": [-10.0, -10.0],
					"boundsMax": [100.0, 30.0]
				},
				"Camera Shake": {
					"enabled": true,
					"noisePosition": [0.0, 0.0, 0.0],
					"noiseRotation": [0.0, 0.0, 0.0],
					"noiseFrequency": 1.0,
					"noiseGain": 1.0,
					"impulseGain": 1.0,
					"impulseFrequency": 8.0,
					"impulseDamping": 0.2,
					"impulseRotation": 2.0
				}
			}
		},
		{
			"id": 2,
			"name": "Directional Light",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 3.0, 0.0],
					"rotation": [50.0, -30.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Directional Light": {
					"color": [1.0, 1.0, 1.0],
					"intensity": 1.0,
					"showIcon": true,
					"showRange": true
				}
			}
		},
		{
			"id": 3,
			"name": "Ground",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, -0.05, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [20.0, 0.1, 20.0]
				},
				"Collider": {
					"enabled": true,
					"isTrigger": false,
					"shape": "Box",
					"center": [0.0, 0.0, 0.0],
					"physicMaterial": "",
					"size": [1.0, 1.0, 1.0]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [0.5882353, 0.5882353, 0.5882353, 1.0],
					"intensity": 1.0,
					"surface": "Opaque",
					"texture": "",
					"uvScale": [1.0, 1.0],
					"shadingType": "HalfLambert",
					"specular": [0.0, 0.0, 0.0],
					"shininess": 32.0,
					"metallic": 0.0,
					"roughness": 0.5,
					"emissive": [0.0, 0.0, 0.0],
					"receivesDecals": true
				}
			}
		},
		{
			"id": 4,
			"name": "Cube",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [3.0, 0.5, 3.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Collider": {
					"enabled": true,
					"isTrigger": false,
					"shape": "Box",
					"center": [0.0, 0.0, 0.0],
					"physicMaterial": "",
					"size": [1.0, 1.0, 1.0]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [0.9019608, 0.50980395, 0.19607843, 1.0],
					"intensity": 1.0,
					"surface": "Opaque",
					"texture": "",
					"uvScale": [1.0, 1.0],
					"shadingType": "HalfLambert",
					"specular": [0.0, 0.0, 0.0],
					"shininess": 32.0,
					"metallic": 0.0,
					"roughness": 0.5,
					"emissive": [0.0, 0.0, 0.0],
					"receivesDecals": true
				}
			}
		},
		{
			"id": 15981731572122588503,
			"name": "Player",
			"parent": 0,
			"active": true,
			"prefab": "resources/prefabs/Player.prefab",
			"components": {
				"Transform": {
					"translation": [0.0, 2.0, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				}
			}
		},
		{
			"id": 5,
			"name": "KeyConfig",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 0.0, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"KeyConfigTest": {
					"Enabled": true
				}
			}
		}
	]
}
```

## J-7 `Sandbox.vcxproj`・`Sandbox.vcxproj.filters`

```xml
    <ClCompile Include="Source\Scripts\Input\KeyConfigTest.cpp" />
```

```xml
    <ClInclude Include="Source\Scripts\Input\KeyConfigTest.h" />
```

filters（`Source\Scripts\Input` の Filter も足す）。

```xml
    <Filter Include="Source\Scripts\Input">
      <UniqueIdentifier>{5c2e8f71-4a39-4d6b-b8e2-91d07a6c3f58}</UniqueIdentifier>
    </Filter>
```

```xml
    <ClCompile Include="Source\Scripts\Input\KeyConfigTest.cpp">
      <Filter>Source\Scripts\Input</Filter>
    </ClCompile>
```

```xml
    <ClInclude Include="Source\Scripts\Input\KeyConfigTest.h">
      <Filter>Source\Scripts\Input</Filter>
    </ClInclude>
```

# パートK Reference

写しのブランチ `answer-game-input` の `Docs/` と同じ。直す所：

- **`Reference.md`** の索引：11 章の行に「Action の名前で読む入力（`GameInput`）・キーコンフィグ・2人で遊ぶ」を足す
- **`Reference/11-Input.md`**：「エディタの操作のキー」の「作る予定」を直し、下の節を足す
- **`Reference/17-Editor.md`**：Settings の Project に Input を足し、「Input（Settings → Project → Input）」の行を足す
- **`Reference/19-NotYet.md`**：「ゲームのキーの割り当て」の行を消し、「参加・キーボードを2人で分ける」「割り当てごとの倍率」「Action のまとまりごとに止める」の3行にする

11 章に足す節：

````markdown
## Action の名前で読む（GameInput・キーコンフィグ）

キー・ボタンを直接書かず、**「Jump」「Move」のような Action の名前で読む**。どのキー・ボタンかは、ゲームのファイル `Parameters/Input/InputActions.json`（git に入れて、チームで1つ）で決める。プレイヤーがキーコンフィグで変えた割り当ては、プレイヤーごとに保存される。

```cpp
// Script.h に入っているので、include は要らない
if (GameInput::IsTriggered("Jump")) { … }            // 押した瞬間（IsPressed：押している間、IsReleased：離した瞬間）
const Vector2 move = GameInput::GetVector2("Move");  // 2つの量（x が右・y が上。-1〜1）
const float zoom = GameInput::GetValue("Zoom");      // 1つの量（トリガー・ホイール）
const std::string key = GameInput::GetDisplayText("Jump"); // "Space" など（「Space でジャンプ」の案内に）
```

- **Action を足す・直すのは Settings → Project → Input**（[17章](17-Editor.md)）。名前・種類（Button・Value・Vector2）・グループ・効く場所（ぶつかりを調べる範囲）を決め、割り当てを押して拾う。Save で `Parameters/Input/InputActions.json` に書き、Play 中でもすぐ効く。
- 種類が Vector2・Value の割り当ては、**向きの行**に分けて並ぶ（Vector2 は 上・下・左・右（キー・ボタン）と 横の軸・縦の軸（スティック・マウスの動き））。行の ＋ で、その向きの割り当てを足す。軸は ＋ の向き（右・上）へ動かして拾う（反対へ動かすと「（反対）」になる）。
- 知らない名前は false・0 を返し、ログに1回だけ警告が出る（書き間違いはログで気づく）。
- 修飾キーは「書いた物を押していればよい」（Shift を押しながらでもジャンプできる）。エディタのパネルを触っている間は押していない（`InputGate::Game`）。

### キーコンフィグの画面（プレイヤーが変える）

画面はゲームの UI（Text・UI State など）で作り、`GameInput` に頼む。

```cpp
// 決めたとき：次に押したキー・ボタンを「Jump」の1つ目の割り当てにする（index を省くと足す）
GameInput::StartRebind("Jump", InputBindingRow::All, 0);

// 毎フレーム（Update）：結果を見る（GameInput がこのフレームの Script より前に拾っている）
switch (GameInput::GetRebindResult()) {
case InputRebinder::Result::Captured: /* 入った。ぶつかりは GameInput::GetMap().FindConflicts で見せる */ break;
case InputRebinder::Result::Canceled: /* Esc でやめた */ break;
default: break;
}

GameInput::SaveBindings();          // 保存（既定から変えた Action だけ）
GameInput::RevertBindings();        // 保存した割り当てに戻す
GameInput::GetMap().ResetAllToDefault(); // 全部を既定に戻す（残すなら SaveBindings）
```

- 割り当ての一覧・行は `GameInput::GetMap()`（`GetBindings`・`GetDesc`）と `InputBindingRows::Get(種類)`・`InputBindingRows::Of(種類, 割り当て)` で作る。行の名前（「上」など）はゲームの画面の文字なので、ゲームが決める。
- 拾っている間は、その人の全部の Action が効かない（決定のキーを拾っても、メニューが動かない）。拾い始めたときに押していた決定のキーは、離して押し直すまで拾わない。**Esc でやめる**（Esc は割り当てられない）。
- 「プレイヤーが変えられない」にした Action（メニューの決定など）は、`StartRebind` しても何もしない。画面にも出さない。
- 保存先は、ゲームのフォルダ（`Parameters/` と同じ所）の `UserData/Input/Player1.json`（エディタも製品版も同じ）。**ゲームのリポジトリの `.gitignore` に `UserData/` を足す**（遊んだ人ごとの物なので）。ゲームを書けない所（Program Files など）に置くと保存できない（ログに出る）。
- エディタで遊んで保存した割り当ては、Settings → Project → Input の一番下の「プレイヤーの上書きを消す」で消せる（消さないと、既定を直しても、その Action はプレイヤーの割り当てのまま）。
- 上書きは **Action ごと**（変えた Action は割り当ての全部を持つ）。既定に足した割り当ては、上書きのある Action には届かない。

### 2人で遊ぶ

```cpp
GameInput::SetPlayerCount(2);              // 1〜4
if (GameInput::IsTriggered("Jump", 1)) { … } // 2P（最後の引数。0 が 1P）
```

- 1P はキーボード・マウス・パッド0、2P 以降はパッドだけ（n 人目＝ n 番のパッド）。全員が同じファイルの既定から始まり、保存は人ごと（`Player2.json`）。
- ボタンを押した人がパッドを取る「参加」、キーボードを2人で分けるのは無い（[19章](19-NotYet.md)）。

```cpp
// ✗ だめな例
if (InputManager::IsKeyTriggered(DIK_SPACE)) { Jump(); } // ✗ キーの決め打ち。プレイヤーがキーコンフィグで変えられない
const Vector2 look = GameInput::GetVector2("Look");      // ✗ マウスの動きとスティックを1つの Action に混ぜている（下）
```

- **マウスの動きとスティックは別の Action にする**（`LookMouse`：割り当てられる入力を MouseMove だけ、`LookStick`：PadAxis だけ）。マウスはそのフレームに動いた数、スティックは倒した量（時間を掛けて使う）で、単位が違う。
- 今までの `InputManager::GetMoveAxis()`（WASD・矢印・左スティック）はそのまま使える。新しく書くなら `GameInput::GetVector2("Move")` にすると、キーコンフィグで変えられる。
````

# パートL 確かめ方

## L-1 写しで確かめた所（2026-11-14）

写し（`answer-game-input`）で Debug・Development・Release のビルドが通る（警告なし）。動きは、写しにだけ一時的に入れた入力の仕掛け（キー・マウスを流し込む。本体には入れない）で、Debug のエディタを動かして撮った。

| 確かめた所 | 結果 |
|---|---|
| Settings → Project に Input が出る・ファイルを読む | 6 個の Action。開いた直後は `*` が付かない |
| 表が行に分かれる | 移動：上（W・Up）・下・左・右・横の軸（Pad LeftStickX）・縦の軸（Pad LeftStickY）。Keymap の「前後に動く（ホイール）」は軸の行だけ |
| 縦の軸の ＋ → マウスを上へ動かす | 「Mouse MoveY（反対）」が入る（`Mouse.MoveY:-Y`）。モーダルに「＋ の向き（右・上）へ動かして」 |
| 右の ＋ → G | 右の行に G。ページの名前に `*` |
| Action を足す → 名前を Jump にする | 「保存できません: 同じ名前の Action が2つあります: Jump」 |
| 名前 Dash・表示名 ダッシュ → Save | ファイルに `{"name": "Dash", "label": "ダッシュ", "group": "動き", "bindings": []}`。`*` が消える |
| Save の後に Play | キーコンフィグにダッシュ・G・Mouse MoveY が出る（GameInput が読み直した） |
| Play → D | プレイヤーが右へ歩く |
| Tab | キーコンフィグが開く（時間が止まる）。閉じているときは「Tab でキーコンフィグ / Space でジャンプ」 |
| ↓↓ → Enter → J | 移動（左）が「J / Left」。拾い始めの Enter は拾わず、拾った後に Enter でメニューが動かない |
| ジャンプも J にする | 両方に「！「…」とぶつかる」、知らせに「入れ替えました（「移動」とぶつかっています）」 |
| 保存する | `UserData/Input/Player1.json` に Jump と Move だけ（`git status --ignored` で無視されている）。Input ページに「保存先: UserData/Input」 |
| 閉じて J | 左へ歩きながら跳ぶ。案内が「J でジャンプ」 |
| Stop → Input ページ | 「1 人分あります」→「プレイヤーの上書きを消す」でファイルが消える |
| Release で起動 | ログに「Action: 6 個」。保存するまで `UserData/` は作らない |

## L-2 手で見る所（本物のキー・マウス・パッド）

写しでは、ウィンドウを手前に出さずに入力を流し込んだので、次は本物で見てほしい。

1. `GameInputTest` を開いて Play。Game ビューを左クリックしてから、WASD・矢印で歩く・Space で跳ぶ。**パッドがあれば左スティックと A**。
2. Tab でキーコンフィグを開き、↑↓・Enter で「移動（上）」を選んで別のキーを押す。入れ替わり、その後 Enter を押し続けてもメニューが動かないこと。
3. もう一度選んで **Esc** → 「やめました」。
4. 「ジャンプ」を選び、**パッドのボタン**を押す → 「Pad X」などに入れ替わる。
5. 「移動（縦の軸）」を選び、**左スティックを下へ倒す** → 縦の軸が反対になる（キーコンフィグの見本は「（反対）」を出さないので、Input ページで確かめる）。スティックの上で下へ動くこと。
6. 「保存する」→ Stop → もう一度 Play で、変えた割り当てのまま。Settings → Project → Input の一番下に「1 人分あります」。
7. Input ページで、Vector2 の「上」の ＋ を押して **パッドの十字キーの上**を押す → 上の行に入る。横の軸の ＋ で **右スティックを右へ** → 横の軸に入る。
8. **Play 中に**、Input ページで Jump に割り当てを足して Save → そのまま Play 中のゲームで効く。
9. Release でビルドして、exe と `Parameters/`・`Resources/` などを別のフォルダに写して起動し、キーコンフィグで変えて保存 → そのフォルダに `UserData/Input/Player1.json` ができる。もう一度起動して、変えたままか。
10. 日本語入力を ON にしたまま、キーコンフィグでキーを拾えるか（DirectInput で読むので拾えるはず）。

## L-3 まだ見ていない所

- 2人（`SetPlayerCount(2)` とパッド2つ）。Sandbox に2人のテストは無い。
- Value の Action（トリガーの量・ホイール）を、ゲームで読む所。

# パートM 表を右クリックで直す（× と「既定」のボタンをやめる）

2026-11-15 に足した（本体へ入れた後に）。**同じ日に本体へ直接入れた**（写しの `c3e6ca9` の M-1〜M-4 を当てた。未コミット）。割り当ての並びを見やすくするため、各割り当ての右の × と、行の右の「既定」のボタンをやめ、右クリックのメニューにする。エディタの Keymap ページと Input ページの両方が変わる（同じ InputActionTable を使っている）。

| 操作 | どうなる |
|---|---|
| 行の左端の ＋ | その行に足す（今まで通り。並びの長さが変わっても同じ所にあるように、左端へ動かした） |
| 割り当てを左クリック | 入れ替える（今まで通り） |
| 割り当てを右クリック | 「変更」（左クリックと同じ）・「消す」 |
| 行の空いている所・Action の名前を右クリック | 「足す」（Vector2・Value の行なら「「上」に足す」のように、その行に）・「既定に戻す」（横に既定の割り当て。既定のままなら押せない） |
| Action の名前の `*` | 既定から変えている（「既定」のボタンが無くなったので、変えたかが分かるように） |

- 空いている所は、行の右端まで見えないボタン（`InvisibleButton`）で埋めて、そこへの右クリックを受ける。
- 「変えられない」Action（Keymap の「選ぶ（クリック）」）は、＋ もメニューも出さない。
- Input ページでは「既定に戻す」の文字が「保存した割り当てに戻す」になる（コンストラクタの引数を、吹き出しの文字から、メニューの文字に変えた）。
- ぶつかりの「「…」の方を外す」ボタンは残す（相手の行まで行かずに直せるので）。

## M-1 `Editor/Widgets/InputActionTable.h`（全体を差し替え）

```cpp
#pragma once
#include <string>

#include "HTTEngine/Input/Action/InputActionMap.h"
#include "HTTEngine/Input/Action/InputRebinder.h"

/// <summary>
/// Input Action の表の割り当てを直す ImGui の表（Settings の Keymap ページ・Input ページ。表1つに1つ持つ）
/// <para>Action を group ごとに見出しを付けて並べ、割り当てをボタンで出す。行の左端の ＋ で足す</para>
/// <para>割り当てを押す（右クリック →「変更」でも）と、次に押したキー・ボタンを拾って入れ替える。右クリック →「消す」で外す</para>
/// <para>行の空いている所・Action の名前を右クリックすると「足す」「既定に戻す」（× や「既定」のボタンは置かない。並びを見やすくするため）</para>
/// <para>Value・Vector2 は、割り当てを行に分けて出す（Vector2 なら 上・下・左・右 のキー・ボタンと、横・縦 の軸。InputBindingRow）。行の ＋ は、その行の向きで拾う</para>
/// <para>ぶつかっている割り当ては赤くし、相手の名前と「相手の方を外す」ボタンを出す</para>
/// </summary>
class InputActionTable {
public:
	// resetLabel は、右クリックのメニューの「既定に戻す」の文字（Input ページでは「保存した割り当てに戻す」）
	explicit InputActionTable(std::string resetLabel = "既定に戻す") : resetLabel_(std::move(resetLabel)) {}

	// 表を描く（ページの中から毎フレーム。拾っている間は、案内のモーダルも出す）
	void Draw(InputActionMap& map);
	// 拾う係を動かし、拾えたら map に入れる（毎フレーム1回、フレームの頭に。表の Update の前）
	// <para>入力を読めるのはフレームの頭（WindowManager::UpdateAll の中）だけなので、Draw の中では動かさない</para>
	// <para>前のフレームに表を描いていなければ（ページを閉じた・ほかのページにした）、拾うのをやめる</para>
	void UpdateListening(InputActionMap& map);
	// 割り当てを拾っているか（その間、表の SetEnabled(false) にしておく）
	bool IsListening() const { return rebinder_.IsListening(); }
	// 拾うのをやめる（表の Action を足す・消すなど、番号が変わるとき）
	void CancelListening() { rebinder_.Cancel(); }

private:
	// Action 1つ（行ごとに、名前と割り当て）
	void DrawAction(InputActionMap& map, InputActionId id);
	// 割り当ての1行（行の名前・＋・その行の割り当て・空いている所）
	void DrawRow(InputActionMap& map, InputActionId id, InputBindingRow row);
	// 割り当て1つのボタン（押すと拾い始める。右クリックで「変更」「消す」）。消したら true（後ろが詰まるので、その行の残りを描かない）
	bool DrawBinding(InputActionMap& map, InputActionId id, int index, InputBindingRow row);
	// 右クリックのメニュー（行の空いている所・Action の名前）。row の行に「足す」、Action を「既定に戻す」
	void DrawActionMenu(InputActionMap& map, InputActionId id, InputBindingRow row, const char* popupId);
	// 行に割り当てを足し始める（行の ＋・右クリックの「足す」）
	void StartAdd(const InputActionMap& map, InputActionId id, InputBindingRow row);
	// ぶつかっている相手（行の下に出す）
	void DrawConflicts(InputActionMap& map, InputActionId id, int index);
	// 拾っている間の案内（モーダル。ほかを触れないようにして、押したクリックが別の所に効かないように）
	void DrawListening(const InputActionMap& map);

	InputRebinder rebinder_;
	std::string resetLabel_; // 右クリックのメニューの「既定に戻す」の文字
	int lastDrawFrame_ = -1; // 最後に表を描いたフレーム（ImGui::GetFrameCount）
};
```

## M-2 `Editor/Widgets/InputActionTable.cpp`（全体を差し替え）

```cpp
#ifdef USE_IMGUI
#include "InputActionTable.h"

#include <format>
#include <string>
#include <vector>

#include <externals/imgui/imgui.h>

namespace {
constexpr float kLabelWidth = 200.0f;                               // Action の名前の列の幅
constexpr float kRowLabelWidth = 56.0f;                             // 行の名前（上・下・横の軸など）の幅
constexpr ImVec4 kConflictButtonColor = {0.62f, 0.20f, 0.20f, 1.0f}; // ぶつかっている割り当てのボタン
constexpr ImVec4 kConflictTextColor = {1.0f, 0.50f, 0.45f, 1.0f};    // ぶつかりの説明の文字
constexpr const char* kListeningPopupId = "割り当てを拾う";          // 拾っている間のモーダルの名前（ID にもなる）
constexpr float kSmallButtonScale = 0.75f;                          // ＋ の文字の大きさ（ふつうの文字の何倍か）
constexpr const char* kBindingMenuId = "##bindingMenu";             // 割り当てを右クリックしたメニュー
constexpr const char* kRowMenuId = "##rowMenu";                     // 行の空いている所を右クリックしたメニュー
constexpr const char* kLabelMenuId = "##labelMenu";                 // Action の名前を右クリックしたメニュー

// 小さいボタン（＋）。文字を小さくし、同じ行の割り当てのボタンと上下の真ん中をそろえる
bool SmallRowButton(const char* label) {
	const float rowHeight = ImGui::GetFrameHeight(); // 割り当てのボタン（ふつうの文字）の高さ
	ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * kSmallButtonScale);
	// 小さいボタンの高さ（小さい文字の高さ）との差の半分だけ下げる
	// SmallButton は、同じ行の文字の高さに合わせて自分でも FramePadding.y だけ下がるので、その分は引く
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f - ImGui::GetStyle().FramePadding.y);
	const bool isPressed = ImGui::SmallButton(label);
	ImGui::PopFont();
	return isPressed;
}

// 割り当ての一覧を "A / B" の文字にする（「既定に戻す」の横に出す）
std::string JoinBindings(const std::vector<InputBinding>& bindings) {
	std::string text;
	for (const InputBinding& binding : bindings) {
		text += text.empty() ? "" : " / ";
		text += InputBindingText::ToDisplayString(binding);
	}
	return text.empty() ? "（なし）" : text;
}

// 行の名前（All は出さない）
const char* RowLabel(InputBindingRow row) {
	switch (row) {
	case InputBindingRow::All:
		return "";
	case InputBindingRow::Positive:
		return "＋";
	case InputBindingRow::Negative:
		return "−";
	case InputBindingRow::Axis:
		return "軸";
	case InputBindingRow::Up:
		return "上";
	case InputBindingRow::Down:
		return "下";
	case InputBindingRow::Left:
		return "左";
	case InputBindingRow::Right:
		return "右";
	case InputBindingRow::Horizontal:
		return "横の軸";
	case InputBindingRow::Vertical:
		return "縦の軸";
	}
	return "";
}

// 行の説明（行の名前の吹き出し）
const char* RowTooltip(InputBindingRow row) {
	switch (row) {
	case InputBindingRow::Positive:
	case InputBindingRow::Negative:
		return "押している間、この向きに 1 を足すキー・ボタン";
	case InputBindingRow::Axis:
		return "量をそのまま足す軸（トリガー・ホイール・スティック）。（反対）は、引いて足す";
	case InputBindingRow::Up:
	case InputBindingRow::Down:
	case InputBindingRow::Left:
	case InputBindingRow::Right:
		return "押している間、この向きに 1 を足すキー・ボタン";
	case InputBindingRow::Horizontal:
		return "横の量をそのまま足す軸（スティックの横・マウスの横の動き）。両側に効く（＋に倒すと右）。（反対）は左右が逆";
	case InputBindingRow::Vertical:
		return "縦の量をそのまま足す軸（スティックの縦・マウスの縦の動き）。両側に効く（＋に倒すと上）。（反対）は上下が逆";
	default:
		return "";
	}
}

// 行に何か出すか（入れられる入力があるか、もう割り当てが入っている）
bool HasRow(const InputActionMap& map, InputActionId id, InputBindingRow row) {
	if ((map.GetAcceptedDevices(id) & InputBindingRows::DevicesOf(row)) != InputDeviceMask::None) {
		return true;
	}
	const InputActionType type = map.GetDesc(id).type;
	for (const InputBinding& binding : map.GetBindings(id)) {
		if (InputBindingRows::Of(type, binding) == row) {
			return true;
		}
	}
	return false;
}
} // namespace


//=============================================================================
// 表
//=============================================================================
// ===== 全部 =====
void InputActionTable::Draw(InputActionMap& map) {
	lastDrawFrame_ = ImGui::GetFrameCount();

	// --- group ごとに見出しを付け、表を分ける（同じ group が続く間は、同じ表）---
	std::string currentGroup;
	bool isTableOpen = false;
	for (uint32_t i = 0; i < map.GetActionCount(); ++i) {
		const InputActionId id = {i};
		const InputActionDesc& desc = map.GetDesc(id);
		if (i == 0 || desc.group != currentGroup) {
			if (isTableOpen) {
				ImGui::EndTable();
			}
			currentGroup = desc.group;
			ImGui::SeparatorText(currentGroup.c_str());
			isTableOpen = ImGui::BeginTable(std::format("##group{}", i).c_str(), 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit);
			if (isTableOpen) {
				ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, kLabelWidth);
				ImGui::TableSetupColumn("bindings", ImGuiTableColumnFlags_WidthStretch);
			}
		}
		if (isTableOpen) {
			ImGui::PushID(static_cast<int>(i));
			DrawAction(map, id);
			ImGui::PopID();
		}
	}
	if (isTableOpen) {
		ImGui::EndTable();
	}

	DrawListening(map);
}

// ===== 拾う係を動かす（フレームの頭）=====
void InputActionTable::UpdateListening(InputActionMap& map) {
	if (!rebinder_.IsListening()) {
		return;
	}
	// --- 前のフレームに表を描いていなければ（ページを閉じた・ほかのページにした）、やめる ---
	// フレームの頭（ImGui::NewFrame の前）では、GetFrameCount は前のフレームの番号。前のフレームに描いていれば同じ
	if (lastDrawFrame_ < ImGui::GetFrameCount()) {
		rebinder_.Cancel();
		return;
	}
	// --- 拾えたら、表に入れる ---
	const InputActionId id = rebinder_.GetAction();
	const int index = rebinder_.GetIndex();
	if (rebinder_.Update() == InputRebinder::Result::Captured) {
		map.SetBinding(id, index, *rebinder_.GetCaptured());
	}
}


//=============================================================================
// Action 1つ
//=============================================================================
void InputActionTable::DrawAction(InputActionMap& map, InputActionId id) {
	const InputActionDesc& desc = map.GetDesc(id);
	bool isFirstRow = true;
	for (const InputBindingRow row : InputBindingRows::Get(desc.type)) {
		if (!HasRow(map, id, row)) {
			continue; // 入れられる入力が無い行（ホイールだけの Value の ＋・− など）
		}
		ImGui::TableNextRow();
		ImGui::PushID(static_cast<int>(row));

		// --- 名前（1行目だけ。既定から変えていれば * を付ける。右クリックで「足す」「既定に戻す」）---
		ImGui::TableNextColumn();
		if (isFirstRow) {
			ImGui::AlignTextToFramePadding();
			ImGui::Text("%s%s", desc.label.c_str(), (!desc.isFixed && !map.IsDefault(id)) ? " *" : "");
			if (!desc.tooltip.empty()) {
				ImGui::SetItemTooltip("%s", desc.tooltip.c_str());
			}
			DrawActionMenu(map, id, row, kLabelMenuId);
		}

		// --- 割り当て ---
		ImGui::TableNextColumn();
		DrawRow(map, id, row);
		ImGui::PopID();
		isFirstRow = false;
	}
}

// ===== 割り当ての1行 =====
void InputActionTable::DrawRow(InputActionMap& map, InputActionId id, InputBindingRow row) {
	const InputActionDesc& desc = map.GetDesc(id);
	// --- 行の名前（Button は出さない）---
	if (row != InputBindingRow::All) {
		const float startX = ImGui::GetCursorPosX();
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("%s", RowLabel(row));
		ImGui::SetItemTooltip("%s", RowTooltip(row));
		ImGui::SameLine();
		ImGui::SetCursorPosX(startX + kRowLabelWidth); // 行の名前の幅をそろえる（割り当てのボタンが縦に並ぶように）
	}

	// --- 足す（行の左端。並びが変わっても同じ所にあるように）---
	if (!desc.isFixed) {
		if (SmallRowButton("+")) {
			StartAdd(map, id, row);
		}
		ImGui::SetItemTooltip("%s", InputBindingRows::IsAxisRow(row) ? "軸を足す（押してから、スティックを ＋ の向き（右・上）へ倒す。反対へ倒すと、反対に足す）" : "割り当てを足す（押してから、足したいキー・ボタンを押す）");
		ImGui::SameLine();
	}

	// --- この行の割り当て（ボタンを横に並べる）---
	bool hasAny = false;
	for (int index = 0; index < static_cast<int>(map.GetBindings(id).size()); ++index) {
		if (InputBindingRows::Of(desc.type, map.GetBindings(id)[index]) != row) {
			continue;
		}
		hasAny = true;
		if (DrawBinding(map, id, index, row)) {
			return; // 消したので、後ろが詰まった（次のフレームで描き直す）
		}
		ImGui::SameLine();
	}
	if (!hasAny) {
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("（なし）");
		ImGui::SameLine();
	}
	if (desc.isFixed) {
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("変えられません");
	} else {
		// --- 空いている所（右クリックで「足す」「既定に戻す」）---
		// 見えないボタンで行の右端まで埋め、そこへの右クリックを受ける
		const float width = ImGui::GetContentRegionAvail().x;
		if (width > 1.0f) {
			ImGui::InvisibleButton("##rowSpace", ImVec2(width, ImGui::GetFrameHeight()));
			DrawActionMenu(map, id, row, kRowMenuId);
		} else {
			ImGui::NewLine();
		}
	}

	// --- ぶつかり（この行の割り当てごとに、並びの下に1行ずつ）---
	for (int index = 0; index < static_cast<int>(map.GetBindings(id).size()); ++index) {
		if (InputBindingRows::Of(desc.type, map.GetBindings(id)[index]) == row) {
			DrawConflicts(map, id, index);
		}
	}
}

// ===== 割り当て1つのボタン =====
bool InputActionTable::DrawBinding(InputActionMap& map, InputActionId id, int index, InputBindingRow row) {
	const InputActionDesc& desc = map.GetDesc(id);
	const InputBinding binding = map.GetBindings(id)[index]; // 写しておく（外すと、元の物は消える）
	const bool isListeningHere = rebinder_.IsListening() && rebinder_.GetAction() == id && rebinder_.GetIndex() == index;
	const bool hasConflict = !map.FindConflicts(id, index).empty();

	ImGui::PushID(index);
	// --- ボタン（押すと拾い始める。ぶつかっていれば赤）---
	std::string text = isListeningHere ? std::string("押してください…") : InputBindingText::ToDisplayString(binding);
	if (text.empty()) {
		text = "?"; // 名前の無い入力（ふつうは無い）
	}
	// 軸の行で、反対に足す割り当て（-X・-Y）
	if (!isListeningHere && InputBindingRows::IsAxisRow(row) && InputBindingRows::DirectionOf(row) != binding.direction) {
		text += "（反対）";
	}
	if (hasConflict) {
		ImGui::PushStyleColor(ImGuiCol_Button, kConflictButtonColor);
	}
	ImGui::BeginDisabled(desc.isFixed);
	if (ImGui::Button(text.c_str())) {
		rebinder_.Start(map, id, index, row);
	}
	ImGui::EndDisabled();
	if (hasConflict) {
		ImGui::PopStyleColor();
	}
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
		ImGui::SetTooltip("%s", desc.isFixed ? "ほかの所でも、この入力を決め打ちで使っているので変えられません" : "押してから、新しいキー・ボタンを押すと入れ替わります（右クリックで 変更・消す）");
	}

	// --- 右クリックのメニュー（変更・消す）---
	bool isRemoved = false;
	if (!desc.isFixed && ImGui::BeginPopupContextItem(kBindingMenuId)) {
		if (ImGui::MenuItem("変更")) {
			rebinder_.Start(map, id, index, row);
		}
		ImGui::SetItemTooltip("押した後に、新しいキー・ボタンを押すと入れ替わる");
		if (ImGui::MenuItem("消す")) {
			map.RemoveBinding(id, index);
			isRemoved = true;
		}
		ImGui::EndPopup();
	}
	ImGui::PopID();
	return isRemoved;
}

// ===== 右クリックのメニュー（行の空いている所・Action の名前）=====
void InputActionTable::DrawActionMenu(InputActionMap& map, InputActionId id, InputBindingRow row, const char* popupId) {
	const InputActionDesc& desc = map.GetDesc(id);
	if (desc.isFixed || !ImGui::BeginPopupContextItem(popupId)) {
		return;
	}
	// --- 足す（その行の向きで拾う）---
	const std::string addLabel = (row == InputBindingRow::All) ? std::string("足す") : std::format("「{}」に足す", RowLabel(row));
	if (ImGui::MenuItem(addLabel.c_str())) {
		StartAdd(map, id, row);
	}
	// --- 既定に戻す（Action の割り当てを全部。横に既定の割り当てを出す）---
	if (ImGui::MenuItem(resetLabel_.c_str(), JoinBindings(desc.defaults).c_str(), false, !map.IsDefault(id))) {
		map.ResetToDefault(id);
	}
	ImGui::EndPopup();
}

// ===== 行に割り当てを足し始める =====
void InputActionTable::StartAdd(const InputActionMap& map, InputActionId id, InputBindingRow row) {
	rebinder_.Start(map, id, static_cast<int>(map.GetBindings(id).size()), row);
}

// ===== ぶつかっている相手 =====
void InputActionTable::DrawConflicts(InputActionMap& map, InputActionId id, int index) {
	const std::vector<InputBindingRef> conflicts = map.FindConflicts(id, index);
	const std::string bindingText = InputBindingText::ToDisplayString(map.GetBindings(id)[index]);
	for (const InputBindingRef& other : conflicts) {
		const InputActionDesc& otherDesc = map.GetDesc(other.action);
		ImGui::PushID(index);
		ImGui::PushID(static_cast<int>(other.action.index));
		ImGui::PushID(other.index);
		ImGui::AlignTextToFramePadding();
		ImGui::TextColored(kConflictTextColor, "! %s は「%s」とぶつかっています", bindingText.c_str(), otherDesc.label.c_str());
		bool isRemoved = false;
		if (!otherDesc.isFixed) {
			ImGui::SameLine();
			if (ImGui::SmallButton(std::format("「{}」の方を外す", otherDesc.label).c_str())) {
				map.RemoveBinding(other.action, other.index);
				isRemoved = true;
			}
		}
		ImGui::PopID();
		ImGui::PopID();
		ImGui::PopID();
		if (isRemoved) {
			break; // 相手の並びが詰まったので、残りの一覧は古い（次のフレームで描き直す）
		}
	}
}


//=============================================================================
// 拾っている間
//=============================================================================
void InputActionTable::DrawListening(const InputActionMap& map) {
	// 拾う係はここでは動かさない（UpdateListening）。ImGui を描く間は、InputManager がウィンドウを手前でない扱いにしていて、何も読めない
	// --- 案内のモーダル（拾い終えたら閉じる）---
	if (rebinder_.IsListening() && !ImGui::IsPopupOpen(kListeningPopupId)) {
		ImGui::OpenPopup(kListeningPopupId);
	}
	if (ImGui::BeginPopupModal(kListeningPopupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		if (!rebinder_.IsListening()) {
			ImGui::CloseCurrentPopup();
		} else {
			const InputBindingRow row = rebinder_.GetRow();
			const std::string& label = map.GetDesc(rebinder_.GetAction()).label;
			if (row == InputBindingRow::All) {
				ImGui::Text("「%s」に割り当てるキー・ボタンを押してください", label.c_str());
			} else if (InputBindingRows::IsAxisRow(row)) {
				ImGui::Text("「%s」の「%s」に割り当てる軸を、＋ の向き（右・上）へ動かしてください", label.c_str(), RowLabel(row));
				ImGui::TextDisabled("反対の向きへ動かすと、反対に足す割り当てになります");
			} else {
				ImGui::Text("「%s」の「%s」に割り当てるキー・ボタンを押してください", label.c_str(), RowLabel(row));
			}
			ImGui::TextDisabled("Ctrl・Shift・Alt を押しながら押すと、組にして割り当てます（Ctrl+Z など）");
			ImGui::TextDisabled("Esc でやめる");
		}
		ImGui::EndPopup();
	}
}
#endif
```

## M-3 `Editor/Keymap/GameInputPage.cpp`（コメントだけ）

```cpp
// 表の「既定」は保存した割り当て（右クリックの「保存した割り当てに戻す」）。今の割り当ては SetBindings で入れる
```

## M-4 `Docs/Reference/17-Editor.md`

Keymap の行の「＋で足す・×で外す・「既定」で戻す。」を、次にする。

```
行の左端の ＋ で足す。割り当てを右クリックすると「変更」「消す」、行の空いている所か名前を右クリックすると「足す」「既定に戻す」（既定から変えた操作は名前に `*`）。
```

Input の行の「「既定」は保存した割り当てに戻す。」を「右クリックの「保存した割り当てに戻す」で、保存した割り当てに戻す。」にする。

## M-5 写しで確かめた所

写しの `c3e6ca9`。Debug のエディタを入力の仕掛けで動かして撮った。

- Input ページ・Keymap ページとも、× と「既定」が無く、＋ が行の左端にそろう（行の名前の後ろ）
- ジャンプの「Pad A」を右クリック →「変更」「消す」。「消す」で外れ、名前が「ジャンプ *」、ページが「Input *」
- ジャンプの行の空いている所を右クリック →「足す」「保存した割り当てに戻す　Space / Pad A」。戻すと Pad A が戻り、`*` が消える
- 移動の縦の軸の行の空いている所を右クリック →「「縦の軸」に足す」
- Keymap の「前後に動く（ホイール）」は軸の行に ＋ と Mouse Wheel。「選ぶ（クリック）」は ＋ が無く「変えられません」

