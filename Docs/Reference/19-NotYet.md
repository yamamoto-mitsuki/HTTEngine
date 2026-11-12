> HTTEngine リファレンスの1章。ほかの章は [索引](../Reference.md) から探す。

# 19. まだ無い物

**作っていない物を正直に書いておく。** 2026-11-10 にコードと照らして書き直した（それより前にあった行のうち、もう作った物は消した）。

| 無い物 | 今どうするか |
|---|---|
| 回る箱（OBB）の Collider | Collider の Box は軸にそろった箱（AABB）、Capsule はいつも立ったまま。Entity を回しても当たりの形は回らない |
| ゲームのキーの割り当て（Script から名前で読む入力・プレイヤーのキーコンフィグ） | Input Action の表（`InputActionMap`）はあるが、ゲームから使う窓口・プレイヤーの割り当ての保存先はまだ。キー・ボタンを直接読む（移動は `InputManager::GetMoveAxis`）。エディタの操作は Keymap で変えられる（[17章](17-Editor.md)） |
| 3D の音の前後・ドップラー | 3D の音は大きさと左右だけ |
| ゲームのカメラの正射影 | 透視だけ。正射影っぽく見せたいときは視野角を小さくして遠くから写す |
| 画面分割（カメラ2つを同時に描く） | 描くカメラは1つ（Priority の大きい物） |
| スプライトのコマ送りアニメ（コマを自動で送る再生の仕組み） | コマを並べた画像の1コマを出すのはある（Sprite Renderer・Image の Columns・Rows・Frame）。コマを送るのは、Script で `frame` を書き換えるか、Timeline の値のトラックで `frame` を動かす（[21章](21-CurveTimeline.md)） |
| Component の可変長の文字列（`std::string`） | 入れられない。固定長の `FixedString` を使う（理由は [ARCHITECTURE.md](../ARCHITECTURE.md) の基本設計原則 2） |
| Component の可変長の一覧（`std::vector`） | 入れられない。上限の数を決めた `FixedList<T, N>` を使う（Inspector・保存は `ui.ListField`。Script のメンバなら `std::vector` でよい） |
| ビルドの高速化（PCH） | 使っていない。先に「複数プロセッサによるコンパイル（/MP）」がオンになっていることを確かめる |
