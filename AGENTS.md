# HTTEngine — AI 向けの案内
## 必ず守ること
- 返答・コメント・コミットメッセージは日本語。
- `project/externals/` の外部ライブラリの元のファイルは書き換えない。設定を変えたいときはエンジン側（props など）で対応する。自分で足すファイル（`imgui.vcxproj` など）は置いてよい。
- エンジンの型名・コメント・Tooltip に、特定のゲームの話を書かない（複数のゲームで使い回すため）。
- 1つの機能のために、共有の定数（`kClearColor` など）の値を変えない。専用の値を足す。変える前に使っている所を grep する。
- ファイルを足したら `HTTEngine.vcxproj` と `.vcxproj.filters` の両方に足す。
- C++ を書く・直す前に `rules/coding-style.md` を読む。

## フォルダ
| 場所 | 中身 |
|---|---|
| `project/HTTEngine/` | エンジン本体（スタティックライブラリ）。分野ごとのフォルダ（Graphics・ECS・Editor・Components・Systems など） |
| `project/Sandbox/` | エンジンの確認用の exe。機能を足したらここのテストシーンで確かめる |
| `project/externals/` | 外部ライブラリ（DirectXTex・imgui・spdlog など。assimp は vcpkg が入れる） |
| `project/HTTEngine.props` | ゲームが読む設定（include・ライブラリ・参照・vcpkg・実行に要るファイルの写し） |
| `generated/` | ビルドの出力（git に入れない） |
| `Docs/` | 設計の理由とリファレンス |

ファイルの置き場所は名前の終わりで決まる（`〜Component` は `Components/カテゴリ/`、`〜System` は `Systems/カテゴリ/` など）。表は `Docs/ARCHITECTURE.md` の「フォルダの決まり」。

## ビルドと確認
- ソリューションは `project/HTTEngine.slnx`。構成は Debug・Development（エディタあり・最適化なし）・Release の 3 つで、x64 だけ。
- コマンドでビルドするときは 64 ビット版の MSBuild（`MSBuild\Current\Bin\amd64\MSBuild.exe`）を使う。
  `MSBuild.exe project/HTTEngine.slnx -m -p:Configuration=Debug -p:Platform=x64`
- 初回は vcpkg が assimp を入れるので数分かかる。Visual Studio に「vcpkg パッケージマネージャー」が要る。
- 自動テストは無い。Sandbox を起動し、`project/Sandbox/Resources/scenes/` のテストシーンで動きを確かめる。
- 起動時に走る自己テストのコードはエンジンに入れない。
- push すると GitHub Actions が Debug と Release をビルドする（`.github/workflows/`）。

## 索引（必要なときだけ開く）
| いつ読むか | ファイル |
|---|---|
| C++ を書く・直す前 | `rules/coding-style.md` |
| ブランチを切る・コミットする・vcxproj がぶつかったとき | `rules/git-workflow.md` |
| Entity・Component・System の分け方、フレームの順番、生成と破棄の決まり | `Docs/ARCHITECTURE.md` |
| ゲーム側から使う API と書き方（良い例・だめな例） | `Docs/Reference.md`（索引。表で章を見つけ、`Reference/` の下のその章のファイルだけを開く） |

## 作業の進め方
- 新しい作業は `develop` から `feature/〜` を切る。終わったら `develop` に入れ、区切りで `master` にそろえた後 `feature/〜` を削除する。
- 大きな変更は、先に設計（どの層の責務か・既存の何を使うか）を決めてから書く。その場しのぎの回避策で済ませない。
- 既存の名前とぶつかる・区別しにくいときは、既存の名前を変えてもよい（ゲーム側が直す必要が出てもよい）。