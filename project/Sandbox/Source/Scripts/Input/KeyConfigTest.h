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
