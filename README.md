# MyEngine_v1
DirectX 12 で作っている自作ゲームエンジン（静的ライブラリ）。ゲームは別のリポジトリから参照して使う。
エンジンの起動確認・機能テストは、同じリポジトリの `project/Sandbox/`（exe）で行う。

## ビルドの状態
### master
[![DebugBuild](https://github.com/yamamoto-mitsuki/MyEngine_v1/actions/workflows/DebugBuild.yml/badge.svg?branch=master)](https://github.com/yamamoto-mitsuki/MyEngine_v1/actions/workflows/DebugBuild.yml?query=branch%3Amaster) | [![ReleaseBuild](https://github.com/yamamoto-mitsuki/MyEngine_v1/actions/workflows/ReleaseBuild.yml/badge.svg?branch=master)](https://github.com/yamamoto-mitsuki/MyEngine_v1/actions/workflows/ReleaseBuild.yml?query=branch%3Amaster) | [![CheckUnwantedFiles](https://github.com/yamamoto-mitsuki/MyEngine_v1/actions/workflows/CheckUnwantedFiles.yml/badge.svg?branch=master)](https://github.com/yamamoto-mitsuki/MyEngine_v1/actions/workflows/CheckUnwantedFiles.yml?query=branch%3Amaster) |
### develop
[![DebugBuild](https://github.com/yamamoto-mitsuki/MyEngine_v1/actions/workflows/DebugBuild.yml/badge.svg?branch=develop)](https://github.com/yamamoto-mitsuki/MyEngine_v1/actions/workflows/DebugBuild.yml?query=branch%3Adevelop) | [![ReleaseBuild](https://github.com/yamamoto-mitsuki/MyEngine_v1/actions/workflows/ReleaseBuild.yml/badge.svg?branch=develop)](https://github.com/yamamoto-mitsuki/MyEngine_v1/actions/workflows/ReleaseBuild.yml?query=branch%3Adevelop) | [![CheckUnwantedFiles](https://github.com/yamamoto-mitsuki/MyEngine_v1/actions/workflows/CheckUnwantedFiles.yml/badge.svg?branch=develop)](https://github.com/yamamoto-mitsuki/MyEngine_v1/actions/workflows/CheckUnwantedFiles.yml?query=branch%3Adevelop) |

## 取ってくるとき
外部ライブラリは `project/externals` にまとめている。assimp は vcpkg で用意する（`project/externals/vcpkg.json`）。clone してビルドするだけでよい。

1. `project/MyEngine_v1.slnx` を Visual Studio で開く
2. 構成（Debug・Development・Release）を選んで F5。エンジンと Sandbox がビルドされ、Sandbox が起動する（最初のシーンは `SampleScene`）

- Visual Studio に「vcpkg パッケージ マネージャー」が入っている必要がある（Visual Studio Installer の「C++ によるデスクトップ開発」の中）
- 最初のビルドは、assimp のビルドで数分かかる。ネットにつながっている必要がある
- 2 回目からは `%LOCALAPPDATA%\vcpkg\archives` のキャッシュを使うので、すぐ終わる
