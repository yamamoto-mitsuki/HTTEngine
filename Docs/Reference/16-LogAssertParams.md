> HTTEngine リファレンスの1章。ほかの章は [索引](../Reference.md) から探す。

# 16. ログ・アサート・調整項目

```cpp
LogManager::Log("Push!");            // logs/日付.log に残る
LogManager::Warning("敵が多すぎる");
LogManager::Error("モデルが読めない");

HTT_ASSERT_MSG(handle != 0, "モデルを読み込んでから呼んでください"); // 外れたら通知が出てログにも残る
```

`HTT_ASSERT` / `HTT_ASSERT_MSG` は**どの構成でも条件を確かめる**（Release でも消えない）。外れたら通知（「logを開く」ボタン付き）を出してログに残し、Debug・Development は `assert` で止まり、Release はそのまま終わる（`std::abort`）。条件の式は Release でも毎回計算されるので、重い計算は書かない。

数値の調整は `GlobalVariables`（Settings ウィンドウで触って、jsonに保存される）。**登録する関数と、読み戻す関数の2つを書く**のが決まり。

```cpp
// 登録（Initializeで1回）。Group → Category → Add をつなげて書く
void GameScene::RegisterGV() {
	auto gv = GlobalVariables::GetInstance()->Group("GameScene");
	gv.Category("Player").Add("Speed", playerSpeed_).Add("JumpPower", jumpPower_);
}

// 読み戻す（Updateで毎フレーム）
void GameScene::ApplyGV() {
	auto* gv = GlobalVariables::GetInstance();
	playerSpeed_ = gv->Get<float>("GameScene", "Player", "Speed");
	jumpPower_ = gv->Get<float>("GameScene", "Player", "JumpPower");
}
```

`Add` は**登録済みなら値を上書きしない**（jsonから読んだ値が生き残る）。`Category("Enemy").Category("Move")` と重ねると階層になり、`Get<float>("GameScene", "Enemy/Move", "Speed")` で引ける。

> **Inspectorで触れる物はComponentに入れる方がよい。** `GlobalVariables` は「Entityに紐づかない全体の設定」向け。

- **1グループ＝ Settings ウィンドウの1ページ**。左の一覧の見出しは Project（ゲーム全体。`Parameters/<グループ>/<カテゴリ>.json`。エンジンの Audio・PostEffect・Physics・Layers・DamageTypes もここ）・Scene（開いているシーンの名前が出るだけで、まだ項目は無い）・Editor（エディタだけ。`Parameters/Editor/<グループ>/`。製品版では使わない）。ゲームのグループは `Group("名前")` のまま（Project に出る）。エディタだけの物は `Group("名前", SettingsSection::Editor)`。**見出し（置き場所）は、そのグループを最初に作った `Group(...)` で決まる**（2回目からの `Group` は、もうあるグループを返すだけで、渡した見出しは使わない）。なので `SettingsSection` は、最初に呼ぶ所（ふつうは `Add` を並べる所）に書く。
- 保存していない変更があるページには `*`。ページの **Save** でファイルに書き、**Revert** で保存した値に戻す。
- Settings ウィンドウで変えた値も **Ctrl+Z / Ctrl+Y** で戻る（Inspector と同じく、つまんで離したら1件。Revert も1件）。ただしシーンには入っていないので、Play 中に変えた値は Stop しても戻らない。
- `Add` で項目を作ったときに、保存したファイルにその項目があればその値になる（シーンを切り替えて後から登録しても、保存した値から始まる）。
- **シーンの `Initialize` で `GlobalVariables::GetInstance()->LoadFiles()` を呼ばない**（全部のグループをファイルから読み直すので、Play を押すたびに、保存していない PostEffect・音量などが戻ってしまう）。
