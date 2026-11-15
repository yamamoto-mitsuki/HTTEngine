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
