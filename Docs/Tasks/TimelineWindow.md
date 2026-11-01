参考：
- 画面の先例：Unity Timeline：https://docs.unity3d.com/Packages/com.unity.timeline@1.8/manual/index.html
- 目標にしている UI：自作ツール Titan とベクターUIで作った IDOLY PRIDE の UI アニメーションの実装：https://speakerdeck.com/qualiarts/zi-zuo-turutitanto-bekutauiwoshi-tuta-idoly-prideno-uianimesiyonnoshi-zhuang
- ImGui の ID の仕組み：https://github.com/ocornut/imgui/blob/master/docs/FAQ.md#q-about-the-id-stack-system
- `std::rotate`：https://en.cppreference.com/w/cpp/algorithm/rotate
- `std::ranges::unique`：https://en.cppreference.com/w/cpp/algorithm/ranges/unique
- `std::exchange`：https://en.cppreference.com/w/cpp/utility/exchange

# UI 段階4：Timeline ウィンドウ

コードを書かずに `.timeline` を作って直せるウィンドウを作る。Unity の Timeline と同じ考えで、時間の目盛りの上にトラックとクリップとキーを並べ、再生位置をつまむと停止中でも画面に結果が出る。

パートは **A → B の順に写す**（B は A の物を使うので、順番は変えられない）。A の後でも Debug のビルドと自動の確認が通ることを確かめた。C は確認の手順。

**Timeline.md のパートD は写さなくてよい。** 仮の Inspector は、このパートB の物に置き換えた（Create → Timeline もここに入っている）。

作業の前に、develop からブランチを切る。

```bash
git switch -c feature/TimelineWindow develop
```

## 0章 全体の形

### 0.1 画面

```
┌ Timeline ───────────────────────────────────────────────┐
│ ▶ ⏸ ■ Loop Preview  時間  長さ  スナップ  Fit  ファイル / Entity │
│ Add Track   │ 0   0.2   0.4   0.6 （つまむと再生位置が動く）      │
│ Title.color M│    ◆━━━◆━━━━◆                                 │
│ Track       M│         ◆（イベント）                            │
│              │ ←（横のスクロールバー）→                         │
│ ──────────（つまんで高さを変える）────────────────────── │
│ 結び付け │ クリップ・キーの値 │ カーブの絵 │ キーの数値 │
└─────────────────────────────────────────────────────────┘
```

### 0.2 作るファイル

| ファイル | 中身 |
|---|---|
| `Timeline/TimelineTimeView.h` | 時間 ↔ ピクセル、拡大縮小、スナップの計算（ImGui を知らない） |
| `Timeline/TimelineEdit.h`・`.cpp` | トラック・キーを直す操作（ImGui を知らない） |
| `Editor/Timeline/TimelineWindow.h`・`.cpp` | ウィンドウ全体・上の並び・プレビューの要求・Undo |
| `Editor/Timeline/TimelineEditorState.h`・`.cpp` | 部品が一緒に使う状態 |
| `Editor/Timeline/TimelineRows.cpp` | 目盛り・トラックの一覧・時間の列・スクロールバー |
| `Editor/Timeline/TimelineDetails.cpp` | 下の詳細（結び付け・クリップ・キー・カーブ） |
| `Editor/Inspector/TimelineAssetEditor.h`・`.cpp` | `.timeline` を Inspector に出す係（様子と開くボタンだけ） |

`Editor/Windows` は 19 ファイルあるので、`Editor/Timeline` を新しく作った。

### 0.3 決めたこと

| 項目 | 決めたこと | 理由 |
|---|---|---|
| プレビューの値 | 書くのは `TimelineSystem::Update` から `EngineSystems::Draw` の終わりまで。毎フレーム書いて毎フレーム戻す | 保存・Undo・Play の退避はフレームの頭、Inspector はその後に動くので、誰もプレビューの値を見ない（1.1） |
| 戻し方 | SetValue の前に Component を丸ごと型のまま写す | 値で戻すと、回転の度とクォータニオンの変換でずれが毎フレームたまる（1.2） |
| プレビューの要求 | ウィンドウが毎フレーム頼む。頼まなければ何も書かない | 切り忘れが起きない。ウィンドウが隠れる・Entity が消える・Play を押すと、それだけで元の値のまま |
| Inspector | プレビュー中もシーンの本当の値を出す | ここで打った値がそのまま保存される。プレビューの値を打ち換えて混ざることがない |
| イベント | プレビューでは呼ばない | 停止中は Script が動いていない |
| プレビューの時間 | `ImGui::GetIO().DeltaTime` × Component の Speed | 停止中は Time Mode の時間が要らない |
| Undo | 操作の前後の JSON の文字を `EditorHistory::Record` に積む | トラックは `unique_ptr` で写せない。文字なら全部の種類を同じに扱える |
| 保存 | 操作が終わるたびにファイルへ書く | `.curve` と同じ。1つ前は `.bak` |
| `.timeline` を直す場所 | Timeline ウィンドウだけ。Inspector は様子と開くボタン | Undo の入口を1つにする |
| クリップの重なり | 止めない。後に始まるクリップが勝つ | 再生の決まり（`FindClip`）のまま。混ぜる機能は無い |
| キーの印 | 全部の成分の同じ時間のキーを1つに見せて、まとめて動かす | 列では時間だけを見る。値はカーブの絵で直す |
| 右端にキーがあるクリップ | 右端のすぐ外をつまむと長さ、内側のひし形をつまむとキー | 新しいクリップは終わりに必ずキーがある |
| 新しいクリップの値 | 対象の今の値で平らなキーを全部の成分に入れる | クリップを足した瞬間に見た目が変わらない |
| 動かさない成分 | チェックを外すとキーを全部消す | キーの無い成分は書かない決まり（UI 段階3）がそのまま使える |
| ウィンドウで開く物 | Timeline の付いた Entity を選ぶと開く。別の Entity を選んでも開いたまま | 子を選んで Inspector を見ながら直せる |
| Play 中の編集 | できる。ファイルへ書く | `.timeline` はシーンの物ではない。Stop しても残る |

### 0.4 プレビューとほかの機能の関係

| 起きたこと | 動き |
|---|---|
| Save（Ctrl+S） | フレームの頭で書くので元の値が入る。プレビューは続く |
| Play | そのフレームから要求を止める。Play の退避は元の値を写す。ウィンドウは再生中の時間を出し、ボタンは `TimelineSystem` を動かす |
| Stop | ウィンドウはプレビューを切ったまま（再生位置をつまむと、また始まる） |
| シーンを開く | 再生する Entity が見つからなくなり、プレビューを切る。ファイルは開いたまま（「再生する Entity なし」と出る） |
| UI Screen の編集に入る・出る | プレビューを切る。中身が入れ替わるので、見ていた時間の意味が変わる |
| UI Screen の編集中 | 画面の一番上の Timeline を開いてプレビューできる。Done の保存に値は混ざらない |
| Undo・Redo | 中身が変わるだけ。プレビューは Undo に積まない |
| ウィンドウを隠す | `Begin` が false で要求を出さないので、元の値のまま。戻すと続きから見える |
| 再生する Entity を消す | プレビューを切る。Undo で戻せば、番号が同じなので結び付きも戻る |
| Inspector で値を打つ | 本当の値が変わる。プレビュー中の見た目は Timeline の値が勝つ |

### 0.5 やらなかったこと（後の項目）

なくても完成の条件を満たせるので作っていない。必要になる場面と一緒に書く。

| 項目 | 必要になる場面 |
|---|---|
| 複数選択 | キーやクリップを束で動かす。必殺演出のように 20 トラックを超えると欲しくなる |
| コピー・貼り付け・複製 | 同じ動きを別のトラックへ写す。今は Timeline のファイルを写すしかない |
| 値を変えると自動でキーを打つ | Scene ビューで動かしてキーにする。今はキーの欄かカーブで打つ |
| クリップの左端の調整 | 頭を削る。キーの時間も一緒にずらす決まりが要る |
| クリップをトラックの間で動かす | 種類の違うトラックへの移動の扱いを決める必要がある |
| クリップの重なりを混ぜる | クロスフェード。再生の仕組みから作る |
| クリップの端・再生位置へのスナップ | 今は目盛りだけ |
| 再生前の値へ戻す Stop | UI 段階3 の 0 の値へ戻す決まりのまま |
| 開いたファイルを固定するボタン | 別の Timeline の付いた Entity を選ぶと切り替わる |
| 親子の名前が変わったときのパスの直し | Hierarchy で名前を変えるとトラックの行が赤くなるので、選び直す |
| 同じ名前の兄弟の区別 | パスは最初の1つを指す（UI 段階3 の決まり）。名前を変えて分ける |

## 1章 解説

### 1.1 プレビューの値を混ぜない仕組み

停止中に値を書き換えて、後で元へ戻す作りにすると、「戻す」を忘れられない場所が増える。Save の前、Play の前、シーンを開く前、Undo の前、Prefab の保存の前、ウィンドウを閉じるとき……全部に戻す処理が要り、1つ忘れると値が保存へ混ざる。

そこで、**フレームの境目をまたいで値を持ち越さない**作りにした。1フレームの順番はこうなっている。

```
WindowManager::UpdateAll
   FlushPending ──────── Undo・Redo・画面の編集に入る・出る
   SceneManager::Update ─ Play の退避・作り直し・Save・シーンを開く・「*」の比較
   EngineSystems::Update
      TimelineSystem::Update ← ここで Component を写して、プレビューの値を書く
      UpdateTransforms・カメラ・ほかの System
WindowManager::DrawAll
   シーンの Draw
   EngineSystems::Draw ─ Image・Text などが描く物を列に積む
      TimelineSystem::FinishPreview ← ここで全部元へ戻す
WindowManager::PreRenderAll
   ImGuiManager::Begin ─ Inspector・Hierarchy・Timeline ウィンドウ（元の値を見る）
   列に積んだ物を GPU へ描く
```

値が書き換わっているのは `TimelineSystem::Update` から `EngineSystems::Draw` の終わりまでだけで、ここでは保存も Undo も ImGui も動かない。列に積んだ物には値が写っているので、描くのはプレビューの値になる。

`FinishPreview` の最後で `UpdateTransforms` をもう一度呼ぶのは、プレビューで動いた物の**子**のワールド行列が、写した Component に入っていないから。これを呼ばないと、Scene ビューのギズモがプレビューの位置を読む。

要求はウィンドウが毎フレーム出す。`RequestPreview` を呼ばないフレームは何も書かないので、止める処理そのものが要らない。

### 1.2 値ではなく Component を写す理由

`SetValue` は「項目の名前」で書く。戻すのも同じ `SetValue` でやれそうに見えるが、Transform の回転はうまくいかない。Describe 関数は回転を度で見せていて、中身はクォータニオンなので、読む → 書くを通すと少しだけ値が変わる。これを毎フレーム繰り返すと、ずれがたまって保存にも出る。

そこで `ComponentSerializer::Register<T>` に、**型のまま写す関数**を足した。`T` の写しをラムダ式に持たせて、戻すときに `*now = copy` と書くので、1ビットも変わらない（自己テストで 1000 回繰り返して確かめている）。

```cpp
[](Handle<Entity> entity) -> RestoreFunction {
	const T* component = EntityManager::Get<T>(entity);
	if (component == nullptr) {
		return {}; // 持っていない
	}
	return [entity, copy = *component] {
		if (T* now = EntityManager::Get<T>(entity)) {
			*now = copy;
		}
	};
}
```

Script は型が消えているので `T` が使えない。Script の項目は Describe 関数の値そのものなので、JSON に書いて読み戻す（`CaptureAsJson`）。

写すのは `SetValue` の中なので、`SetValue` で書くトラックは全部、何もしなくても元へ戻る。`SetValue` を通らずに書くと戻せないので、プレビューに出るのは `CanPreview` が true のトラックだけにした（既定は false）。

### 1.3 Undo と保存

`Timeline` は `unique_ptr` のトラックを持つので写せない。そこで、最後に積んだときの中身を JSON の文字で持っておき（`committedText`）、操作が終わったら今の文字と比べる。

```
直す → MarkEdited（版を1つ増やす。プレビューは新しい中身で動く）
   ↓ どの項目も操作していない（マウスを離した・打ち終わった）
CommitEdit：今の文字 ≠ committedText なら、保存して EditorHistory::Record（前の文字・後の文字）
```

Undo は `TimelineManager::ApplyText` で文字から作り直す。トラックのアドレスが全部変わるので、ウィンドウはアドレスを覚えず、番号で選択を持つ。版が自分の知らない値に増えていたら Undo が走ったと分かるので、`SyncRevision` で選択を確かめ直す。

`ComponentUI` の `Field` は「変えたか」を返さない。Inspector の Component は前後のバイト列を比べているが、毎フレーム JSON を作ると重いので、ImGui の `ActiveIdHasBeenEditedThisFrame`（今操作している項目を、このフレームに変えた）を見る。`imgui_internal.h` の値だが、ImGui 自身がこの用途に使っている。

### 1.4 使い方

1. Project の Create → Timeline でファイルを作る。
2. 動かしたい物の親の Entity に Timeline Component を付けて、ファイルを選ぶ。
3. その Entity を Hierarchy で選ぶと、Timeline ウィンドウに出る（View に項目は無い。ウィンドウは最初から開いているので、タブを好きな場所へドッキングする）。
4. Add Track → `TimelineValueTrack`。下の欄で Target・Component・Property を選ぶ。Hierarchy の Entity をトラックの行へドラッグしても Target を選べる。
5. 列の何も無い所をダブルクリックしてクリップを足す。
6. 右の端のひし形をクリックして、キーの値を打つ。動かさない成分はチェックを外す。
7. クリップをダブルクリックするとキーが増える。形はカーブの絵で直す。
8. 目盛りをつまむと、画面に結果が出る。▶ でプレビューを再生する。

| 操作 | 場所 | 内容 |
|---|---|---|
| ホイール | 列 | 拡大縮小（マウスの下の時間は動かない） |
| Shift + ホイール・スクロールバー・中ボタン | 列 | 横に動かす |
| Ctrl + ホイール・中ボタン | 列 | 縦に動かす（一覧の上のホイールでも動く） |
| F・Fit | 列 | 全体を見る |
| ダブルクリック | 列 | 何も無い所：クリップを足す / クリップの上：キーを足す |
| つまむ | 列 | クリップ・右端・キー・イベントを動かす |
| Ctrl を押しながらつまむ | 列・目盛り | スナップしない |
| Delete | 一覧・列の上 | 選んでいるキー → クリップ → トラックの順に、一番細かい物を消す |
| 右クリック | 一覧・列 | メニュー（足す・消す・並べ替え） |
| つまんで運ぶ | 一覧 | トラックを並べ替える |
| M | 一覧 | Mute |

結び付けに問題があると、トラックの名前が赤くなる。マウスを乗せると、対象・Component・項目のどれが無いかが出る。

### 1.5 クリップの型を知らずに直す

ウィンドウはゲーム側のトラックも直せる必要がある。クリップの型はトラックごとに違うので、`TimelineTrack` に「番号で直す」仮想関数を足して、`TimelineTrackOf<Clip>` が全部書く形にした。

ゲーム側のトラックは何も書かなくても、クリップの追加・移動・長さ・`Fields` の項目がウィンドウで直せる。さらに2つ足すと、キーとプレビューも使える。

```cpp
class FlashTrack : public TimelineTrackOf<FlashClip> {
public:
	void Evaluate(const TimelineFrame& frame, TimelineTrackState& state) const override { ... }
	// カーブを持っていれば、列にキーが出てカーブの絵で直せる
	std::span<Curve> GetClipCurves(size_t index) override { return clips[index].curves; }
	// Evaluate が値を SetValue だけで書くなら、プレビューに出せる
	bool CanPreview() const override { return true; }
};
```

イベントのように長さの無いクリップは、クリップの型に `static constexpr bool kHasDuration = false;` と書く。

### 1.6 ImGui の ID の分け方

ImGui は項目をラベルの文字と `PushID` の積み重ねで区別する。同じ ID の項目があると、片方が反応しない。今回の決まりは3つ。

- **トラックの行：** `PushID(番号)`。トラックは同じ名前でもよいので、名前を ID にしない。
- **対象の一覧：** 同じ名前の兄弟があるとパスが同じになるので、数えた番号で `PushID`。
- **下の詳細：** トラックとクリップの両方に `Name###name` があり、色の成分の `a` とキーの値の `a` も同じになる。`PushID("track")`・`PushID("clip")`・`PushID("channels")`・`PushID("keyValues")` で分ける。

実際に、成分の `a` とキーの値の `a` は作っている途中で衝突した。ImGui は衝突した項目にマウスが乗ると赤い枠と注意を出す（`io.ConfigDebugHighlightIdConflicts`。既定で true）。乗った項目しか調べないので、確認ではウィンドウ全体をマウスでなぞった。

列はひし形やクリップごとにボタンを作らず、大きなボタン1つで受けて、マウスの位置から相手を探す（`FindHit`）。クリップがいくつあっても ID が増えない。

### 1.7 あまり使われない STL

- **`std::rotate(first, middle, last)`：** `[first, last)` を回して、`middle` の要素を先頭にする。`vector` の1つの要素を別の場所へ動かすのに使う（`unique_ptr` でもよい。ムーブだけで動く）。
- **`std::ranges::unique(範囲, 比べ方)`：** 隣り合う同じ値を後ろへ寄せて、寄せた範囲を返す。`erase` と組み合わせて消す。並べてから使う。
- **`std::exchange(変数, 新しい値)`：** 変数に新しい値を入れて、前の値を返す。「読んだら空にする」を1行で書ける（プレビューの要求を1フレームだけ使う）。
- **`std::optional::value_or(値)`：** 値があればそれ、無ければ引数を返す。

## パートA エンジンの土台

ImGui を使わない部分。ここまでで Debug のビルドと `TimelineSelfTest` が通る。

### A-1 `Math/MathUtility.h`

include に足す。

```cpp
#include <cmath>
#include <numbers>
```

定数の後ろに足す（`CurveEditor` の中の関数をここへ移す）。

```cpp
inline constexpr float kRadToDeg = 180.0f / kPi;         // ラジアン → 度（std::atan2 の答えを度にするとき）

// ===== 目盛り =====

// 目盛りの間隔。1・2・5 × 10 の n 乗 の中で、minStep 以上の一番小さい値（切りの良い数字が並ぶ）
inline float CalcNiceStep(float minStep) {
	const float base = std::pow(10.0f, std::floor(std::log10(minStep)));
	for (const float multiplier : {1.0f, 2.0f, 5.0f}) {
		if (base * multiplier >= minStep) {
			return base * multiplier;
		}
	}
	return base * 10.0f;
}
```

### A-2 `Editor/Widgets/CurveEditor.cpp`

include に足す。

```cpp
#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/Math/MathUtility.h"
```

無名名前空間の `CalcGridStep`（見出しのコメントごと）を消して、`DrawGridLines` の1行を直す。

```cpp
	const float step = MathUtility::CalcNiceStep(kGridMinPixels * (max - min) / pixels);
```

### A-3 `ECS/ComponentSerializer.h`

`ComponentValueResult` の下に足す。

```cpp
/// <summary>
/// 値を読み書きできる項目1つ（GetValueFields）
/// </summary>
struct ComponentValueField {
	std::string key;         // 保存の名前（SetValue・GetValue に渡す）
	std::string label;       // 表示名
	size_t channelCount = 1; // 成分の数（float は 1、Vector3 は 3）
	bool isColor = false;    // 色（成分は r, g, b, a の順）
};
```

`Register` の `RegisterErased` の最後の引数の後ろに足す。

```cpp
			    describe(ui, *component);
			    return true;
		    },
		    // --- 写す：本物の値を型のまま写して、戻す関数を返す（プレビュー。Transform の回転はクォータニオンなので、見せ方の関数を通すとずれる）---
		    [](Handle<Entity> entity) -> RestoreFunction {
			    const T* component = EntityManager::Get<T>(entity);
			    if (component == nullptr) {
				    return {}; // 持っていない
			    }
			    return [entity, copy = *component] {
				    if (T* now = EntityManager::Get<T>(entity)) {
					    *now = copy;
				    }
			    };
		    });
	}
```

`SetValue` の下に足す。

```cpp
	/// <summary>
	/// Component の1つの項目の今の値を読む（SetValue の逆。使う成分の isUsed が true になる）
	/// </summary>
	static ComponentValueResult GetValue(Handle<Entity> entity, std::string_view typeName, std::string_view key, ComponentValue& out);
	// Entity が持っている Component の名前（保存の順）
	static std::vector<std::string> GetComponentNames(Handle<Entity> entity);
	// Component の中の、値を読み書きできる項目の一覧（持っていなければ空）
	static std::vector<ComponentValueField> GetValueFields(Handle<Entity> entity, std::string_view typeName);

	// ===== 値を一時的に書く（Timeline のプレビュー）=====
	// これより後の SetValue は、書く前にその Component を丸ごと写す
	static void BeginValueCapture();
	// 写した Component を全部元へ戻して、写すのをやめる
	static void RestoreCapturedValues();
```

`LoadFunction` の下に足して、`RegisterErased` の宣言を直す。

```cpp
	using RestoreFunction = std::function<void()>;                         // 写した値へ戻す関数
	using CaptureFunction = std::function<RestoreFunction(Handle<Entity>)>; // 持っていなければ空の関数を返す
```

```cpp
	// visit は本物に見せ方の関数を呼ぶ（持っていなければ false。save と違い、写しに呼んではいけない）
	// capture は値を写す関数（空なら、JSON に書いて読み戻す）
	static void RegisterErased(const char* name, SaveFunction save, LoadFunction load, SaveFunction visit, CaptureFunction capture = {});
```

### A-4 `ECS/ComponentSerializer.cpp`

`Entry` に足す。

```cpp
	ComponentSerializer::CaptureFunction capture;            // 本物の値を写して、戻す関数を返す（空なら JSON で写す）
```

`ValueWriterUI` の下（無名名前空間の中）に足す。

```cpp
//=============================================================================
// 値を読む係（見せ方の関数に渡すと、数で表せる項目を全部集める）
//=============================================================================
class ValueListUI final : public ComponentUI {
public:
	/// <summary>
	/// 集めた項目1つ
	/// </summary>
	struct Item {
		ComponentValueField field; // 名前と形
		ComponentValue value;      // 今の値
	};

	const std::vector<Item>& GetItems() const { return items_; }

	void Field(const char* label, float& value, FieldStyle) override { Add(label, &value, 1, false); }
	void Field(const char* label, int& value, FieldStyle) override {
		const float number = static_cast<float>(value);
		Add(label, &number, 1, false);
	}
	void Field(const char* label, bool& value, FieldStyle) override {
		const float number = value ? 1.0f : 0.0f;
		Add(label, &number, 1, false);
	}
	void Field(const char* label, Vector2& value, FieldStyle) override { Add(label, &value.x, 2, false); }
	void Field(const char* label, Vector3& value, FieldStyle) override { Add(label, &value.x, 3, false); }
	void Field(const char* label, Vector4& value, FieldStyle) override { Add(label, &value.x, 4, false); }
	void ColorField(const char* label, Vector3& rgb) override { Add(label, &rgb.x, 3, true); }
	void ColorField(const char* label, Vector4& rgba) override { Add(label, &rgba.x, 4, true); }

	// 数で表せない項目は集めない（ValueWriterUI が書けない物と同じ）
	void Field(const char*, EntityRef&, FieldStyle) override {}
	void Field(const char*, std::string&, FieldStyle) override {}
	void AssetField(const char*, uint32_t&, AssetType, FieldStyle) override {}
	void Label(const char*) override {}
	void Separator(const char*) override {}
	void Space() override {}
	bool Button(const char*) override { return false; }

protected:
	void EnumField(const char*, size_t&, std::span<const std::string_view>, FieldStyle) override {}

private:
	void Add(const char* label, const float* values, size_t count, bool isColor) {
		const std::string_view text(label);
		Item item;
		item.field.key = KeyOf(label);
		item.field.label = std::string(text.substr(0, text.find("###")));
		item.field.channelCount = count;
		item.field.isColor = isColor;
		for (size_t i = 0; i < count; ++i) {
			item.value.channels[i] = values[i];
			item.value.isUsed[i] = true;
		}
		items_.push_back(std::move(item));
	}

	std::vector<Item> items_; // 集めた項目（見せ方の関数の順）
};

//=============================================================================
// 値を一時的に書く（Timeline のプレビュー）
//=============================================================================
/// <summary>
/// 写した Component 1つ
/// </summary>
struct CapturedComponent {
	Handle<Entity> entity;
	const Entry* entry = nullptr;
	ComponentSerializer::RestoreFunction restore; // 写した値へ戻す
};

std::vector<CapturedComponent> captured; // 写した Component（同じ物は1回だけ）
bool isCapturing = false;                // SetValue の前に写すか

// ===== JSON で写す（型が消えている Script の分）=====
ComponentSerializer::RestoreFunction CaptureAsJson(const Entry& entry, Handle<Entity> entity) {
	std::set<std::string> warnings;
	SceneJson fields = SceneJson::object();
	JsonWriterUI writer(fields, entry.name, warnings);
	if (!entry.save(entity, writer)) {
		return {}; // 持っていない
	}
	return [&entry, entity, fields = std::move(fields)] {
		std::set<std::string> ignored;
		JsonReaderUI reader(fields, entry.name, ignored);
		entry.visit(entity, reader);
	};
}

// ===== まだ写していなければ写す =====
void CaptureOnce(const Entry& entry, Handle<Entity> entity) {
	const bool isCaptured = std::ranges::any_of(captured, [&](const CapturedComponent& item) { return item.entity == entity && item.entry == &entry; });
	if (isCaptured) {
		return;
	}
	ComponentSerializer::RestoreFunction restore = entry.capture ? entry.capture(entity) : CaptureAsJson(entry, entity);
	if (restore) {
		captured.push_back({entity, &entry, std::move(restore)});
	}
}

// ===== Component の項目を全部集める（持っていなければ空）=====
std::vector<ValueListUI::Item> ListValues(Handle<Entity> entity, std::string_view typeName) {
	ValueListUI list;
	const Entry* entry = FindEntry(typeName);
	if (entry != nullptr) {
		entry->save(entity, list); // save は写しに呼ぶので、本物を変えない
	}
	return list.GetItems();
}
```

`RegisterErased` を直す。

```cpp
void ComponentSerializer::RegisterErased(const char* name, SaveFunction save, LoadFunction load, SaveFunction visit, CaptureFunction capture) {
```

```cpp
	entries.insert(position, Entry{name, std::move(save), std::move(load), std::move(visit), std::move(capture)});
```

`SetValue` の `FindEntry` の下に足す。

```cpp
	const Entry* entry = FindEntry(typeName);
	if (entry != nullptr && isCapturing) {
		CaptureOnce(*entry, entity);
	}
```

`SetValue` の下に足す。

```cpp
ComponentValueResult ComponentSerializer::GetValue(Handle<Entity> entity, std::string_view typeName, std::string_view key, ComponentValue& out) {
	const Entry* entry = FindEntry(typeName);
	ValueListUI list;
	if (entry == nullptr || !entry->save(entity, list)) {
		return ComponentValueResult::NoComponent;
	}
	const std::vector<ValueListUI::Item>& items = list.GetItems();
	const auto found = std::ranges::find_if(items, [key](const ValueListUI::Item& item) { return item.field.key == key; });
	if (found == items.end()) {
		return ComponentValueResult::NoField;
	}
	out = found->value;
	return ComponentValueResult::Written;
}

std::vector<std::string> ComponentSerializer::GetComponentNames(Handle<Entity> entity) {
	std::vector<std::string> names;
	for (const Entry& entry : Entries()) {
		ValueListUI list; // 持っているかを調べるだけ（読んだ値は使わない）
		if (entry.save(entity, list)) {
			names.push_back(entry.name);
		}
	}
	return names;
}

std::vector<ComponentValueField> ComponentSerializer::GetValueFields(Handle<Entity> entity, std::string_view typeName) {
	std::vector<ComponentValueField> fields;
	for (const ValueListUI::Item& item : ListValues(entity, typeName)) {
		fields.push_back(item.field);
	}
	return fields;
}

void ComponentSerializer::BeginValueCapture() { isCapturing = true; }

void ComponentSerializer::RestoreCapturedValues() {
	isCapturing = false;
	for (const CapturedComponent& item : captured) {
		item.restore();
	}
	captured.clear();
}
```

### A-5 `ECS/EntityManager.h`・`.cpp`

`.h` の include に `<optional>` を足して、`FindByPath` の下に足す。

```cpp
	// FindByPath の逆。root から entity までの名前のパス（entity が root なら空。root の子孫でなければ std::nullopt）
	static std::optional<std::string> MakePath(Handle<Entity> root, Handle<Entity> entity);
```

`.cpp` の `FindByPath` の下に足す。

```cpp
std::optional<std::string> EntityManager::MakePath(Handle<Entity> root, Handle<Entity> entity) {
	std::string path;
	Handle<Entity> current = entity;
	// 親へ上りながら、名前を前につなぐ
	while (IsAlive(current) && current != root) {
		const Entity* data = Get(current);
		path = path.empty() ? data->name : data->name + "/" + path;
		current = data->parent;
	}
	if (current != root || !IsAlive(root)) {
		return std::nullopt;
	}
	return path;
}
```

### A-6 `Timeline/TimelineTrack.h`

include に足す。

```cpp
#include "MyEngine/Core/Handle.h"
#include "MyEngine/Curve/Curve.h"
#include "MyEngine/ECS/ComponentSerializer.h"
```

`GetEndTime` の下に足す。

```cpp
	// ===== クリップを直す（Timeline ウィンドウ。クリップの型を知らずに直せるように、TimelineTrackOf が書いてくれる）=====
	virtual float GetClipStart(size_t index) const = 0;
	virtual float GetClipDuration(size_t index) const = 0;
	// クリップに長さがあるか（false はイベントのような1点）
	virtual bool HasClipDuration() const = 0;
	// 足す。並びが変わるので、足した場所の番号を返す
	virtual size_t AddClip(float start, float duration) = 0;
	virtual void RemoveClip(size_t index) = 0;
	// 時間を変える。並びが変わるので、新しい番号を返す
	virtual size_t SetClipTime(size_t index, float start, float duration) = 0;
	// クリップの項目を描く（Fields を呼ぶ）。時間を打ち換えると並びが変わるので、新しい番号を返す
	virtual size_t DescribeClip(size_t index, ComponentUI& ui) = 0;
	// クリップの持つカーブ（カーブを持つ種類だけ書く）。書くと Timeline ウィンドウでキーを直せる
	virtual std::span<Curve> GetClipCurves(size_t /*index*/) { return {}; }

	// プレビューで Evaluate を呼んでよいか。true にするなら、値は ComponentSerializer::SetValue で書く（書いた値を元へ戻せるのは SetValue だけ）
	virtual bool CanPreview() const { return false; }
```

`TimelineClip` の頭に足す。

```cpp
	static constexpr bool kHasDuration = true; // 長さがあるか（イベントのような1点なら、継承先で false にする）
```

`ReadClips` の2行（`clip.start = ...`・`clip.duration = ...`）を置き換える。

```cpp
			Normalize(clip);
			clips.push_back(std::move(clip));
```

`TimelineTrackOf` の `GetEndTime` の下に足す。

```cpp
	float GetClipStart(size_t index) const override { return clips[index].start; }
	float GetClipDuration(size_t index) const override { return clips[index].duration; }
	bool HasClipDuration() const override { return Clip::kHasDuration; }

	size_t AddClip(float start, float duration) override {
		Clip clip;
		clip.start = start;
		clip.duration = duration;
		return Insert(std::move(clip));
	}

	void RemoveClip(size_t index) override { clips.erase(clips.begin() + index); }

	size_t SetClipTime(size_t index, float start, float duration) override {
		Clip clip = std::move(clips[index]);
		clips.erase(clips.begin() + index);
		clip.start = start;
		clip.duration = duration;
		return Insert(std::move(clip));
	}

	size_t DescribeClip(size_t index, ComponentUI& ui) override {
		clips[index].Fields(ui);
		return SetClipTime(index, clips[index].start, clips[index].duration);
	}
```

`ForEachStarted` の下に足す。

```cpp
private:
	// 時間を 0 以上にする（比べ方を逆にしないのは、NaN もここで 0 にするため）
	static void Normalize(Clip& clip) {
		clip.start = (clip.start >= 0.0f) ? clip.start : 0.0f;
		clip.duration = (Clip::kHasDuration && clip.duration >= 0.0f) ? clip.duration : 0.0f;
	}

	// 時間の順を守って入れる（同じ時間のクリップの後ろ）
	size_t Insert(Clip clip) {
		Normalize(clip);
		const auto position = std::ranges::upper_bound(clips, clip.start, {}, &Clip::start);
		const auto inserted = clips.insert(position, std::move(clip));
		return static_cast<size_t>(inserted - clips.begin());
	}
```

### A-7 `Timeline/TimelineEventTrack.h`・`TimelineValueTrack.h`

`TimelineEventClip` の頭に足す。

```cpp
	static constexpr bool kHasDuration = false; // 1点
```

`TimelineValueTrack` の `Evaluate` の下に足す。

```cpp
	std::span<Curve> GetClipCurves(size_t index) override { return clips[index].curves; }
	bool CanPreview() const override { return true; }
```

### A-8 `Timeline/TimelineManager.h`・`.cpp`

`.h` の `GetRevision` の上に足す。

```cpp
	// 中身を JSON の文字にする（Undo の前後の値に使う。Timeline は unique_ptr を持っていて写せないので、文字で写す）
	static std::string ToText(uint32_t handle);
	// JSON の文字から中身を作り直す（ToText の逆。版も1つ増える。読めなければ何も変えず false）
	static bool ApplyText(uint32_t handle, const std::string& text);
```

`.cpp` の `GetRevision` の上に足す。

```cpp
std::string TimelineManager::ToText(uint32_t handle) {
	const Timeline* timeline = Get(handle);
	return (timeline != nullptr) ? ToJson(*timeline).dump() : std::string();
}

bool TimelineManager::ApplyText(uint32_t handle, const std::string& text) {
	TimelineAsset* asset = Find(handle);
	const SceneJson json = SceneJson::parse(text, nullptr, false); // false: 壊れていても例外を投げず、discarded を返す
	if (asset == nullptr || json.is_discarded() || !json.is_object()) {
		return false;
	}
	std::set<std::string> warnings;
	asset->timeline = FromJson(json, warnings);
	for (const std::string& warning : warnings) {
		LogManager::Warning(std::format("{}: {}", asset->path, warning));
	}
	++asset->revision;
	return true;
}
```

### A-9 `Systems/Animation/TimelineSystem.h`

「イベントを受け取る」の上に足す。

```cpp
	// ===== プレビュー（エディタの停止中に、再生せずに値を見る）=====
	/// <summary>
	/// 次の Update で、time 秒の値を仮に書く。見るのは1フレームだけなので、見続ける間は毎フレーム呼ぶ（呼ばなくなれば、それだけで元の値のまま）
	/// <para>値を書くのは Update から描画の要求を積み終わる FinishPreview までの間だけ。フレームの頭の保存・Undo と、後の ImGui のウィンドウは元の値を見る</para>
	/// </summary>
	/// <param name="owner">再生する Entity（トラックの対象はここからのパス）</param>
	/// <param name="timeline">見る Timeline の番号</param>
	/// <param name="time">見る時間（秒）</param>
	static void RequestPreview(Handle<Entity> owner, uint32_t timeline, float time);
	// 仮に書いた値を元へ戻す。描画の要求を全部積んだ後に、1フレームに1回呼ぶ
	static void FinishPreview();
```

### A-10 `Systems/Animation/TimelineSystem.cpp`

include に `<optional>`・`<utility>` を足す。`isRunning` の下に足す。

```cpp
/// <summary>
/// 頼まれたプレビュー
/// </summary>
struct PreviewRequest {
	Handle<Entity> owner;  // 再生する Entity
	uint32_t timeline = 0; // 見る Timeline
	float time = 0.0f;     // 見る時間（秒）
};
std::optional<PreviewRequest> previewRequest; // 次の Update で見る物（毎フレーム頼まれる）
TimelineRuntime previewRuntime;               // プレビューのトラックの値（毎フレーム作らないように持っておく）
bool isPreviewApplied = false;                // 値を仮に書いている（FinishPreview で戻す）
```

`SendEvents` の下に足す。

```cpp
// ===== プレビュー：今の値を写して、見る時間の値を書く =====
void ApplyPreview(const PreviewRequest& request) {
	const Timeline* timeline = TimelineManager::Get(request.timeline);
	if (timeline == nullptr || !EntityManager::IsAlive(request.owner)) {
		return;
	}
	// --- トラックの値を用意する（警告は出さない。キーを動かすたびに作り直すのでログが埋まる。問題は Timeline ウィンドウの行に出す）---
	const bool isSameRuntime = previewRuntime.entity == request.owner && previewRuntime.timeline == request.timeline && previewRuntime.revision == TimelineManager::GetRevision(request.timeline);
	if (!isSameRuntime) {
		previewRuntime.entity = request.owner;
		BuildStates(previewRuntime, request.timeline, *timeline);
		for (const std::unique_ptr<TimelineTrackState>& state : previewRuntime.states) {
			state->isWarned = true;
		}
	}
	// --- 書く（イベントは呼ばない。ranges が空なので、通った時間は無い）---
	TimelineFrame frame;
	frame.owner = request.owner;
	frame.time = request.time;
	frame.events = &pendingEvents;
	ComponentSerializer::BeginValueCapture();
	isPreviewApplied = true;
	for (size_t i = 0; i < timeline->tracks.size(); ++i) {
		const TimelineTrack& track = *timeline->tracks[i];
		if (!track.isMuted && track.CanPreview()) {
			track.Evaluate(frame, *previewRuntime.states[i]);
		}
	}
	pendingEvents.clear();
}
```

`Release` に足す。

```cpp
	previewRequest.reset();
	previewRuntime = {};
```

`Update` の頭を直す。

```cpp
	isRunning = isGameRunning;
	const std::optional<PreviewRequest> preview = std::exchange(previewRequest, std::nullopt); // 頼まれた1フレーム分だけ
	if (!isGameRunning) {
		runtimes.clear(); // 停止中は動かさない（Play を押すと、また最初から）
		if (preview.has_value()) {
			ApplyPreview(*preview);
		}
		return;
	}
```

「外から読む」の上に足す。

```cpp
//=============================================================================
// プレビュー
//=============================================================================
void TimelineSystem::RequestPreview(Handle<Entity> owner, uint32_t timeline, float time) { previewRequest = PreviewRequest{owner, timeline, time}; }

void TimelineSystem::FinishPreview() {
	if (!isPreviewApplied) {
		return;
	}
	isPreviewApplied = false;
	ComponentSerializer::RestoreCapturedValues();
	EntityManager::UpdateTransforms(); // 動かした物の子のワールド行列も元へ戻す（この後の ImGui のギズモが読む）
}
```

### A-11 `Systems/EngineSystems.cpp`

`EngineSystems::Draw` の一番下（`#endif` の後）に足す。

```cpp
	TimelineSystem::FinishPreview(); // Timeline のプレビューの値を元へ戻す（描画の要求を積んだ後。この後の ImGui と、次のフレームの頭の保存・Undo は元の値を見る）
```

### A-12 `Timeline/TimelineTimeView.h`（新しいファイル）

```cpp
#pragma once
#include <algorithm>
#include <cmath>

#include "MyEngine/Math/MathUtility.h"

/// <summary>
/// Timeline ウィンドウの見ている範囲（時間 ↔ 横のピクセル）。ImGui を知らない計算だけ
/// </summary>
struct TimelineTimeView {
	static constexpr float kMinPixelsPerSecond = 8.0f;    // 一番縮めたとき
	static constexpr float kMaxPixelsPerSecond = 4000.0f; // 一番広げたとき

	float pixelsPerSecond = 120.0f; // 1 秒の幅（ピクセル）
	float scrollTime = 0.0f;        // 左端の時間（秒）

	// 時間 → 左端からのピクセル
	float ToPixel(float time) const { return (time - scrollTime) * pixelsPerSecond; }
	// 左端からのピクセル → 時間
	float ToTime(float pixel) const { return scrollTime + pixel / pixelsPerSecond; }

	// pixel の下の時間が動かないように拡大縮小する
	void ZoomAt(float pixel, float factor) {
		const float time = ToTime(pixel);
		pixelsPerSecond = std::clamp(pixelsPerSecond * factor, kMinPixelsPerSecond, kMaxPixelsPerSecond);
		scrollTime = (std::max)(time - pixel / pixelsPerSecond, 0.0f);
	}

	// pixels の分だけ見ている範囲を動かす（左端は 0 秒まで）
	void Pan(float pixels) { scrollTime = (std::max)(scrollTime + pixels / pixelsPerSecond, 0.0f); }

	// duration 秒全部が width ピクセルに入るようにする
	void Fit(float duration, float width) {
		constexpr float kMargin = 1.1f; // 右端に少し余白を残す
		const float span = (std::max)(duration, 0.1f) * kMargin;
		pixelsPerSecond = std::clamp(width / span, kMinPixelsPerSecond, kMaxPixelsPerSecond);
		scrollTime = 0.0f;
	}

	// 目盛りの間隔（秒）。線の間が minPixels 以上あく、切りの良い値
	float CalcTickStep(float minPixels) const { return MathUtility::CalcNiceStep(minPixels / pixelsPerSecond); }
};

/// <summary>
/// 時間を目盛りへそろえる
/// </summary>
/// <param name="time">時間（秒）</param>
/// <param name="step">目盛りの間隔（秒）。0 以下ならそろえない</param>
inline float SnapTime(float time, float step) { return (step > 0.0f) ? std::round(time / step) * step : time; }
```

### A-13 `Timeline/TimelineEdit.h`（新しいファイル）

```cpp
#pragma once
#include <span>
#include <string_view>
#include <vector>

#include "MyEngine/Curve/Curve.h"
#include "MyEngine/Timeline/TimelineManager.h"

/// <summary>
/// Timeline の中身を直す操作（ImGui を知らない。Timeline ウィンドウと自己テストから使う）
/// <para>キーは成分ごとのカーブに入っているが、ウィンドウでは同じ時間のキーを1つの印に見せる。ここの関数は、その1つの印を全部の成分へまとめて行う</para>
/// </summary>
class TimelineEdit {
public:
	static constexpr float kSameKeyTime = 0.0001f;      // この差より近いキーは、同じ時間とみなす（秒）
	static constexpr float kDefaultClipDuration = 1.0f; // 新しいクリップの長さ（秒）
	static constexpr float kMinClipDuration = 0.01f;    // クリップの右端をつまんで縮められる下限（秒）

	// ===== トラック =====
	// 足す。足した番号を返す（知らない種類なら -1）
	static int AddTrack(Timeline& timeline, std::string_view typeName);
	static void RemoveTrack(Timeline& timeline, int index);
	// index のトラックを newIndex へ動かす。動かした先の番号を返す
	static int MoveTrack(Timeline& timeline, int index, int newIndex);

	// ===== クリップ =====
	// start 秒に足すクリップの長さ（次のクリップの手前まで。ぶつからなければ kDefaultClipDuration）
	static float CalcNewClipDuration(const TimelineTrack& track, float start);

	// ===== キー =====
	// 全部のカーブのキーの時間を、重ならないように小さい順に並べる
	static std::vector<float> CollectKeyTimes(std::span<const Curve> curves);
	// キーのあるカーブ全部へ、形を変えずにキーを足す
	static void AddKeys(std::span<Curve> curves, float time);
	// その時間のキーを全部消す
	static void RemoveKeys(std::span<Curve> curves, float time);
	// from の時間のキーを全部 to の時間へ動かす
	static void MoveKeys(std::span<Curve> curves, float from, float to);
	// カーブを、0 秒と duration 秒に value のキーがある平らな線にする
	static void FillFlat(Curve& curve, float value, float duration);
};
```

### A-14 `Timeline/TimelineEdit.cpp`（新しいファイル）

```cpp
#include "TimelineEdit.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

namespace {
// 同じ時間か
bool IsSameTime(float a, float b) { return std::abs(a - b) <= TimelineEdit::kSameKeyTime; }
} // namespace

//=============================================================================
// トラック
//=============================================================================
int TimelineEdit::AddTrack(Timeline& timeline, std::string_view typeName) {
	std::unique_ptr<TimelineTrack> track = TimelineTrackRegistry::Create(typeName);
	if (track == nullptr) {
		return -1;
	}
	timeline.tracks.push_back(std::move(track));
	return static_cast<int>(timeline.tracks.size()) - 1;
}

void TimelineEdit::RemoveTrack(Timeline& timeline, int index) {
	if (index >= 0 && index < static_cast<int>(timeline.tracks.size())) {
		timeline.tracks.erase(timeline.tracks.begin() + index);
	}
}

int TimelineEdit::MoveTrack(Timeline& timeline, int index, int newIndex) {
	const int count = static_cast<int>(timeline.tracks.size());
	if (index < 0 || index >= count) {
		return index;
	}
	const int destination = std::clamp(newIndex, 0, count - 1);
	// std::rotate は、範囲の中の1つを前へ持ってくる関数。動かす向きで、範囲の取り方を変える
	const auto begin = timeline.tracks.begin();
	if (destination < index) {
		std::rotate(begin + destination, begin + index, begin + index + 1);
	} else {
		std::rotate(begin + index, begin + index + 1, begin + destination + 1);
	}
	return destination;
}

//=============================================================================
// クリップ
//=============================================================================
float TimelineEdit::CalcNewClipDuration(const TimelineTrack& track, float start) {
	float duration = kDefaultClipDuration;
	for (size_t i = 0; i < track.GetClipCount(); ++i) {
		const float gap = track.GetClipStart(i) - start;
		if (gap > kMinClipDuration) {
			duration = (std::min)(duration, gap);
		}
	}
	return duration;
}

//=============================================================================
// キー
//=============================================================================
std::vector<float> TimelineEdit::CollectKeyTimes(std::span<const Curve> curves) {
	std::vector<float> times;
	for (const Curve& curve : curves) {
		for (const CurveKey& key : curve.GetKeys()) {
			times.push_back(key.time);
		}
	}
	std::ranges::sort(times);
	const auto removed = std::ranges::unique(times, IsSameTime); // 並んでいる同じ値を後ろへ寄せて、寄せた範囲を返す
	times.erase(removed.begin(), removed.end());
	return times;
}

void TimelineEdit::AddKeys(std::span<Curve> curves, float time) {
	for (Curve& curve : curves) {
		if (!curve.GetKeys().empty()) {
			curve.InsertKeyOnCurve(time);
		}
	}
}

void TimelineEdit::RemoveKeys(std::span<Curve> curves, float time) {
	for (Curve& curve : curves) {
		const std::vector<CurveKey>& keys = curve.GetKeys();
		for (int i = static_cast<int>(keys.size()) - 1; i >= 0; --i) { // 消すと後ろの番号がずれるので、後ろから
			if (IsSameTime(keys[i].time, time)) {
				curve.RemoveKey(i);
			}
		}
	}
}

void TimelineEdit::MoveKeys(std::span<Curve> curves, float from, float to) {
	for (Curve& curve : curves) {
		std::vector<CurveKey> keys = curve.GetKeys();
		for (CurveKey& key : keys) {
			if (IsSameTime(key.time, from)) {
				key.time = to;
			}
		}
		curve = Curve(std::move(keys)); // 並びとハンドルの長さは Curve が直す
	}
}

void TimelineEdit::FillFlat(Curve& curve, float value, float duration) {
	curve = Curve();
	curve.AddKey(0.0f, value);
	if (duration > kSameKeyTime) {
		curve.AddKey(duration, value);
	}
}
```

### A-15 `Timeline/TimelineSelfTest.cpp`

include に足す。

```cpp
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
```

```cpp
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/Systems/Animation/TimelineSystem.h"
#include "MyEngine/Timeline/TimelineEdit.h"
#include "MyEngine/Timeline/TimelineEventTrack.h"
#include "MyEngine/Timeline/TimelineManager.h"
#include "MyEngine/Timeline/TimelineTimeView.h"
#include "MyEngine/Timeline/TimelineValueTrack.h"
```

`MakeTestEntities` の上に足す。

```cpp
// ===== 値を読む（SetValue の逆）=====
void TestGetValue(Tester& t, Handle<Entity> root, Handle<Entity> panel, Handle<Entity> title) {
	SetChannel(title, "Image", "color", 3, 0.75f);
	ComponentValue value;
	t.Check(ComponentSerializer::GetValue(title, "Image", "color", value) == ComponentValueResult::Written, "読める");
	t.Near(value.channels[3], 0.75f, "色の a を読む");
	t.Check(value.isUsed[3], "読んだ成分に印が付く");
	ComponentValue none;
	t.Check(ComponentSerializer::GetValue(title, "Unknown", "color", none) == ComponentValueResult::NoComponent, "知らない Component");
	t.Check(ComponentSerializer::GetValue(title, "Image", "unknown", none) == ComponentValueResult::NoField, "無い項目");

	const std::vector<std::string> names = ComponentSerializer::GetComponentNames(title);
	t.Check(std::ranges::find(names, std::string("Image")) != names.end() && std::ranges::find(names, std::string("Transform")) != names.end(), "持っている Component の名前");
	t.Check(std::ranges::find(names, std::string("Text")) == names.end(), "持っていない Component は出ない");

	const std::vector<ComponentValueField> fields = ComponentSerializer::GetValueFields(title, "Image");
	const auto color = std::ranges::find(fields, std::string("color"), &ComponentValueField::key);
	t.Check(color != fields.end() && color->channelCount == 4 && color->isColor, "色の項目は 4 成分の色");
	t.Check(ComponentSerializer::GetValueFields(title, "Text").empty(), "持っていない Component の項目は空");

	t.Check(EntityManager::MakePath(root, title) == "Panel/Title", "孫までのパス");
	t.Check(EntityManager::MakePath(panel, title) == "Title", "子までのパス");
	t.Check(EntityManager::MakePath(root, root) == "", "自分は空のパス");
	t.Check(!EntityManager::MakePath(title, root).has_value(), "子孫でなければ作らない");
	t.Check(EntityManager::FindByPath(root, *EntityManager::MakePath(root, title)) == title, "FindByPath で同じ Entity に戻る");
}

// ===== プレビュー：書いた値が1ビットも変わらず戻る =====
void TestCapture(Tester& t, Handle<Entity> title) {
	// 度とクォータニオンを行き来するとずれる回転を入れておく
	SetChannel(title, "Transform", "rotation", 0, 33.3f);
	SetChannel(title, "Transform", "rotation", 1, 71.7f);
	SetChannel(title, "Transform", "rotation", 2, -123.4f);
	SetChannel(title, "Image", "color", 3, 1.0f);
	const TransformComponent transformBefore = *EntityManager::Get<TransformComponent>(title);
	bool isSame = true;
	for (int i = 0; i < 1000; ++i) { // 毎フレーム繰り返すので、たまったずれも見る
		ComponentSerializer::BeginValueCapture();
		SetChannel(title, "Transform", "rotation", 1, 10.0f);
		SetChannel(title, "Transform", "translation", 0, 5.0f);
		SetChannel(title, "Image", "color", 3, 0.25f);
		isSame = isSame && EntityManager::Get<ImageComponent>(title)->color.w == 0.25f; // 書いている間は書いた値
		ComponentSerializer::RestoreCapturedValues();
		const TransformComponent& now = *EntityManager::Get<TransformComponent>(title);
		isSame = isSame && std::memcmp(&now.rotation, &transformBefore.rotation, sizeof(now.rotation)) == 0 && now.translation.x == transformBefore.translation.x;
		isSame = isSame && EntityManager::Get<ImageComponent>(title)->color.w == 1.0f;
	}
	t.Check(isSame, "1000 回書いて戻しても、1ビットも変わらない");
	SetChannel(title, "Image", "color", 3, 0.5f);
	t.Near(EntityManager::Get<ImageComponent>(title)->color.w, 0.5f, "戻した後の SetValue はそのまま残る");
	SetChannel(title, "Image", "color", 3, 1.0f);
}

// ===== プレビュー：TimelineSystem を通す =====
void TestPreview(Tester& t, Handle<Entity> root, Handle<Entity> title) {
	const std::filesystem::path path = std::filesystem::temp_directory_path() / "MyEngineTimelineSelfTest.timeline";
	std::filesystem::remove(path);
	t.Check(TimelineManager::CreateNewFile(path), "確認用のファイルを作る");
	const uint32_t handle = TimelineManager::Load(path);
	TimelineManager::ApplyText(handle, "{}"); // 読み込み済みの中身が残っていても、空から始める
	Timeline* timeline = TimelineManager::GetForEdit(handle);
	auto* track = static_cast<TimelineValueTrack*>(timeline->tracks[TimelineEdit::AddTrack(*timeline, "TimelineValueTrack")].get());
	track->target = "Panel/Title";
	track->component = "Image";
	track->property = "color";
	const size_t clip = track->AddClip(0.0f, 1.0f);
	TimelineEdit::FillFlat(track->GetClipCurves(clip)[3], 0.25f, 1.0f);
	TimelineManager::MarkChanged(handle);

	const auto alpha = [title] { return EntityManager::Get<ImageComponent>(title)->color.w; };
	TimelineSystem::RequestPreview(root, handle, 0.5f);
	TimelineSystem::Update(false);
	t.Near(alpha(), 0.25f, "Update の後はプレビューの値");
	TimelineSystem::FinishPreview();
	t.Near(alpha(), 1.0f, "FinishPreview で元の値");

	TimelineSystem::Update(false);
	t.Near(alpha(), 1.0f, "頼まなければ書かない（1回の要求は1フレームだけ）");
	TimelineSystem::FinishPreview();

	track->isMuted = true;
	TimelineSystem::RequestPreview(root, handle, 0.5f);
	TimelineSystem::Update(false);
	t.Near(alpha(), 1.0f, "Mute のトラックは書かない");
	TimelineSystem::FinishPreview();
	track->isMuted = false;

	TimelineSystem::RequestPreview(root, handle, 0.5f);
	TimelineSystem::Update(true);
	t.Near(alpha(), 1.0f, "Play 中はプレビューしない");
	TimelineSystem::Update(false);

	TimelineSystem::RequestPreview({}, handle, 0.5f);
	TimelineSystem::Update(false);
	TimelineSystem::FinishPreview();
	t.Near(alpha(), 1.0f, "無効な Entity のプレビューは何もしない");

	std::filesystem::remove(path);
}

// ===== トラックとクリップを直す =====
void TestEditTracks(Tester& t) {
	Timeline timeline;
	t.Check(TimelineEdit::AddTrack(timeline, "NotRegistered") == -1, "知らない種類は足さない");
	for (const char* name : {"a", "b", "c"}) {
		timeline.tracks[TimelineEdit::AddTrack(timeline, "TimelineEventTrack")]->name = name;
	}
	const auto order = [&timeline] {
		std::string text;
		for (const std::unique_ptr<TimelineTrack>& track : timeline.tracks) {
			text += track->name;
		}
		return text;
	};
	t.Check(TimelineEdit::MoveTrack(timeline, 0, 2) == 2 && order() == "bca", "後ろへ動かす");
	t.Check(TimelineEdit::MoveTrack(timeline, 2, 0) == 0 && order() == "abc", "前へ動かす");
	t.Check(TimelineEdit::MoveTrack(timeline, 1, 99) == 2 && order() == "acb", "範囲の外は端へ");
	TimelineEdit::RemoveTrack(timeline, 1);
	t.Check(order() == "ab", "消す");
	TimelineEdit::RemoveTrack(timeline, 9);
	t.Check(order() == "ab", "知らない番号は何もしない");

	TimelineValueTrack track;
	t.Check(track.AddClip(2.0f, 1.0f) == 0, "最初のクリップ");
	t.Check(track.AddClip(1.0f, 0.5f) == 0, "前に足すと 0 番");
	t.Check(track.AddClip(3.0f, 1.0f) == 2, "後ろに足す");
	t.Check(track.SetClipTime(0, 5.0f, 2.0f) == 2 && track.GetClipStart(2) == 5.0f && track.GetClipDuration(2) == 2.0f, "追い越すと番号が変わる");
	t.Check(track.GetClipStart(0) == 2.0f && track.GetClipStart(1) == 3.0f, "ほかのクリップは時間の順のまま");
	t.Check(track.SetClipTime(0, -3.0f, kNaN) == 0 && track.GetClipStart(0) == 0.0f && track.GetClipDuration(0) == 0.0f, "負の時間と NaN は 0");
	track.RemoveClip(0);
	t.Check(track.GetClipCount() == 2, "クリップを消す");
	t.Check(track.HasClipDuration() && !track.GetClipCurves(0).empty(), "値のクリップは長さとカーブがある");

	TimelineEventTrack events;
	t.Check(!events.HasClipDuration(), "イベントは1点");
	t.Check(events.AddClip(1.0f, 5.0f) == 0 && events.GetClipDuration(0) == 0.0f, "イベントの長さは 0 のまま");
	t.Check(events.GetClipCurves(0).empty(), "イベントにカーブは無い");
	t.Check(!events.CanPreview() && track.CanPreview(), "プレビューするのは値のトラックだけ");

	TimelineValueTrack gaps;
	gaps.AddClip(2.0f, 1.0f);
	t.Near(TimelineEdit::CalcNewClipDuration(gaps, 1.5f), 0.5f, "次のクリップの手前まで");
	t.Near(TimelineEdit::CalcNewClipDuration(gaps, 0.0f), TimelineEdit::kDefaultClipDuration, "離れていれば既定の長さ");
	t.Near(TimelineEdit::CalcNewClipDuration(gaps, 3.0f), TimelineEdit::kDefaultClipDuration, "後ろのクリップが無ければ既定の長さ");
}

// ===== キーを直す =====
void TestEditKeys(Tester& t) {
	std::array<Curve, 2> curves;
	TimelineEdit::FillFlat(curves[0], 2.0f, 1.0f);
	t.Check(curves[0].GetKeys().size() == 2 && curves[0].GetKeys()[1].time == 1.0f && curves[0].GetKeys()[1].value == 2.0f, "平らな線は2つのキー");
	Curve single;
	TimelineEdit::FillFlat(single, 3.0f, 0.0f);
	t.Check(single.GetKeys().size() == 1, "長さ 0 ならキーは1つ");

	curves[1].AddKey(0.5f, 7.0f);
	const std::vector<float> times = TimelineEdit::CollectKeyTimes(curves);
	t.Check(times.size() == 3 && times[1] == 0.5f, "別の成分のキーも時間の順に集める");
	curves[1].AddKey(1.0f + TimelineEdit::kSameKeyTime * 0.5f, 7.0f);
	t.Check(TimelineEdit::CollectKeyTimes(curves).size() == 3, "ほとんど同じ時間のキーは1つに見せる");

	Curve shape;
	shape.AddKey(0.0f, 0.0f);
	shape.AddKey(1.0f, 1.0f);
	const float middle = shape.Evaluate(0.3f);
	TimelineEdit::AddKeys(std::span(&shape, 1), 0.5f);
	t.Check(shape.GetKeys().size() == 3, "キーが増える");
	t.Near(shape.Evaluate(0.3f), middle, "足しても形は変わらない");
	Curve empty;
	TimelineEdit::AddKeys(std::span(&empty, 1), 0.5f);
	t.Check(empty.GetKeys().empty(), "キーの無い成分には足さない");

	TimelineEdit::MoveKeys(curves, 1.0f, 0.75f);
	t.Check(curves[0].GetKeys()[1].time == 0.75f, "1つの成分のキーを動かす");
	t.Check(curves[1].GetKeys()[1].time == 0.75f, "同じ時間の別の成分のキーも動く");
	TimelineEdit::MoveKeys(curves, 0.75f, 0.25f);
	t.Check(curves[1].GetKeys()[0].time == 0.25f && curves[1].GetKeys()[1].time == 0.5f, "追い越すと並びが直る");

	TimelineEdit::RemoveKeys(curves, 0.25f);
	t.Check(curves[0].GetKeys().size() == 1 && curves[1].GetKeys().size() == 1, "同じ時間のキーを全部の成分で消す");
}

// ===== Undo の文字 =====
void TestEditText(Tester& t) {
	const std::filesystem::path path = std::filesystem::temp_directory_path() / "MyEngineTimelineSelfTest2.timeline";
	std::filesystem::remove(path);
	TimelineManager::CreateNewFile(path);
	const uint32_t handle = TimelineManager::Load(path);
	TimelineManager::ApplyText(handle, "{}"); // 読み込み済みの中身が残っていても、空から始める
	Timeline* timeline = TimelineManager::GetForEdit(handle);
	const std::string empty = TimelineManager::ToText(handle);

	const int index = TimelineEdit::AddTrack(*timeline, "TimelineValueTrack");
	TimelineTrack& track = *timeline->tracks[index];
	track.name = "Edited";
	const size_t clip = track.AddClip(0.5f, 2.0f);
	TimelineEdit::FillFlat(track.GetClipCurves(clip)[1], 3.0f, 2.0f);
	const std::string edited = TimelineManager::ToText(handle);
	t.Check(edited != empty, "直すと文字が変わる");

	const uint32_t revision = TimelineManager::GetRevision(handle);
	t.Check(TimelineManager::ApplyText(handle, empty), "前の文字へ戻す");
	t.Check(TimelineManager::GetRevision(handle) == revision + 1, "戻すと版が増える");
	t.Check(TimelineManager::Get(handle)->tracks.empty() && TimelineManager::ToText(handle) == empty, "戻した中身");
	t.Check(TimelineManager::ApplyText(handle, edited), "やり直す");
	t.Check(TimelineManager::ToText(handle) == edited, "文字で往復しても同じ");
	const TimelineTrack& restored = *TimelineManager::Get(handle)->tracks[0];
	t.Check(restored.name == "Edited" && restored.GetClipCount() == 1, "トラックとクリップが戻る");

	t.Check(!TimelineManager::ApplyText(handle, "{ broken"), "壊れた文字は読まない");
	t.Check(TimelineManager::ToText(handle) == edited, "読めなければ中身は変わらない");
	t.Check(!TimelineManager::ApplyText(0, empty), "知らない番号は読まない");
	std::filesystem::remove(path);
}

// ===== 見ている時間の範囲 =====
void TestTimeView(Tester& t) {
	TimelineTimeView view;
	view.pixelsPerSecond = 100.0f;
	view.scrollTime = 2.0f;
	t.Near(view.ToPixel(3.0f), 100.0f, "時間 → ピクセル");
	t.Near(view.ToTime(100.0f), 3.0f, "ピクセル → 時間");
	view.ZoomAt(100.0f, 2.0f);
	t.Near(view.pixelsPerSecond, 200.0f, "拡大する");
	t.Near(view.ToTime(100.0f), 3.0f, "マウスの下の時間は動かない");
	view.ZoomAt(0.0f, 1000000.0f);
	t.Check(view.pixelsPerSecond == TimelineTimeView::kMaxPixelsPerSecond, "拡大の上限");
	view.ZoomAt(0.0f, 0.000001f);
	t.Check(view.pixelsPerSecond == TimelineTimeView::kMinPixelsPerSecond, "縮小の下限");
	view.scrollTime = 1.0f;
	view.ZoomAt(100000.0f, 2.0f);
	t.Check(view.scrollTime >= 0.0f, "左端は 0 秒より前へ行かない");
	view.Pan(-1000000000.0f);
	t.Check(view.scrollTime == 0.0f, "左へ動かしても 0 秒まで");
	view.Fit(10.0f, 1100.0f);
	t.Near(view.pixelsPerSecond, 100.0f, "全体が入る");
	t.Check(view.scrollTime == 0.0f, "全体を見ると左端は 0 秒");
	view.pixelsPerSecond = 100.0f;
	t.Near(view.CalcTickStep(60.0f), 1.0f, "60 ピクセル以上の切りの良い間隔");
	view.pixelsPerSecond = 400.0f;
	t.Near(view.CalcTickStep(60.0f), 0.2f, "広げると間隔が細かくなる");

	t.Near(SnapTime(0.26f, 0.1f), 0.3f, "目盛りへそろえる");
	t.Near(SnapTime(0.24f, 0.1f), 0.2f, "近い方へそろえる");
	t.Near(SnapTime(1.0f / 30.0f * 7.4f, 1.0f / 30.0f), 7.0f / 30.0f, "1/30 秒へそろえる");
	t.Check(SnapTime(0.26f, 0.0f) == 0.26f, "0 ならそろえない");
}
```

`Run` の `TestJson(tester);` の下に足す。

```cpp
	TestGetValue(tester, rootA, panelA, titleA);
	TestCapture(tester, titleA);
	TestPreview(tester, rootA, titleA);
	TestEditTracks(tester);
	TestEditKeys(tester);
	TestEditText(tester);
	TestTimeView(tester);
```

### A-16 プロジェクトに足す

`Timeline/TimelineEdit.cpp`・`.h`、`Timeline/TimelineTimeView.h` を `MyEngine_v1` に足す。

ここで Debug をビルドして起動すると、ログに `[timeline] self test OK (162 / 162)` が出る。

## パートB Timeline ウィンドウ

### B-1 `Editor/Timeline/TimelineWindow.h`（新しいファイル）

```cpp
#pragma once
#include <filesystem>

/// <summary>
/// Timeline ウィンドウ（.timeline のファイルを時間の表で直す。Unity の Timeline と同じ考え）
/// <para>開く：Hierarchy で Timeline Component の付いた Entity を選ぶ（プレビューできる）/ Project で .timeline をダブルクリック（同じファイルを再生する Entity があれば、それでプレビューできる）</para>
/// <para>直した物は、操作が終わるたびにファイルへ書いて Undo に積む（.curve と同じ）</para>
/// <para>プレビュー：停止中に再生位置を動かすと、その時間の値を画面に出す。値を書くのは描画の要求を積む間だけ（TimelineSystem::RequestPreview）。Inspector・保存・Undo は元の値を見る</para>
/// </summary>
class TimelineWindow {
public:
	// 毎フレーム1回（ImGuiManager::Begin から）
	static void Draw();

	// ファイルを開いて、ウィンドウを前に出す（Project のダブルクリック・Inspector のボタンから）
	static void Open(const std::filesystem::path& filePath);
};
```

### B-2 `Editor/Timeline/TimelineEditorState.h`（新しいファイル）

```cpp
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "MyEngine/Core/Handle.h"
#include "MyEngine/ECS/Entity.h"
#include "MyEngine/Editor/Widgets/CurveEditor.h"
#include "MyEngine/Timeline/TimelineManager.h"
#include "MyEngine/Timeline/TimelinePlayhead.h"
#include "MyEngine/Timeline/TimelineTimeView.h"

// Timeline ウィンドウの部品（TimelineWindow・TimelineRows・TimelineDetails）が一緒に使う物。ほかからはインクルードしない

constexpr float kTimelineListWidth = 230.0f; // 左のトラックの一覧の幅（ピクセル）
constexpr float kTimelineRowHeight = 30.0f;  // トラック1行の高さ
constexpr float kTimelineScrollbarHeight = 12.0f; // 横のスクロールバーの高さ

/// <summary>
/// 選んでいる物（複数選択は作っていない）
/// </summary>
struct TimelineSelection {
	int track = -1;       // トラックの番号（無ければ -1）
	int clip = -1;        // クリップの番号（無ければ -1）
	bool hasKey = false;  // キーの印を選んでいる
	float keyTime = 0.0f; // 選んでいるキーの時間（クリップの始まりからの秒）
	int channel = 0;      // カーブの絵に出す成分
};

/// <summary>
/// 列の上でつまんでいる物
/// </summary>
enum class TimelineDrag {
	None,
	Clip,    // クリップを動かす
	ClipEnd, // クリップの右端で長さを変える
	Key,     // キーの印を動かす
};

/// <summary>
/// Timeline ウィンドウの状態（1つのファイルだけ開ける）
/// </summary>
struct TimelineEditorState {
	// ===== 開いている物 =====
	uint32_t handle = 0;  // 開いているファイル（0 なら開いていない）
	EntityId ownerId = 0; // 再生する Entity。Play・Stop で作り直されると Handle は変わるが、番号は変わらない
	Handle<Entity> owner; // ownerId から毎フレーム引き直す（居なければ無効。プレビューできない）

	// ===== 見え方 =====
	TimelineTimeView view;  // 見ている時間の範囲
	float laneWidth = 1.0f; // 右の時間の列の幅（ピクセル）
	float snapStep = 0.1f;  // 目盛りの間隔（秒。0 ならスナップしない）
	bool needsFit = true;   // 次に描くとき、全体が見える範囲にする

	// ===== 再生位置 =====
	bool isGameRunning = false; // エディタの Play 中か（ウィンドウのボタンは TimelineSystem を動かす）
	TimelinePlayhead playhead;  // 停止中のプレビューの再生位置
	float time = 0.0f;          // 再生位置（秒）
	float duration = 0.0f;      // 長さ（秒）
	bool isPreview = false;     // 停止中のプレビューをしている
	bool isLoop = false;        // 停止中のプレビューをループする（ファイルにも Component にも書かない）

	// ===== 選択・操作 =====
	TimelineSelection selection;            // 選んでいる物
	CurveEditor curveEditor;                // 下のカーブの絵
	TimelineDrag drag = TimelineDrag::None; // つまんでいる物
	float dragOffset = 0.0f;                // つまんだ物とマウスの時間の差（秒）
	float dragKeyTime = 0.0f;               // つまんだキーの、操作の前の時間（クリップの始まりからの秒）
	std::vector<Curve> dragCurves;          // つまんだクリップの、操作の前のカーブ（毎フレームここから作り直す。途中で追い越してもハンドルが縮まない）

	// ===== Undo =====
	std::string committedText; // 最後に Undo へ積んだときの中身（JSON の文字）
	uint32_t seenRevision = 0; // 知っている中身の版（Undo で版が増えたら、選択を確かめ直す）
	bool hasEdit = false;      // 直して、まだ Undo に積んでいない

	// ===== 調べる =====
	Timeline* GetTimeline() const { return TimelineManager::GetForEdit(handle); }
	// 選んでいるトラック（無ければ nullptr）
	TimelineTrack* GetSelectedTrack() const;
	// 選んでいるクリップのカーブ（無ければ空）
	std::span<Curve> GetSelectedCurves() const;
	// 時間を目盛りへそろえる（Ctrl を押している間はそろえない）
	float Snap(float value) const;

	// ===== 変える =====
	// 中身を直した後に呼ぶ（操作が終わったら TimelineWindow が保存して Undo に積む）
	void MarkEdited();
	// 再生位置を動かす。停止中はプレビューを始める。Play 中は TimelineSystem::Seek
	void SeekTo(float value);
	void SelectTrack(int track);
	void SelectClip(int track, int clip);
	void SelectKey(int track, int clip, float keyTime);
	// 中身が変わった後に、無くなった物を選んだままにしない
	void ClampSelection();
};

// ===== 部品（どれも今のカーソルの位置から描く）=====
// 左上の Add Track と、時間の目盛り（ここをつまむと再生位置が動く）
void DrawTimelineRuler(TimelineEditorState& state);
// トラックの一覧と時間の列
void DrawTimelineRows(TimelineEditorState& state, float height);
// 横のスクロールバー
void DrawTimelineScrollbar(TimelineEditorState& state);
// 下の詳細（トラックの結び付け・クリップ・キー・カーブ）
void DrawTimelineDetails(TimelineEditorState& state, float height);

// ===== 部品の間で使う =====
// 新しいクリップのカーブに、対象の今の値で平らなキーを入れる
void FillNewClipCurves(const TimelineEditorState& state, TimelineTrack& track, size_t clip);
// 結び付けの問題（無ければ空。トラックの行に出す）
std::string FindTimelineBindingProblem(const TimelineEditorState& state, const TimelineTrack& track);
// トラックの対象を entity にする（再生する Entity の子孫でなければ、ログに出して変えない）
void SetTimelineTarget(TimelineEditorState& state, TimelineTrack& track, Handle<Entity> entity);
```

### B-3 `Editor/Timeline/TimelineEditorState.cpp`（新しいファイル）

```cpp
#ifdef USE_IMGUI
#include "TimelineEditorState.h"

#include <algorithm>

#include <externals/imgui/imgui.h>

#include "MyEngine/Systems/Animation/TimelineSystem.h"

//=============================================================================
// 調べる
//=============================================================================
TimelineTrack* TimelineEditorState::GetSelectedTrack() const {
	const Timeline* timeline = TimelineManager::Get(handle);
	if (timeline == nullptr || selection.track < 0 || selection.track >= static_cast<int>(timeline->tracks.size())) {
		return nullptr;
	}
	return timeline->tracks[selection.track].get();
}

std::span<Curve> TimelineEditorState::GetSelectedCurves() const {
	TimelineTrack* track = GetSelectedTrack();
	return (track != nullptr && selection.clip >= 0) ? track->GetClipCurves(static_cast<size_t>(selection.clip)) : std::span<Curve>{};
}

float TimelineEditorState::Snap(float value) const { return ImGui::GetIO().KeyCtrl ? value : SnapTime(value, snapStep); }

//=============================================================================
// 変える
//=============================================================================
void TimelineEditorState::MarkEdited() {
	TimelineManager::MarkChanged(handle);
	seenRevision = TimelineManager::GetRevision(handle); // 自分で増やした版（Undo と区別する）
	hasEdit = true;
}

void TimelineEditorState::SeekTo(float value) {
	time = std::clamp(value, 0.0f, duration);
	if (isGameRunning) {
		TimelineSystem::Seek(owner, time);
		return;
	}
	playhead.Seek(time, duration);
	isPreview = owner.IsValid();
}

void TimelineEditorState::SelectTrack(int track) { SelectClip(track, -1); }

void TimelineEditorState::SelectClip(int track, int clip) {
	if (selection.track != track || selection.clip != clip) {
		curveEditor = CurveEditor{}; // 別のカーブになるので、見ている範囲と選択をやり直す
	}
	selection.track = track;
	selection.clip = clip;
	selection.hasKey = false;
}

void TimelineEditorState::SelectKey(int track, int clip, float keyTime) {
	SelectClip(track, clip);
	selection.hasKey = true;
	selection.keyTime = keyTime;
}

void TimelineEditorState::ClampSelection() {
	const TimelineTrack* track = GetSelectedTrack();
	if (track == nullptr) {
		selection = {};
		curveEditor = CurveEditor{};
		return;
	}
	if (selection.clip >= static_cast<int>(track->GetClipCount())) {
		SelectClip(selection.track, -1);
	}
}
#endif
```

### B-4 `Editor/Timeline/TimelineWindow.cpp`（新しいファイル）

`ResolveOwner` が開く物を決めて、`UpdateTime` が時間を進めてプレビューを頼む。`CommitEdit` が保存と Undo をまとめて行う。

```cpp
#ifdef USE_IMGUI
#include "TimelineWindow.h"

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

#include <externals/imgui/imgui.h>

#include "MyEngine/Components/Animation/TimelineComponent.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/Editor/History/EditorHistory.h"
#include "MyEngine/Editor/Timeline/TimelineEditorState.h"
#include "MyEngine/Editor/Widgets/ToolIcons.h"
#include "MyEngine/Editor/Windows/HierarchyWindow.h"
#include "MyEngine/Editor/Windows/UIScreenEditor.h"
#include "MyEngine/Scene/SceneManager.h"
#include "MyEngine/String/ConvertString.h"
#include "MyEngine/Systems/Animation/TimelineSystem.h"

namespace {
constexpr float kFirstWidth = 960.0f;       // 最初のウィンドウの幅
constexpr float kFirstHeight = 520.0f;      // 最初のウィンドウの高さ
constexpr float kMinRowsHeight = 60.0f;     // トラックの一覧の高さの下限
constexpr float kMinDetailsHeight = 80.0f;  // 詳細の高さの下限
constexpr float kSplitterHeight = 6.0f;     // 詳細との境目の高さ
constexpr float kTimeFieldWidth = 80.0f;    // 再生位置の欄の幅
constexpr float kLengthFieldWidth = 70.0f;  // 長さの欄の幅
constexpr float kSnapFieldWidth = 80.0f;    // スナップの欄の幅

/// <summary>
/// スナップの選択肢1つ
/// </summary>
struct SnapChoice {
	const char* name;
	float step;
};
constexpr std::array<SnapChoice, 6> kSnapChoices = {{{"Off", 0.0f}, {"1 s", 1.0f}, {"0.5 s", 0.5f}, {"0.1 s", 0.1f}, {"1/30 s", 1.0f / 30.0f}, {"1/60 s", 1.0f / 60.0f}}};

TimelineEditorState state;                // ウィンドウの状態
Handle<Entity> lastSelected;              // 前に見た Hierarchy の選択（選び直したときだけファイルを開き直す）
Handle<Entity> lastScreenRoot;            // 前に見た編集中の画面（UI Screen）
bool needsFocus = false;                  // 次に描くとき、ウィンドウを前に出す
float detailsHeight = 240.0f;             // 下の詳細の高さ
std::vector<TimelineRange> passedRanges;  // プレビューで通った時間（使わないが、Advance が書く場所）

// ===== ファイルを開く =====
void OpenFile(uint32_t handle) {
	state.handle = handle;
	state.selection = {};
	state.curveEditor = CurveEditor{};
	state.playhead.Stop();
	state.time = 0.0f;
	state.isPreview = false;
	state.needsFit = true;
	state.hasEdit = false;
	state.committedText = TimelineManager::ToText(handle);
	state.seenRevision = TimelineManager::GetRevision(handle);
}

// ===== プレビューをやめる（次のフレームから要求を出さない＝値は元のまま）=====
void StopPreview() {
	state.playhead.Stop();
	state.time = 0.0f;
	state.isPreview = false;
}

// ===== 開く物と、再生する Entity を決める =====
void ResolveOwner() {
	// --- Hierarchy で Timeline の付いた Entity を選んだら、それを開く ---
	// Component が付くのを待てるように、付いていない間は覚えない（画面（UI Screen）の一番上は、選ばれた次のフレームに Component が付く）
	const Handle<Entity> selected = HierarchyWindow::GetSelected();
	const TimelineComponent* selectedComponent = EntityManager::Get<TimelineComponent>(selected);
	if (selectedComponent == nullptr || selectedComponent->timeline == 0) {
		lastSelected = {};
	} else if (selected != lastSelected) {
		lastSelected = selected;
		state.ownerId = EntityManager::Get(selected)->id;
		if (selectedComponent->timeline != state.handle) {
			OpenFile(selectedComponent->timeline);
		}
	}
	// --- 番号から今の Handle を引く。別のシーンの同じ番号の Entity と間違えないように、同じファイルを再生しているかも見る ---
	const Handle<Entity> found = EntityManager::FindById(state.ownerId);
	const TimelineComponent* component = EntityManager::Get<TimelineComponent>(found);
	state.owner = (component != nullptr && component->timeline == state.handle) ? found : Handle<Entity>{};
	// --- Inspector で別のファイルを選び直した ---
	if (found == selected && component != nullptr && component->timeline != 0 && component->timeline != state.handle) {
		OpenFile(component->timeline);
		state.owner = found;
	}
	// --- 再生する Entity が居なくなった（シーンの切り替え・消した）---
	if (!state.owner.IsValid()) {
		state.isPreview = false;
	}
	// --- 画面（UI Screen）の編集に入る・出るときは切る（中身が入れ替わる。編集中は画面の中の Entity を選べばプレビューできる）---
	const Handle<Entity> screenRoot = UIScreenEditor::GetRoot();
	if (screenRoot != lastScreenRoot) {
		lastScreenRoot = screenRoot;
		StopPreview();
	}
}

// ===== Undo で中身が変わっていたら、知っている中身を合わせる =====
void SyncRevision() {
	const uint32_t revision = TimelineManager::GetRevision(state.handle);
	if (revision != state.seenRevision) {
		state.seenRevision = revision;
		state.committedText = TimelineManager::ToText(state.handle);
		state.hasEdit = false;
		state.ClampSelection();
	}
}

// ===== 再生位置を進めて、プレビューを頼む =====
void UpdateTime(const Timeline& timeline) {
	const SceneManager* sceneManager = SceneManager::GetMain();
	const bool isGameRunning = sceneManager != nullptr && sceneManager->GetPlayState() != PlayState::Editing;
	if (isGameRunning && !state.isGameRunning) {
		StopPreview(); // Play を押した（このフレームから要求を出さないので、Play の退避は元の値を写す）
	}
	state.isGameRunning = isGameRunning;
	state.duration = timeline.GetDuration();

	// --- Play 中：TimelineSystem の時間を見せる ---
	if (isGameRunning) {
		const TimelineStatus status = TimelineSystem::GetStatus(state.owner);
		state.time = status.time;
		return;
	}
	// --- 停止中：ウィンドウの中で時間を進める ---
	const TimelineComponent* component = EntityManager::Get<TimelineComponent>(state.owner);
	const float speed = (component != nullptr && component->speed > 0.0f) ? component->speed : 1.0f;
	if (state.playhead.GetState() == TimelinePlayState::Playing) {
		state.playhead.Advance(ImGui::GetIO().DeltaTime * speed, state.duration, state.isLoop, passedRanges);
		state.time = state.playhead.GetTime();
	}
	state.time = (std::min)(state.time, state.duration); // 縮めた長さの外へ出ない
	if (state.isPreview && state.owner.IsValid()) {
		TimelineSystem::RequestPreview(state.owner, state.handle, state.time);
	}
}

// ===== 押している間は色を変えるボタン =====
bool ToggleButton(const char* label, bool isOn, const char* tooltip) {
	if (isOn) {
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_ButtonActive));
	}
	const bool isPressed = ImGui::Button(label);
	if (isOn) {
		ImGui::PopStyleColor();
	}
	ImGui::SetItemTooltip("%s", tooltip);
	return isPressed;
}

// ===== 再生・一時停止・停止・ループ =====
void DrawTransport() {
	const bool canPlay = state.owner.IsValid();
	const TimelineStatus status = TimelineSystem::GetStatus(state.owner);
	const bool isPlaying = state.isGameRunning ? status.IsPlaying() : state.playhead.GetState() == TimelinePlayState::Playing;
	ImGui::BeginDisabled(!canPlay);
	if (ToolIcons::Button("play", ToolIcon::Play, isPlaying, state.isGameRunning ? "この Entity の Timeline を再生する" : "プレビューを再生する（イベントは呼ばない）")) {
		if (state.isGameRunning) {
			TimelineSystem::Play(state.owner);
		} else {
			state.playhead.Seek(state.time, state.duration);
			state.playhead.Play();
			state.isPreview = true;
		}
	}
	ImGui::SameLine();
	if (ToolIcons::Button("pause", ToolIcon::Pause, false, "一時停止")) {
		if (state.isGameRunning) {
			TimelineSystem::Pause(state.owner);
		} else {
			state.playhead.Pause();
		}
	}
	ImGui::SameLine();
	if (ToolIcons::Button("stop", ToolIcon::Stop, false, state.isGameRunning ? "止めて最初へ戻す" : "プレビューをやめて最初へ戻す（画面は元の値へ戻る）")) {
		if (state.isGameRunning) {
			TimelineSystem::Stop(state.owner);
		} else {
			StopPreview();
		}
	}
	ImGui::SameLine();
	// --- ループ：Play 中は Component の値（Stop で元へ戻る）、停止中はプレビューだけ ---
	TimelineComponent* component = EntityManager::Get<TimelineComponent>(state.owner);
	const bool isLoop = (state.isGameRunning && component != nullptr) ? component->loop : state.isLoop;
	if (ToggleButton("Loop", isLoop, state.isGameRunning ? "最後まで行ったら最初へ戻す（Play の間だけ。Stop で元へ戻る）" : "プレビューをループする（保存しない）")) {
		if (state.isGameRunning && component != nullptr) {
			component->loop = !component->loop;
		} else {
			state.isLoop = !state.isLoop;
		}
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(state.isGameRunning);
	if (ToggleButton("Preview", state.isPreview, "停止中に、再生位置の値を画面に出す（切ると元の値へ戻る。Inspector・保存・Undo はいつも元の値を見る）")) {
		state.isPreview = !state.isPreview;
	}
	ImGui::EndDisabled();
	ImGui::EndDisabled();
}

// ===== 上の並び =====
void DrawToolbar(Timeline& timeline) {
	DrawTransport();
	// --- 再生位置 ---
	ImGui::SameLine();
	ImGui::SetNextItemWidth(kTimeFieldWidth);
	float time = state.time;
	if (ImGui::DragFloat("##time", &time, 0.01f, 0.0f, state.duration, "%.3f s", ImGuiSliderFlags_AlwaysClamp)) {
		state.SeekTo(time);
	}
	ImGui::SetItemTooltip("再生位置（秒）");
	// --- 長さ ---
	ImGui::SameLine();
	ImGui::SetNextItemWidth(kLengthFieldWidth);
	if (ImGui::DragFloat("##length", &timeline.length, 0.01f, 0.0f, Curve::kMaxValue, "%.2f s", ImGuiSliderFlags_AlwaysClamp)) {
		state.MarkEdited();
	}
	ImGui::SetItemTooltip("長さ（秒）。クリップがこれより長ければ、クリップの終わりまで再生する");
	// --- スナップ ---
	ImGui::SameLine();
	ImGui::SetNextItemWidth(kSnapFieldWidth);
	const auto current = std::ranges::find(kSnapChoices, state.snapStep, &SnapChoice::step);
	if (ImGui::BeginCombo("##snap", (current != kSnapChoices.end()) ? current->name : "Off")) {
		for (const SnapChoice& choice : kSnapChoices) {
			if (ImGui::Selectable(choice.name, choice.step == state.snapStep)) {
				state.snapStep = choice.step;
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("動かすときに目盛りへそろえる間隔（Ctrl を押している間はそろえない）");
	ImGui::SameLine();
	if (ImGui::Button("Fit")) {
		state.needsFit = true;
	}
	ImGui::SetItemTooltip("全体が見える範囲にする（F）");
	// --- 開いている物 ---
	ImGui::SameLine();
	const Entity* owner = EntityManager::Get(state.owner);
	const std::string& path = TimelineManager::GetPath(state.handle);
	ImGui::TextDisabled("%s  /  %s", ToUtf8(ToPath(path).filename()).c_str(), (owner != nullptr) ? owner->name.c_str() : "再生する Entity なし");
	ImGui::SetItemTooltip("%s", (owner != nullptr) ? path.c_str() : "Timeline Component でこのファイルを選んだ Entity を Hierarchy で選ぶと、プレビューできる");
}

// ===== 詳細との境目（つまんで高さを変える）=====
void DrawSplitter(float maxDetailsHeight) {
	ImGui::InvisibleButton("##splitter", ImVec2(ImGui::GetContentRegionAvail().x, kSplitterHeight));
	if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
	}
	if (ImGui::IsItemActive()) {
		detailsHeight -= ImGui::GetIO().MouseDelta.y;
	}
	detailsHeight = std::clamp(detailsHeight, kMinDetailsHeight, (std::max)(maxDetailsHeight, kMinDetailsHeight));
}

// ===== 操作が終わったら、保存して Undo に積む =====
void CommitEdit() {
	if (!state.hasEdit || ImGui::IsAnyItemActive()) {
		return;
	}
	state.hasEdit = false;
	std::string after = TimelineManager::ToText(state.handle);
	if (after == state.committedText) {
		return;
	}
	TimelineManager::Save(state.handle);
	const auto apply = [handle = state.handle](const std::string& text) { return TimelineManager::ApplyText(handle, text) && TimelineManager::Save(handle); };
	EditorHistory::Record([apply, before = std::move(state.committedText)] { return apply(before); }, [apply, value = after] { return apply(value); });
	state.committedText = std::move(after);
}
} // namespace

//=============================================================================
// 描く
//=============================================================================
void TimelineWindow::Draw() {
	ImGui::SetNextWindowSize(ImVec2(kFirstWidth, kFirstHeight), ImGuiCond_FirstUseEver);
	if (std::exchange(needsFocus, false)) {
		ImGui::SetNextWindowFocus();
	}
	// ウィンドウの中のスクロールは部品ごとに持つ。隠れていると Begin が false で何も頼まないので、プレビューは止まり値は元のまま
	if (ImGui::Begin("Timeline", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
		ResolveOwner();
		SyncRevision();
		if (Timeline* timeline = state.GetTimeline()) {
			UpdateTime(*timeline);
			DrawToolbar(*timeline);
			DrawTimelineRuler(state);
			const float available = ImGui::GetContentRegionAvail().y;
			const float rowsHeight = (std::max)(available - detailsHeight - kTimelineScrollbarHeight - kSplitterHeight, kMinRowsHeight);
			DrawTimelineRows(state, rowsHeight);
			DrawTimelineScrollbar(state);
			DrawSplitter(available - kMinRowsHeight - kTimelineScrollbarHeight - kSplitterHeight);
			DrawTimelineDetails(state, detailsHeight);
			CommitEdit();
		} else {
			ImGui::TextDisabled("Hierarchy で Timeline Component の付いた Entity を選ぶか、Project で .timeline をダブルクリックする");
		}
	}
	ImGui::End();
}

void TimelineWindow::Open(const std::filesystem::path& filePath) {
	const uint32_t handle = TimelineManager::Load(filePath);
	if (handle == 0) {
		return;
	}
	if (handle != state.handle) {
		OpenFile(handle);
	}
	// 同じファイルを再生している Entity があれば、それをプレビューに使う
	state.ownerId = 0;
	EntityManager::ForEach<TimelineComponent>([handle](Handle<Entity> entity, TimelineComponent& component) {
		if (state.ownerId == 0 && component.timeline == handle) {
			state.ownerId = EntityManager::Get(entity)->id;
		}
	});
	needsFocus = true;
}
#endif
```

### B-5 `Editor/Timeline/TimelineRows.cpp`（新しいファイル）

`FindHit` がマウスの下の物を探す。つまんでいる間は `UpdateDrag` が毎フレーム位置を計算する。キーは `dragCurves`（操作の前のカーブ）から作り直すので、途中で別のキーを追い越してもハンドルが縮まない（`CurveEditor` と同じ考え）。

```cpp
#ifdef USE_IMGUI
#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <utility>
#include <vector>

#include <externals/imgui/imgui.h>

#include "MyEngine/Editor/Timeline/TimelineEditorState.h"
#include "MyEngine/Editor/Windows/HierarchyWindow.h"
#include "MyEngine/Timeline/TimelineEdit.h"

// Timeline ウィンドウの真ん中（時間の目盛り・トラックの一覧・時間の列・スクロールバー）

namespace {
constexpr const char* kTrackDragType = "TIMELINE_TRACK"; // トラックの行をドラッグで運ぶときの種類の名前
constexpr float kRulerHeight = 24.0f;      // 時間の目盛りの高さ
constexpr float kTickMinPixels = 60.0f;    // 目盛りの間隔の下限（ピクセル）
constexpr int kMaxTicks = 256;             // 目盛りの線の数の上限
constexpr float kMuteButtonWidth = 26.0f;  // M ボタンの幅
constexpr float kClipPadding = 4.0f;       // 行の上下とクリップのすき間
constexpr float kClipRounding = 3.0f;      // クリップの角の丸み
constexpr float kKeySize = 5.0f;           // キーの印の大きさ
constexpr float kEventSize = 7.0f;         // イベントの印の大きさ
constexpr float kKeyHitDistance = 7.0f;    // キー・イベントをつかめる距離
constexpr float kEndHitWidth = 6.0f;       // クリップの右端をつかめる幅
constexpr float kPlayheadWidth = 6.0f;     // 目盛りの上の再生位置の三角の幅
constexpr float kZoomStep = 1.15f;         // ホイール1目盛りの拡大率
constexpr float kWheelPanPixels = 40.0f;   // Shift + ホイール1目盛りで動かすピクセル
constexpr float kScrollMargin = 1.1f;      // スクロールできる長さ（全体に対する割合。右に少し余白を残す）
constexpr float kMinThumbWidth = 20.0f;    // スクロールバーのつまみの幅の下限
constexpr float kGridAlpha = 0.18f;        // 目盛りの線の濃さ（灰色の文字の色に掛ける）
constexpr float kMutedAlpha = 0.35f;       // Mute のトラックの濃さ

// 背景・目盛りはテーマの色を使う（エディタの色はリニアなので、暗い色を IM_COL32 で書くと明るく出る）
constexpr ImU32 kClipColor = IM_COL32(60, 130, 170, 255);     // クリップ
constexpr ImU32 kSelectedColor = IM_COL32(255, 160, 40, 255); // 選んでいる物（選んだ物の枠と同じ橙）
constexpr ImU32 kPlayheadColor = IM_COL32(240, 90, 90, 255);  // 再生位置
constexpr ImU32 kProblemColor = IM_COL32(240, 110, 110, 255); // 結び付けの問題

/// <summary>
/// 列の上のマウスの下にある物
/// </summary>
struct LaneHit {
	int track = -1;         // 行（無ければ -1）
	int clip = -1;          // クリップ（無ければ -1）
	bool isKey = false;     // キーの印
	float keyTime = 0.0f;   // キーの時間（クリップの始まりからの秒）
	bool isEnd = false;     // クリップの右端
};

// 右クリックのメニューが相手にする物（開いた後はマウスが動くので、開いたときに覚える）
LaneHit menuHit;
float menuTime = 0.0f;

// ===== ひし形を描く =====
void DrawDiamond(ImDrawList* drawList, const ImVec2& center, float size, ImU32 color) {
	drawList->AddQuadFilled(ImVec2(center.x, center.y - size), ImVec2(center.x + size, center.y), ImVec2(center.x, center.y + size), ImVec2(center.x - size, center.y), color);
}

// ===== 見えている時間の目盛りを順に渡す =====
// float の足し算で進めると大きな場所で止まらなくなるので、番号から計算する
template<class Function> void ForEachTick(const TimelineTimeView& view, float width, Function&& function) {
	const float step = view.CalcTickStep(kTickMinPixels);
	const float first = std::ceil(view.scrollTime / step);
	for (int i = 0; i < kMaxTicks; ++i) {
		const float time = (first + static_cast<float>(i)) * step;
		const float x = view.ToPixel(time);
		if (x > width) {
			break;
		}
		function(time, x);
	}
}

// ===== クリップを足す（対象の今の値で平らなキーも入れる）=====
void AddClipAt(TimelineEditorState& state, int trackIndex, float time) {
	TimelineTrack& track = *state.GetTimeline()->tracks[trackIndex];
	const float start = (std::max)(state.Snap(time), 0.0f);
	const size_t clip = track.AddClip(start, TimelineEdit::CalcNewClipDuration(track, start));
	FillNewClipCurves(state, track, clip);
	state.SelectClip(trackIndex, static_cast<int>(clip));
	state.MarkEdited();
}

// ===== キーを足す（形は変えない）=====
void AddKeyAt(TimelineEditorState& state, int trackIndex, int clipIndex, float time) {
	TimelineTrack& track = *state.GetTimeline()->tracks[trackIndex];
	const float start = track.GetClipStart(clipIndex);
	const float keyTime = std::clamp(state.Snap(time) - start, 0.0f, track.GetClipDuration(clipIndex));
	TimelineEdit::AddKeys(track.GetClipCurves(clipIndex), keyTime);
	state.SelectKey(trackIndex, clipIndex, keyTime);
	state.MarkEdited();
}

// ===== 選んでいる物を消す（キー → クリップ → トラックの順に、一番細かい物）=====
void DeleteSelected(TimelineEditorState& state) {
	Timeline* timeline = state.GetTimeline();
	TimelineTrack* track = state.GetSelectedTrack();
	if (track == nullptr) {
		return;
	}
	TimelineSelection& selection = state.selection;
	if (selection.clip >= 0 && selection.hasKey) {
		TimelineEdit::RemoveKeys(track->GetClipCurves(selection.clip), selection.keyTime);
		selection.hasKey = false;
	} else if (selection.clip >= 0) {
		track->RemoveClip(selection.clip);
		state.SelectTrack(selection.track);
	} else {
		TimelineEdit::RemoveTrack(*timeline, selection.track);
		state.SelectTrack(-1);
	}
	state.MarkEdited();
}

//=============================================================================
// トラックの一覧（左）
//=============================================================================
// ===== 行のドロップ（トラックの並べ替え・Hierarchy の Entity を対象にする）=====
void AcceptRowDrop(TimelineEditorState& state, int index, int& moveFrom, int& moveTo) {
	if (!ImGui::BeginDragDropTarget()) {
		return;
	}
	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kTrackDragType)) {
		moveFrom = *static_cast<const int*>(payload->Data);
		moveTo = index;
	}
	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(HierarchyWindow::kDragDropType)) {
		SetTimelineTarget(state, *state.GetTimeline()->tracks[index], *static_cast<const Handle<Entity>*>(payload->Data));
	}
	ImGui::EndDragDropTarget();
}

// ===== 行の右クリックのメニュー =====
void DrawRowMenu(TimelineEditorState& state, int index, int& moveFrom, int& moveTo, int& deleteIndex) {
	if (!ImGui::BeginPopupContextItem("##rowMenu")) {
		return;
	}
	const int count = static_cast<int>(state.GetTimeline()->tracks.size());
	if (ImGui::MenuItem("Add Clip At Playhead")) {
		AddClipAt(state, index, state.time);
	}
	if (ImGui::MenuItem("Move Up", nullptr, false, index > 0)) {
		moveFrom = index;
		moveTo = index - 1;
	}
	if (ImGui::MenuItem("Move Down", nullptr, false, index + 1 < count)) {
		moveFrom = index;
		moveTo = index + 1;
	}
	if (ImGui::MenuItem("Delete Track")) {
		deleteIndex = index; // 行を描いている途中なので、ここでは消さない
	}
	ImGui::EndPopup();
}

// ===== 1行 =====
void DrawTrackRow(TimelineEditorState& state, int index, const ImVec2& rowMin, int& moveFrom, int& moveTo, int& deleteIndex) {
	Timeline& timeline = *state.GetTimeline();
	TimelineTrack& track = *timeline.tracks[index];
	const ImGuiStyle& style = ImGui::GetStyle();
	ImGui::PushID(index); // トラックは同じ名前でもよいので、番号で分ける
	ImGui::SetCursorScreenPos(rowMin);
	const float rowWidth = kTimelineListWidth - kMuteButtonWidth - style.ItemSpacing.x * 2.0f;
	if (ImGui::Selectable("##row", state.selection.track == index, ImGuiSelectableFlags_AllowOverlap, ImVec2(rowWidth, kTimelineRowHeight - style.ItemSpacing.y))) {
		state.SelectTrack(index);
	}
	if (ImGui::BeginDragDropSource()) {
		ImGui::SetDragDropPayload(kTrackDragType, &index, sizeof(index));
		ImGui::TextUnformatted(track.name.c_str());
		ImGui::EndDragDropSource();
	}
	AcceptRowDrop(state, index, moveFrom, moveTo);
	DrawRowMenu(state, index, moveFrom, moveTo, deleteIndex);
	// --- 問題があれば、マウスを乗せると出す ---
	const std::string problem = FindTimelineBindingProblem(state, track);
	if (!problem.empty()) {
		ImGui::SetItemTooltip("%s", problem.c_str());
	}
	// --- 名前（Selectable の上に書く）---
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImVec2 textPosition(rowMin.x + style.ItemSpacing.x, rowMin.y + (kTimelineRowHeight - ImGui::GetTextLineHeight()) * 0.5f);
	const ImU32 textColor = !problem.empty() ? kProblemColor : ImGui::GetColorU32(track.isMuted ? ImGuiCol_TextDisabled : ImGuiCol_Text);
	drawList->PushClipRect(rowMin, ImVec2(rowMin.x + rowWidth, rowMin.y + kTimelineRowHeight), true);
	drawList->AddText(textPosition, textColor, track.name.c_str());
	drawList->PopClipRect();
	// --- Mute ---
	ImGui::SameLine(0.0f, style.ItemSpacing.x);
	const bool wasMuted = track.isMuted; // 押すと変わるので、Push と Pop の数がそろうように覚えておく
	if (wasMuted) {
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_ButtonActive));
	}
	if (ImGui::Button("M", ImVec2(kMuteButtonWidth, 0.0f))) {
		track.isMuted = !track.isMuted;
		state.MarkEdited();
	}
	if (wasMuted) {
		ImGui::PopStyleColor();
	}
	ImGui::SetItemTooltip("このトラックを動かさない");
	ImGui::PopID();
}

//=============================================================================
// 時間の列（右）
//=============================================================================
/// <summary>
/// 列の場所（ボタンの左上と、行の数）
/// </summary>
struct LaneArea {
	ImVec2 min;
	ImVec2 size;
};

// ===== クリップの横の範囲 =====
float ClipLeft(const TimelineEditorState& state, const LaneArea& area, const TimelineTrack& track, size_t clip) { return area.min.x + state.view.ToPixel(track.GetClipStart(clip)); }
float ClipRight(const TimelineEditorState& state, const LaneArea& area, const TimelineTrack& track, size_t clip) {
	return area.min.x + state.view.ToPixel(track.GetClipStart(clip) + track.GetClipDuration(clip));
}

// ===== マウスの下にある物 =====
LaneHit FindHit(const TimelineEditorState& state, const LaneArea& area, const ImVec2& mouse) {
	const Timeline& timeline = *state.GetTimeline();
	LaneHit hit;
	const int row = static_cast<int>(std::floor((mouse.y - area.min.y) / kTimelineRowHeight));
	if (row < 0 || row >= static_cast<int>(timeline.tracks.size())) {
		return hit;
	}
	hit.track = row;
	TimelineTrack& track = *timeline.tracks[row];
	// --- 上に描かれている物（後ろのクリップ）から調べる ---
	for (int clip = static_cast<int>(track.GetClipCount()) - 1; clip >= 0; --clip) {
		const float left = ClipLeft(state, area, track, clip);
		const float right = ClipRight(state, area, track, clip);
		// 右端のすぐ外は長さを変える（終わりにキーがあっても、ここならつかめる）
		if (track.HasClipDuration() && mouse.x > right && mouse.x <= right + kEndHitWidth) {
			hit.clip = clip;
			hit.isEnd = true;
			return hit;
		}
		// キーはクリップの外へはみ出して描くことがあるので、範囲の前に調べる
		const std::span<Curve> curves = track.GetClipCurves(clip);
		for (const float keyTime : TimelineEdit::CollectKeyTimes(curves)) {
			if (std::abs(left + keyTime * state.view.pixelsPerSecond - mouse.x) <= kKeyHitDistance) {
				hit.clip = clip;
				hit.isKey = true;
				hit.keyTime = keyTime;
				return hit;
			}
		}
		if (!track.HasClipDuration()) {
			if (std::abs(left - mouse.x) <= kKeyHitDistance) {
				hit.clip = clip;
				return hit;
			}
			continue;
		}
		if (mouse.x >= left && mouse.x <= right) {
			hit.clip = clip;
			hit.isEnd = mouse.x >= right - kEndHitWidth;
			return hit;
		}
	}
	return hit;
}

// ===== 拡大縮小とスクロール =====
void HandleView(TimelineEditorState& state, const LaneArea& area) {
	const ImGuiIO& io = ImGui::GetIO();
	if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) {
		if (io.KeyCtrl) {
			ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseWheel * kTimelineRowHeight); // 縦に動かす
		} else if (io.KeyShift) {
			state.view.Pan(-io.MouseWheel * kWheelPanPixels); // 横に動かす
		} else {
			state.view.ZoomAt(io.MousePos.x - area.min.x, std::pow(kZoomStep, io.MouseWheel));
		}
	}
	// --- 中ボタンで動かす ---
	if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
		state.view.Pan(-io.MouseDelta.x);
		ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
	}
}

// ===== 押した物をつまむ =====
void BeginDrag(TimelineEditorState& state, const LaneHit& hit, float mouseTime) {
	state.drag = TimelineDrag::None;
	if (hit.clip < 0) {
		return;
	}
	TimelineTrack& track = *state.GetTimeline()->tracks[hit.track];
	const float start = track.GetClipStart(hit.clip);
	if (hit.isKey) {
		state.drag = TimelineDrag::Key;
		state.dragKeyTime = hit.keyTime;
		state.dragOffset = mouseTime - (start + hit.keyTime);
		const std::span<Curve> curves = track.GetClipCurves(hit.clip);
		state.dragCurves.assign(curves.begin(), curves.end());
	} else if (hit.isEnd) {
		state.drag = TimelineDrag::ClipEnd;
		state.dragOffset = mouseTime - (start + track.GetClipDuration(hit.clip));
	} else {
		state.drag = TimelineDrag::Clip;
		state.dragOffset = mouseTime - start;
	}
}

// ===== つまんでいる物を動かす =====
void UpdateDrag(TimelineEditorState& state, float mouseTime) {
	TimelineTrack* track = state.GetSelectedTrack();
	TimelineSelection& selection = state.selection;
	if (track == nullptr || selection.clip < 0) {
		state.drag = TimelineDrag::None;
		return;
	}
	const float target = state.Snap(mouseTime - state.dragOffset);
	const float start = track->GetClipStart(selection.clip);
	const float duration = track->GetClipDuration(selection.clip);
	switch (state.drag) {
	case TimelineDrag::None:
		return;
	case TimelineDrag::Clip:
		selection.clip = static_cast<int>(track->SetClipTime(selection.clip, (std::max)(target, 0.0f), duration));
		break;
	case TimelineDrag::ClipEnd:
		selection.clip = static_cast<int>(track->SetClipTime(selection.clip, start, (std::max)(target - start, TimelineEdit::kMinClipDuration)));
		break;
	case TimelineDrag::Key: {
		const std::span<Curve> curves = track->GetClipCurves(selection.clip);
		std::ranges::copy(state.dragCurves, curves.begin());
		selection.keyTime = std::clamp(target - start, 0.0f, duration);
		TimelineEdit::MoveKeys(curves, state.dragKeyTime, selection.keyTime);
		break;
	}
	}
	state.MarkEdited();
}

// ===== マウス（選ぶ・つまむ・足す）=====
void HandleMouse(TimelineEditorState& state, const LaneArea& area) {
	const ImGuiIO& io = ImGui::GetIO();
	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		state.drag = TimelineDrag::None;
	}
	if (!ImGui::IsMousePosValid()) {
		return;
	}
	const float mouseTime = state.view.ToTime(io.MousePos.x - area.min.x);
	const bool isHovered = ImGui::IsItemHovered();
	// --- 右端の上では、マウスの形で長さを変えられると知らせる ---
	if (state.drag == TimelineDrag::ClipEnd || (isHovered && FindHit(state, area, io.MousePos).isEnd)) {
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	}
	// --- 左：選ぶ・つまむ。ダブルクリックは足す ---
	if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		const LaneHit hit = FindHit(state, area, io.MousePos);
		if (hit.isKey) {
			state.SelectKey(hit.track, hit.clip, hit.keyTime);
		} else {
			state.SelectClip(hit.track, hit.clip);
		}
		if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hit.track >= 0) {
			state.drag = TimelineDrag::None; // この操作は「足す」だけ
			if (hit.clip < 0) {
				AddClipAt(state, hit.track, mouseTime);
			} else if (!hit.isKey && !state.GetTimeline()->tracks[hit.track]->GetClipCurves(hit.clip).empty()) {
				AddKeyAt(state, hit.track, hit.clip, mouseTime);
			}
		} else {
			BeginDrag(state, hit, mouseTime);
		}
	}
	// --- 右：メニュー（押したときに開くと ImGui が閉じてしまうので、離したときに開く。BeginPopupContextItem と同じ）---
	if (isHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
		menuHit = FindHit(state, area, io.MousePos);
		menuTime = mouseTime;
		if (menuHit.isKey) {
			state.SelectKey(menuHit.track, menuHit.clip, menuHit.keyTime);
		} else {
			state.SelectClip(menuHit.track, menuHit.clip);
		}
		ImGui::OpenPopup("##laneMenu");
	}
	// --- つまんで動かす（押した場所から少し動いてから。クリックだけでは値を変えない）---
	if (state.drag != TimelineDrag::None && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
		UpdateDrag(state, mouseTime);
	}
}

// ===== 右クリックのメニュー =====
void DrawLaneMenu(TimelineEditorState& state) {
	if (!ImGui::BeginPopup("##laneMenu")) {
		return;
	}
	if (menuHit.track < 0) {
		ImGui::TextDisabled("トラックの上で開く");
	} else if (menuHit.isKey) {
		if (ImGui::MenuItem("Delete Key")) {
			DeleteSelected(state);
		}
	} else if (menuHit.clip >= 0) {
		const bool hasCurves = !state.GetTimeline()->tracks[menuHit.track]->GetClipCurves(menuHit.clip).empty();
		if (ImGui::MenuItem("Add Key", nullptr, false, hasCurves)) {
			AddKeyAt(state, menuHit.track, menuHit.clip, menuTime);
		}
		if (ImGui::MenuItem("Delete Clip")) {
			DeleteSelected(state);
		}
	} else if (ImGui::MenuItem("Add Clip")) {
		AddClipAt(state, menuHit.track, menuTime);
	}
	ImGui::EndPopup();
}

// ===== キーボード =====
void HandleKeyboard(TimelineEditorState& state) {
	if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) || ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive()) {
		return;
	}
	if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
		DeleteSelected(state);
	}
	if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
		state.needsFit = true;
	}
}

// ===== クリップ1つを描く =====
void DrawClip(ImDrawList* drawList, const TimelineEditorState& state, const LaneArea& area, TimelineTrack& track, int trackIndex, int clip, float rowTop, ImU32 keyColor) {
	const TimelineSelection& selection = state.selection;
	const bool isSelected = selection.track == trackIndex && selection.clip == clip;
	const float left = ClipLeft(state, area, track, clip);
	const float centerY = rowTop + kTimelineRowHeight * 0.5f;
	// --- 1点のクリップ（イベント）---
	if (!track.HasClipDuration()) {
		DrawDiamond(drawList, ImVec2(left, centerY), kEventSize, isSelected ? kSelectedColor : kClipColor);
		return;
	}
	// --- 長さのあるクリップ ---
	const ImVec2 min(left, rowTop + kClipPadding);
	const ImVec2 max(ClipRight(state, area, track, clip), rowTop + kTimelineRowHeight - kClipPadding);
	drawList->AddRectFilled(min, max, kClipColor, kClipRounding);
	if (isSelected) {
		drawList->AddRect(min, max, kSelectedColor, kClipRounding, 0, 2.0f);
	}
	// --- キーの印（同じ時間のキーは1つに見せる）---
	for (const float keyTime : TimelineEdit::CollectKeyTimes(track.GetClipCurves(clip))) {
		const bool isSelectedKey = isSelected && selection.hasKey && std::abs(selection.keyTime - keyTime) <= TimelineEdit::kSameKeyTime;
		DrawDiamond(drawList, ImVec2(left + keyTime * state.view.pixelsPerSecond, centerY), kKeySize, isSelectedKey ? kSelectedColor : keyColor);
	}
}

// ===== 列を描く =====
void DrawLanes(const TimelineEditorState& state, const LaneArea& area) {
	const Timeline& timeline = *state.GetTimeline();
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImVec2 max(area.min.x + area.size.x, area.min.y + area.size.y);
	drawList->PushClipRect(area.min, max, true);
	drawList->AddRectFilled(area.min, max, ImGui::GetColorU32(ImGuiCol_FrameBg));
	// --- 目盛りの線 ---
	const ImU32 gridColor = ImGui::GetColorU32(ImGuiCol_TextDisabled, kGridAlpha);
	ForEachTick(state.view, area.size.x, [&](float /*time*/, float x) { drawList->AddLine(ImVec2(area.min.x + x, area.min.y), ImVec2(area.min.x + x, max.y), gridColor); });
	// --- 行とクリップ ---
	for (int i = 0; i < static_cast<int>(timeline.tracks.size()); ++i) {
		TimelineTrack& track = *timeline.tracks[i];
		const float rowTop = area.min.y + static_cast<float>(i) * kTimelineRowHeight;
		drawList->AddLine(ImVec2(area.min.x, rowTop + kTimelineRowHeight), ImVec2(max.x, rowTop + kTimelineRowHeight), gridColor);
		const ImU32 keyColor = ImGui::GetColorU32(ImGuiCol_Text, track.isMuted ? kMutedAlpha : 1.0f);
		for (int clip = 0; clip < static_cast<int>(track.GetClipCount()); ++clip) {
			DrawClip(drawList, state, area, track, i, clip, rowTop, keyColor);
		}
	}
	// --- 終わりの線と再生位置 ---
	const float endX = area.min.x + state.view.ToPixel(state.duration);
	drawList->AddLine(ImVec2(endX, area.min.y), ImVec2(endX, max.y), ImGui::GetColorU32(ImGuiCol_TextDisabled));
	const float playheadX = area.min.x + state.view.ToPixel(state.time);
	drawList->AddLine(ImVec2(playheadX, area.min.y), ImVec2(playheadX, max.y), kPlayheadColor);
	drawList->PopClipRect();
}
} // namespace

//=============================================================================
// 時間の目盛り
//=============================================================================
void DrawTimelineRuler(TimelineEditorState& state) {
	Timeline& timeline = *state.GetTimeline();
	// --- トラックを足す ---
	ImGui::SetNextItemWidth(kTimelineListWidth - ImGui::GetStyle().ItemSpacing.x);
	if (ImGui::BeginCombo("##addTrack", "Add Track")) {
		for (const std::string& name : TimelineTrackRegistry::GetNames()) {
			if (ImGui::Selectable(name.c_str())) {
				state.SelectTrack(TimelineEdit::AddTrack(timeline, name));
				state.MarkEdited();
			}
			const std::string& description = TimelineTrackRegistry::GetDescription(name);
			if (!description.empty()) {
				ImGui::SetItemTooltip("%s", description.c_str());
			}
		}
		ImGui::EndCombo();
	}
	// --- 目盛り（つまむと再生位置が動く）---
	ImGui::SameLine(kTimelineListWidth + ImGui::GetStyle().WindowPadding.x);
	const ImVec2 min = ImGui::GetCursorScreenPos();
	state.laneWidth = (std::max)(ImGui::GetContentRegionAvail().x, 1.0f);
	if (std::exchange(state.needsFit, false)) {
		state.view.Fit(state.duration, state.laneWidth);
	}
	ImGui::InvisibleButton("##ruler", ImVec2(state.laneWidth, kRulerHeight));
	if (ImGui::IsItemActive()) {
		if (ImGui::IsItemActivated() && !state.isGameRunning) {
			state.playhead.Pause(); // つまんでいる間は進めない
		}
		state.SeekTo(state.Snap(state.view.ToTime(ImGui::GetIO().MousePos.x - min.x)));
	}
	if (!ImGui::IsItemActive()) {
		ImGui::SetItemTooltip("つまむと再生位置が動く。停止中はプレビューになる（Ctrl：目盛りにそろえない）");
	}

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	const ImVec2 max(min.x + state.laneWidth, min.y + kRulerHeight);
	drawList->PushClipRect(min, max, true);
	drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBgHovered));
	const ImU32 labelColor = ImGui::GetColorU32(ImGuiCol_Text);
	ForEachTick(state.view, state.laneWidth, [&](float time, float x) {
		drawList->AddLine(ImVec2(min.x + x, max.y - 6.0f), ImVec2(min.x + x, max.y), labelColor);
		drawList->AddText(ImVec2(min.x + x + 3.0f, min.y + 1.0f), labelColor, std::format("{:.6g}", time).c_str());
	});
	const float playheadX = min.x + state.view.ToPixel(state.time);
	drawList->AddTriangleFilled(ImVec2(playheadX - kPlayheadWidth, min.y), ImVec2(playheadX + kPlayheadWidth, min.y), ImVec2(playheadX, max.y), kPlayheadColor);
	drawList->PopClipRect();
}

//=============================================================================
// トラックの一覧と時間の列
//=============================================================================
void DrawTimelineRows(TimelineEditorState& state, float height) {
	// ホイールは列のボタンが受け取る。一覧の上のホイールは、この窓の縦のスクロールになる
	ImGui::BeginChild("##rows", ImVec2(0.0f, height));
	const Timeline& timeline = *state.GetTimeline();
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const int count = static_cast<int>(timeline.tracks.size());

	// --- 左の一覧（並べ替えと消すのは回し終わってから。回している間に消すと、track の参照と count が古くなる）---
	int moveFrom = -1;
	int moveTo = -1;
	int deleteIndex = -1;
	for (int i = 0; i < count; ++i) {
		DrawTrackRow(state, i, ImVec2(origin.x, origin.y + static_cast<float>(i) * kTimelineRowHeight), moveFrom, moveTo, deleteIndex);
	}
	if (moveFrom >= 0 && moveFrom != moveTo) {
		state.SelectTrack(TimelineEdit::MoveTrack(*state.GetTimeline(), moveFrom, moveTo));
		state.MarkEdited();
	}
	if (deleteIndex >= 0) {
		state.SelectTrack(deleteIndex);
		DeleteSelected(state);
	}

	// --- 右の列（Add Track・消すで行の数が変わるので、ここで数え直す）---
	const float contentHeight = static_cast<float>(state.GetTimeline()->tracks.size()) * kTimelineRowHeight;
	const LaneArea area = {ImVec2(origin.x + kTimelineListWidth, origin.y), ImVec2(state.laneWidth, (std::max)(contentHeight, height))};
	ImGui::SetCursorScreenPos(area.min);
	ImGui::InvisibleButton("##lanes", area.size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
	ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY); // ホイールを、窓のスクロールより先に受け取る
	// IsItem〜 は直前の項目＝上のボタンを見るので、間に項目を置かない
	HandleView(state, area);
	HandleMouse(state, area);
	HandleKeyboard(state);
	DrawLanes(state, area);
	DrawLaneMenu(state);
	ImGui::EndChild();
}

//=============================================================================
// 横のスクロールバー
//=============================================================================
void DrawTimelineScrollbar(TimelineEditorState& state) {
	const float width = state.laneWidth;
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + kTimelineListWidth);
	const ImVec2 min = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton("##hScroll", ImVec2(width, kTimelineScrollbarHeight));
	// --- 全体の長さと見えている範囲 ---
	const float visible = width / state.view.pixelsPerSecond;
	const float total = (std::max)(state.duration * kScrollMargin, state.view.scrollTime + visible);
	if (ImGui::IsItemActive()) {
		state.view.Pan(ImGui::GetIO().MouseDelta.x * total / width * state.view.pixelsPerSecond);
	}
	const float thumbWidth = (std::max)(visible / total * width, kMinThumbWidth);
	const float thumbLeft = min.x + state.view.scrollTime / total * width;
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddRectFilled(min, ImVec2(min.x + width, min.y + kTimelineScrollbarHeight), ImGui::GetColorU32(ImGuiCol_ScrollbarBg));
	const ImU32 thumbColor = ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_ScrollbarGrabActive : ImGuiCol_ScrollbarGrab);
	drawList->AddRectFilled(ImVec2(thumbLeft, min.y), ImVec2(thumbLeft + thumbWidth, min.y + kTimelineScrollbarHeight), thumbColor, kTimelineScrollbarHeight * 0.5f);
}
#endif
```

### B-6 `Editor/Timeline/TimelineDetails.cpp`（新しいファイル）

```cpp
#ifdef USE_IMGUI
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include <externals/imgui/imgui.h>
#include <externals/imgui/imgui_internal.h>

#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/Editor/Timeline/TimelineEditorState.h"
#include "MyEngine/Timeline/TimelineEdit.h"
#include "MyEngine/Timeline/TimelineValueTrack.h"

// Timeline ウィンドウの下の詳細（トラックの結び付け・クリップ・キー・カーブ）

namespace {
constexpr float kFieldsWidth = 340.0f;        // 左の項目の幅
constexpr float kItemLabelWidth = 90.0f;      // 項目の右の名前に残す幅
constexpr float kCurveMinHeight = 80.0f;      // カーブの絵の高さの下限
constexpr float kCurveFooterHeight = 150.0f;  // カーブの絵の下の数値の欄に残す高さ
constexpr const char* kDefaultTrackName = "Track"; // トラックの名前の既定の値（結び付けを選ぶと自動の名前に替える）
constexpr std::array<const char*, ComponentValue::kChannelCount> kVectorNames = {"x", "y", "z", "w"}; // 成分の名前
constexpr std::array<const char*, ComponentValue::kChannelCount> kColorNames = {"r", "g", "b", "a"};  // 色の成分の名前

// ===== パスの一番下の名前 =====
std::string_view LeafName(std::string_view path) {
	const size_t slash = path.rfind('/');
	return (slash == std::string_view::npos) ? path : path.substr(slash + 1);
}

// ===== 自動の名前（Title.color）=====
std::string MakeAutoName(const TimelineValueTrack& track) { return track.target.empty() ? track.component + "." + track.property : std::string(LeafName(track.target)) + "." + track.property; }

// ===== 結び付けを変えた後、名前が自動のままなら付け直す =====
void UpdateAutoName(TimelineValueTrack& track, const std::string& oldAutoName) {
	if (track.name == kDefaultTrackName || track.name == oldAutoName) {
		track.name = MakeAutoName(track);
	}
}

// ===== 対象の Entity の項目の形（分からなければ空）=====
std::optional<ComponentValueField> FindField(const TimelineEditorState& state, const TimelineValueTrack& track) {
	const Handle<Entity> target = EntityManager::FindByPath(state.owner, track.target);
	if (!target.IsValid()) {
		return std::nullopt;
	}
	const std::vector<ComponentValueField> fields = ComponentSerializer::GetValueFields(target, track.component);
	const auto found = std::ranges::find(fields, track.property, &ComponentValueField::key);
	return (found != fields.end()) ? std::optional(*found) : std::nullopt;
}

// ===== 成分の名前 =====
std::string ChannelName(const std::optional<ComponentValueField>& field, size_t channel) {
	if (!field.has_value()) {
		return kVectorNames[channel];
	}
	if (field->channelCount == 1) {
		return "value";
	}
	return field->isColor ? kColorNames[channel] : kVectorNames[channel];
}

//=============================================================================
// トラック
//=============================================================================
// ===== 再生する Entity の子孫を選択肢に出す =====
void DrawTargetChoices(TimelineEditorState& state, TimelineTrack& track, Handle<Entity> parent, int& id) {
	std::vector<Handle<Entity>> children;
	EntityManager::GetChildren(parent, children);
	for (const Handle<Entity> child : children) {
		const std::string path = EntityManager::MakePath(state.owner, child).value_or("");
		ImGui::PushID(id++); // 同じ名前の兄弟があっても項目が衝突しないように、数えた番号で分ける
		if (ImGui::Selectable(path.c_str(), path == track.target)) {
			SetTimelineTarget(state, track, child);
		}
		ImGui::PopID();
		DrawTargetChoices(state, track, child, id);
	}
}

// ===== 対象 =====
void DrawTarget(TimelineEditorState& state, TimelineTrack& track) {
	if (!state.owner.IsValid()) {
		GetInspectorUI().Field("Target###target", track.target, Tip("動かす相手。再生する Entity から、子の名前を / でつないで書く。空なら再生する Entity 自身"));
		return;
	}
	if (ImGui::BeginCombo("Target###target", track.target.empty() ? "(self)" : track.target.c_str())) {
		if (ImGui::Selectable("(self)", track.target.empty())) {
			SetTimelineTarget(state, track, state.owner);
		}
		int id = 0;
		DrawTargetChoices(state, track, state.owner, id);
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("動かす相手（再生する Entity とその子孫）。Hierarchy の Entity をトラックの行へドラッグしても選べる");
}

// ===== Component と項目 =====
void DrawValueBinding(TimelineEditorState& state, TimelineValueTrack& track) {
	const Handle<Entity> target = EntityManager::FindByPath(state.owner, track.target);
	if (!target.IsValid()) {
		track.Fields(GetInspectorUI()); // 対象が無い間は、文字で直せるようにする
		return;
	}
	const std::string oldAutoName = MakeAutoName(track);
	// --- Component（値を動かせる項目がある物だけ）---
	if (ImGui::BeginCombo("Component###component", track.component.c_str())) {
		for (const std::string& name : ComponentSerializer::GetComponentNames(target)) {
			if (ComponentSerializer::GetValueFields(target, name).empty()) {
				continue;
			}
			if (ImGui::Selectable(name.c_str(), name == track.component)) {
				track.component = name;
				state.MarkEdited();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("動かす Component");
	// --- 項目 ---
	const std::vector<ComponentValueField> fields = ComponentSerializer::GetValueFields(target, track.component);
	if (ImGui::BeginCombo("Property###property", track.property.c_str())) {
		for (const ComponentValueField& field : fields) {
			if (ImGui::Selectable(std::format("{}###{}", field.label, field.key).c_str(), field.key == track.property)) {
				track.property = field.key;
				UpdateAutoName(track, oldAutoName);
				state.MarkEdited();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SetItemTooltip("動かす項目（数で表せる物：float・int・bool・Vector・色）");
}

// ===== トラック全体 =====
void DrawTrackFields(TimelineEditorState& state, TimelineTrack& track) {
	ImGui::SeparatorText(track.typeName.c_str());
	ImGui::PushID("track"); // クリップの項目と名前が重なる（Name###name）ので分ける
	GetInspectorUI().Field("Name###name", track.name, Tip("トラックの名前（一覧と警告に出す）"));
	DrawTarget(state, track);
	if (auto* valueTrack = dynamic_cast<TimelineValueTrack*>(&track)) {
		DrawValueBinding(state, *valueTrack);
	} else {
		track.Fields(GetInspectorUI());
	}
	ImGui::PopID();
}

//=============================================================================
// クリップ
//=============================================================================
// ===== 成分（チェック：キーがある＝動かす / 名前：カーブの絵に出す）=====
void DrawChannels(TimelineEditorState& state, TimelineTrack& track, std::span<Curve> curves) {
	const auto* valueTrack = dynamic_cast<const TimelineValueTrack*>(&track);
	const std::optional<ComponentValueField> field = (valueTrack != nullptr) ? FindField(state, *valueTrack) : std::nullopt;
	const size_t count = field.has_value() ? (std::min)(field->channelCount, curves.size()) : curves.size();
	state.selection.channel = (std::min)(state.selection.channel, static_cast<int>(count) - 1);
	// --- 選んでいる成分にキーが無ければ、キーのある最初の成分を絵に出す ---
	if (curves[state.selection.channel].GetKeys().empty()) {
		const auto used = std::ranges::find_if(curves, [](const Curve& curve) { return !curve.GetKeys().empty(); });
		if (used != curves.end()) {
			state.selection.channel = static_cast<int>(used - curves.begin());
			state.curveEditor = CurveEditor{};
		}
	}
	ImGui::PushID("channels"); // 下のキーの値の欄と同じ名前（a など）になるので分ける
	for (size_t i = 0; i < count; ++i) {
		ImGui::PushID(static_cast<int>(i));
		bool isUsed = !curves[i].GetKeys().empty();
		if (ImGui::Checkbox("##used", &isUsed)) {
			if (isUsed) {
				TimelineEdit::FillFlat(curves[i], 0.0f, track.GetClipDuration(state.selection.clip));
			} else {
				curves[i] = Curve();
			}
			state.curveEditor = CurveEditor{};
			state.MarkEdited();
		}
		ImGui::SetItemTooltip("この成分を動かす（外すとキーを全部消す）");
		ImGui::SameLine();
		if (ImGui::Selectable(ChannelName(field, i).c_str(), state.selection.channel == static_cast<int>(i), 0, ImVec2(40.0f, 0.0f))) {
			state.selection.channel = static_cast<int>(i);
			state.curveEditor = CurveEditor{};
		}
		ImGui::PopID();
		if (i + 1 < count) {
			ImGui::SameLine();
		}
	}
	ImGui::PopID();
}

// ===== 選んでいるキーの値（全部の成分を並べて打てる）=====
void DrawKeyFields(TimelineEditorState& state, TimelineTrack& track, std::span<Curve> curves) {
	TimelineSelection& selection = state.selection;
	// --- カーブの絵で動かしたり消したりしたら、もう選んでいない ---
	const std::vector<float> times = TimelineEdit::CollectKeyTimes(curves);
	const bool isFound = std::ranges::any_of(times, [&selection](float time) { return std::abs(time - selection.keyTime) <= TimelineEdit::kSameKeyTime; });
	if (!selection.hasKey || !isFound) {
		selection.hasKey = false;
		return;
	}
	ImGui::SeparatorText("Key");
	const float duration = track.GetClipDuration(selection.clip);
	float keyTime = selection.keyTime;
	if (ImGui::DragFloat("Time###keyTime", &keyTime, 0.01f, 0.0f, duration, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
		TimelineEdit::MoveKeys(curves, selection.keyTime, keyTime);
		selection.keyTime = keyTime;
		state.MarkEdited();
	}
	ImGui::SetItemTooltip("キーの時間（クリップの始まりからの秒）");
	// --- その時間にキーのある成分の値 ---
	const auto* valueTrack = dynamic_cast<const TimelineValueTrack*>(&track);
	const std::optional<ComponentValueField> field = (valueTrack != nullptr) ? FindField(state, *valueTrack) : std::nullopt;
	ImGui::PushID("keyValues");
	for (size_t i = 0; i < curves.size(); ++i) {
		const std::vector<CurveKey>& keys = curves[i].GetKeys();
		const auto key = std::ranges::find_if(keys, [&selection](const CurveKey& item) { return std::abs(item.time - selection.keyTime) <= TimelineEdit::kSameKeyTime; });
		if (key == keys.end()) {
			continue;
		}
		ImGui::PushID(static_cast<int>(i));
		float value = key->value;
		if (ImGui::DragFloat(ChannelName(field, i).c_str(), &value, 0.01f)) {
			CurveKey edited = *key;
			edited.value = value;
			curves[i].SetKey(static_cast<int>(key - keys.begin()), edited);
			state.MarkEdited();
		}
		ImGui::PopID();
	}
	ImGui::PopID();
}

// ===== クリップ全体 =====
void DrawClipFields(TimelineEditorState& state, TimelineTrack& track) {
	ImGui::SeparatorText("Clip");
	ImGui::PushID("clip");
	state.selection.clip = static_cast<int>(track.DescribeClip(state.selection.clip, GetInspectorUI()));
	const std::span<Curve> curves = track.GetClipCurves(state.selection.clip);
	if (!curves.empty()) {
		DrawChannels(state, track, curves);
		if (ImGui::SmallButton("Add Key At Playhead")) {
			const float keyTime = std::clamp(state.time - track.GetClipStart(state.selection.clip), 0.0f, track.GetClipDuration(state.selection.clip));
			TimelineEdit::AddKeys(curves, keyTime);
			state.SelectKey(state.selection.track, state.selection.clip, keyTime);
			state.MarkEdited();
		}
		ImGui::SetItemTooltip("再生位置に、形を変えずにキーを足す（列の上のクリップをダブルクリックでも足せる）");
		DrawKeyFields(state, track, curves);
	}
	ImGui::PopID();
}

// ===== カーブの絵 =====
void DrawCurvePanel(TimelineEditorState& state, std::span<Curve> curves) {
	if (curves.empty()) {
		ImGui::TextDisabled("このクリップにカーブはありません");
		return;
	}
	if (curves[state.selection.channel].GetKeys().empty()) {
		ImGui::TextDisabled("動かす成分にチェックを入れると、ここでカーブを直せる");
		return;
	}
	const float canvasHeight = (std::max)(ImGui::GetContentRegionAvail().y - kCurveFooterHeight, kCurveMinHeight);
	if (state.curveEditor.Draw("curve", curves[state.selection.channel], canvasHeight, GetInspectorUI())) {
		state.MarkEdited();
	}
	state.curveEditor.TakeFinishedEdit(); // Undo は TimelineWindow がファイル全体でまとめて積むので、使わず受け取る
}
} // namespace

//=============================================================================
// 描く
//=============================================================================
void DrawTimelineDetails(TimelineEditorState& state, float height) {
	TimelineTrack* track = state.GetSelectedTrack();
	if (track == nullptr) {
		ImGui::TextDisabled("トラックを選ぶと、ここで結び付けとキーを直せる");
		const Timeline* timeline = state.GetTimeline();
		if (!timeline->unknownTracks.empty()) {
			ImGui::TextDisabled("知らない種類のトラックが %d 個あります（動かしません。保存しても消えません）", static_cast<int>(timeline->unknownTracks.size()));
		}
		return;
	}
	// --- 左：項目 ---
	ImGui::BeginChild("##fields", ImVec2(kFieldsWidth, height), ImGuiChildFlags_Borders);
	ImGui::PushItemWidth(-kItemLabelWidth);
	DrawTrackFields(state, *track);
	if (state.selection.clip >= 0) {
		DrawClipFields(state, *track);
	}
	ImGui::PopItemWidth();
	ImGui::EndChild();
	// ComponentUI の項目は「変えたか」を返さないので、ImGui の「今操作している項目を変えた」印を見る（打っている文字にも効く）
	if (ImGui::GetCurrentContext()->ActiveIdHasBeenEditedThisFrame) {
		state.MarkEdited();
	}
	// --- 右：カーブ ---
	ImGui::SameLine();
	ImGui::BeginChild("##curve", ImVec2(0.0f, height), ImGuiChildFlags_Borders);
	if (state.selection.clip >= 0) {
		DrawCurvePanel(state, state.GetSelectedCurves());
	} else {
		ImGui::TextDisabled("クリップを選ぶと、ここでカーブを直せる（列の何も無い所をダブルクリックでクリップを足す）");
	}
	ImGui::EndChild();
}

//=============================================================================
// 部品の間で使う
//=============================================================================
void FillNewClipCurves(const TimelineEditorState& state, TimelineTrack& track, size_t clip) {
	const std::span<Curve> curves = track.GetClipCurves(clip);
	if (curves.empty()) {
		return;
	}
	// --- 対象の今の値を読む（読めなければ最初の成分を 0 で）---
	ComponentValue value;
	value.isUsed[0] = true;
	if (const auto* valueTrack = dynamic_cast<const TimelineValueTrack*>(&track)) {
		const Handle<Entity> target = EntityManager::FindByPath(state.owner, valueTrack->target);
		if (target.IsValid()) {
			ComponentSerializer::GetValue(target, valueTrack->component, valueTrack->property, value);
		}
	}
	for (size_t i = 0; i < curves.size(); ++i) {
		if (value.isUsed[i]) {
			TimelineEdit::FillFlat(curves[i], value.channels[i], track.GetClipDuration(clip));
		}
	}
}

std::string FindTimelineBindingProblem(const TimelineEditorState& state, const TimelineTrack& track) {
	if (!state.owner.IsValid()) {
		return {}; // 再生する Entity が分からないので、調べられない
	}
	const Handle<Entity> target = EntityManager::FindByPath(state.owner, track.target);
	if (!target.IsValid()) {
		return std::format("対象が見つかりません: \"{}\"", track.target);
	}
	const auto* valueTrack = dynamic_cast<const TimelineValueTrack*>(&track);
	if (valueTrack == nullptr) {
		return {};
	}
	ComponentValue value;
	switch (ComponentSerializer::GetValue(target, valueTrack->component, valueTrack->property, value)) {
	case ComponentValueResult::Written:
		return {};
	case ComponentValueResult::NoComponent:
		return std::format("\"{}\" に Component \"{}\" がありません", track.target, valueTrack->component);
	case ComponentValueResult::NoField:
		return std::format("Component \"{}\" に、動かせる項目 \"{}\" がありません", valueTrack->component, valueTrack->property);
	}
	return {};
}

void SetTimelineTarget(TimelineEditorState& state, TimelineTrack& track, Handle<Entity> entity) {
	const std::optional<std::string> path = EntityManager::MakePath(state.owner, entity);
	if (!path.has_value()) {
		LogManager::Warning("Timeline の対象にできるのは、再生する Entity とその子孫だけです");
		return;
	}
	auto* valueTrack = dynamic_cast<TimelineValueTrack*>(&track);
	const std::string oldAutoName = (valueTrack != nullptr) ? MakeAutoName(*valueTrack) : std::string();
	track.target = *path;
	if (valueTrack != nullptr) {
		UpdateAutoName(*valueTrack, oldAutoName);
	}
	state.MarkEdited();
}
#endif
```

### B-7 `Editor/Inspector/TimelineAssetEditor.h`（新しいファイル）

```cpp
#pragma once

/// <summary>
/// .timeline のファイルを Inspector に出す係（AssetInspector に登録する）。エディタだけ
/// <para>中身は Timeline ウィンドウで直す。ここは様子と、開くボタンだけ</para>
/// </summary>
class TimelineAssetEditor {
public:
	// AssetInspector に登録する
	static void Initialize();
};
```

### B-8 `Editor/Inspector/TimelineAssetEditor.cpp`（新しいファイル）

```cpp
#ifdef USE_IMGUI
#include "TimelineAssetEditor.h"

#include <cstdint>
#include <format>
#include <memory>

#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/Editor/Inspector/AssetInspector.h"
#include "MyEngine/Editor/Timeline/TimelineWindow.h"
#include "MyEngine/Timeline/TimelineManager.h"

namespace {
constexpr std::string_view kTypeName = "Timeline"; // Inspector の見出しに出す名前

/// <summary>
/// .timeline のファイルを Inspector に出す係
/// </summary>
class TimelineFileEditor final : public AssetEditor {
public:
	std::string_view GetExtension() const override { return TimelineManager::kFileExtension; }
	std::string_view GetTypeName() const override { return kTypeName; }
	bool CanLoad(const std::filesystem::path& filePath) override { return TimelineManager::Load(filePath) != 0; }

	bool Draw(const std::filesystem::path& filePath, ComponentUI& ui) override {
		const Timeline* timeline = TimelineManager::Get(TimelineManager::Load(filePath));
		if (timeline == nullptr) {
			return false;
		}
		ui.Label(std::format("トラック：{} 個　長さ：{:.2f} 秒", timeline->tracks.size(), timeline->GetDuration()).c_str());
		if (ui.Button("Open Timeline")) {
			TimelineWindow::Open(filePath);
		}
		ui.Label("Project でダブルクリックしても開ける");
		return false; // ここでは中身を変えない
	}

	void Save(const std::filesystem::path&) override {} // 保存は Timeline ウィンドウ
};
} // namespace

//=============================================================================
// 初期化
//=============================================================================
void TimelineAssetEditor::Initialize() { AssetInspector::Register(std::make_unique<TimelineFileEditor>()); }
#endif
```

### B-9 `Systems/EngineSystems.cpp`

`#ifdef USE_IMGUI` の中の include に足す。

```cpp
#include "MyEngine/Editor/Inspector/CurveAssetEditor.h"
#include "MyEngine/Editor/Inspector/TimelineAssetEditor.h"
```

`Initialize` の `#ifdef USE_IMGUI` の中に足す。

```cpp
	CurveAssetEditor::Initialize();  // .curve のファイルを Inspector で直す
	TimelineAssetEditor::Initialize(); // .timeline のファイルを Inspector に出す（開くボタン）
```

### B-10 `Editor/ImGuiManager.cpp`

include に足す。

```cpp
#include "MyEngine/Editor/Windows/AttackTableWindow.h"
#include "MyEngine/Editor/Timeline/TimelineWindow.h"
```

`ImGuiManager::Begin` の `AttackTableWindow::Draw();` の下に足す。

```cpp
	AttackTableWindow::Draw();

	// --- Timeline を直す ---
	TimelineWindow::Draw();
```

### B-11 `Editor/Windows/ProjectWindowActions.cpp`

include に足す。

```cpp
#include "MyEngine/Editor/Inspector/AssetInspector.h"
#include "MyEngine/Editor/Timeline/TimelineWindow.h"
```

```cpp
#include "MyEngine/Physics/PhysicMaterialManager.h"
#include "MyEngine/Timeline/TimelineManager.h"
```

`DrawCreateMenu` の Curve の下に足す。

```cpp
		ImGui::SetItemTooltip("今のフォルダに作ります（時間に対して値がどう変わるかのカーブ）");
		// Timeline
		if (ImGui::MenuItem("Timeline")) {
			CreateAssetFile("NewTimeline", TimelineManager::kFileExtension, &TimelineManager::CreateNewFile);
		}
		ImGui::SetItemTooltip("今のフォルダに作ります（時間に沿って Component の値を動かす。Timeline Component で再生する）");
```

右クリックのメニューの `.uiscreen` の分の下に足す。

```cpp
		ImGui::SetItemTooltip("停止中に、Scene ビューで画面を編集する（Hierarchy の Done で保存して閉じる）");
	} else if (ToUtf8(item.path).ends_with(TimelineManager::kFileExtension)) {
		if (ImGui::MenuItem("Open Timeline")) {
			TimelineWindow::Open(item.path);
		}
		ImGui::SetItemTooltip("Timeline ウィンドウで直す（ダブルクリックと同じ）");
	}
```

ダブルクリックの関数に足す。

```cpp
	// Timeline は Timeline ウィンドウで直す
	if (ToUtf8(item.path).ends_with(TimelineManager::kFileExtension)) {
		TimelineWindow::Open(item.path);
	}
```

### B-12 プロジェクトに足す

`Editor/Timeline` の6ファイルと `Editor/Inspector/TimelineAssetEditor.h`・`.cpp` を `MyEngine_v1` に足す。

### B-13 `Editor/Timeline/TimelineRows.cpp`（写経の後に見つけた直し）

トラックの行を右クリックして `Delete Track` を押すと落ちる。B-5 のコードは直してあるので、B-5 をこれから写すなら、この節はいらない。

**落ちる理由：** `DrawTrackRow` は、はじめに `TimelineTrack& track` を受け取って行を描く。途中の `DrawRowMenu` でトラックを消すと、`track` の指す先が解放される。その後の `FindTimelineBindingProblem` が解放済みの `track.target` を読んで落ちる。`DrawTimelineRows` の `count` も古いままなので、最後の行で範囲の外も読む。

**直し方：** 並べ替えの `moveFrom`・`moveTo` と同じく、回している間は「消す番号」を覚えるだけにして、回し終わってから消す。Delete キーは `HandleKeyboard` で回し終わった後に動くので、落ちない。

`DrawRowMenu` の宣言と `Delete Track` を直す。

```cpp
void DrawRowMenu(TimelineEditorState& state, int index, int& moveFrom, int& moveTo, int& deleteIndex) {
```

```cpp
	if (ImGui::MenuItem("Delete Track")) {
		deleteIndex = index; // 行を描いている途中なので、ここでは消さない
	}
```

`DrawTrackRow` の宣言と、`DrawRowMenu` を呼ぶ行を直す。

```cpp
void DrawTrackRow(TimelineEditorState& state, int index, const ImVec2& rowMin, int& moveFrom, int& moveTo, int& deleteIndex) {
```

```cpp
	DrawRowMenu(state, index, moveFrom, moveTo, deleteIndex);
```

`DrawTimelineRows` の一覧の部分を直す。

```cpp
	// --- 左の一覧（並べ替えと消すのは回し終わってから。回している間に消すと、track の参照と count が古くなる）---
	int moveFrom = -1;
	int moveTo = -1;
	int deleteIndex = -1;
	for (int i = 0; i < count; ++i) {
		DrawTrackRow(state, i, ImVec2(origin.x, origin.y + static_cast<float>(i) * kTimelineRowHeight), moveFrom, moveTo, deleteIndex);
	}
	if (moveFrom >= 0 && moveFrom != moveTo) {
		state.SelectTrack(TimelineEdit::MoveTrack(*state.GetTimeline(), moveFrom, moveTo));
		state.MarkEdited();
	}
	if (deleteIndex >= 0) {
		state.SelectTrack(deleteIndex);
		DeleteSelected(state);
	}
```

### B-14 下の詳細を見やすくする（写経の後の直し）

B-4・B-6 のコードの**後から**足す直し。B-4・B-6 と `CurveEditor` はこれから写す場合でも、ここを最後に当てる。

**見づらかった理由：**

- 詳細の高さを固定の値で持っていて、一覧が残りを全部使っていた。トラックが少なくても一覧の空きが場所を取る。
- 境目に線が無く、つまめることが分からなかった。高さは下限の 80 まで縮むと、そのまま残った。
- 1つの欄に結び付け・クリップ・キーを縦に並べていたので、キーの値がスクロールの下に隠れた。カーブの欄も、絵の下に数値の欄の分を取っていた。

**直し方：**

- 一覧は「トラックの数 + 1 行」の高さにして、残りを全部詳細に回す。境目で変えるのは一覧の高さの上限（`rowsLimit`）にする。
- 境目に線を引く。`rowsLimit` を書くのはつまんでいる間だけにして、ウィンドウが一瞬狭くなっても値が変わらないようにする。
- 詳細を「結び付け」「クリップとキー」「カーブ」の3列にする。
- カーブの欄が広いときは、数値の欄を絵の右に置く。

**`Editor/Timeline/TimelineWindow.cpp`**

定数の3つと、上の変数を直す（`detailsHeight` は消す）。

```cpp
constexpr float kMinDetailsHeight = 120.0f; // 詳細の高さの下限
constexpr float kSplitterHeight = 8.0f;     // 詳細との境目の高さ
```

```cpp
float rowsLimit = 180.0f;                 // トラックの一覧の高さの上限（境目をつまんで変える。トラックが少なければ低く出して、詳細に譲る）
```

`DrawSplitter` を置き換える。

```cpp
// ===== 詳細との境目（つまんで高さを変える）=====
void DrawSplitter(float rowsHeight, float maxRowsHeight) {
	ImGui::InvisibleButton("##splitter", ImVec2(ImGui::GetContentRegionAvail().x, kSplitterHeight));
	const bool isHovered = ImGui::IsItemHovered();
	const bool isActive = ImGui::IsItemActive();
	if (isHovered || isActive) {
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
	}
	if (isActive) {
		// 見えている高さから動かす（トラックが少なくて上限より低く出ていても、すぐ動く）
		rowsLimit = std::clamp(rowsHeight + ImGui::GetIO().MouseDelta.y, kMinRowsHeight, maxRowsHeight);
	}
	// --- つまめる場所だと分かるように線を引く ---
	const ImVec2 min = ImGui::GetItemRectMin();
	const ImVec2 max = ImGui::GetItemRectMax();
	const float lineY = (min.y + max.y) * 0.5f;
	const ImU32 lineColor = ImGui::GetColorU32(isActive ? ImGuiCol_SeparatorActive : (isHovered ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator));
	ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, lineY), ImVec2(max.x, lineY), lineColor, 2.0f);
}
```

`TimelineWindow::Draw` の `available` から `DrawTimelineDetails` までを置き換える。

```cpp
			// --- 一覧は行の分だけ（1行は空けて、足す場所を残す）。残りは全部、詳細に使う ---
			const float available = ImGui::GetContentRegionAvail().y;
			const float maxRowsHeight = (std::max)(available - kMinDetailsHeight - kTimelineScrollbarHeight - kSplitterHeight, kMinRowsHeight);
			const float fitHeight = static_cast<float>(timeline->tracks.size() + 1) * kTimelineRowHeight;
			const float rowsHeight = std::clamp((std::min)(rowsLimit, fitHeight), kMinRowsHeight, maxRowsHeight);
			DrawTimelineRows(state, rowsHeight);
			DrawTimelineScrollbar(state);
			DrawSplitter(rowsHeight, maxRowsHeight);
			DrawTimelineDetails(state, ImGui::GetContentRegionAvail().y);
```

**`Editor/Timeline/TimelineDetails.cpp`**

定数を直す（`kCurveFooterHeight` の値も変わる）。

```cpp
constexpr float kFieldsWidth = 280.0f;        // 項目の列（トラック・クリップ）1つの幅
```

```cpp
constexpr float kCurveFooterHeight = 120.0f;  // 欄が狭いとき、カーブの絵の下の数値の欄に残す高さ
constexpr float kCurveWideWidth = 640.0f;     // 欄の幅がこれ以上なら、数値の欄を絵の右に置く
constexpr float kCurveFieldsWidth = 280.0f;   // カーブの絵の右の数値の欄の幅
```

`DrawCurvePanel` の `canvasHeight` と `Draw` の呼び出しを置き換える。`if` の中はそのまま。

```cpp
	// 広ければ数値の欄を右に置いて、上の並びを除いた高さを全部カーブの絵に使う
	const ImVec2 available = ImGui::GetContentRegionAvail();
	const bool isWide = available.x >= kCurveWideWidth;
	const float footerHeight = isWide ? 0.0f : kCurveFooterHeight;
	const float canvasHeight = (std::max)(available.y - ImGui::GetFrameHeightWithSpacing() - footerHeight, kCurveMinHeight);
	if (state.curveEditor.Draw("curve", curves[state.selection.channel], canvasHeight, GetInspectorUI(), isWide ? kCurveFieldsWidth : 0.0f)) {
```

`DrawTimelineDetails` の `// --- 左：項目 ---` から最初の `ImGui::EndChild();` までを置き換える。

```cpp
	// --- 左：トラックの結び付け ---
	ImGui::BeginChild("##trackFields", ImVec2(kFieldsWidth, height), ImGuiChildFlags_Borders);
	ImGui::PushItemWidth(-kItemLabelWidth);
	DrawTrackFields(state, *track);
	ImGui::PopItemWidth();
	ImGui::EndChild();
	// --- 真ん中：クリップとキー（1列にまとめると、キーの値がスクロールの下へ隠れる）---
	ImGui::SameLine();
	ImGui::BeginChild("##clipFields", ImVec2(kFieldsWidth, height), ImGuiChildFlags_Borders);
	ImGui::PushItemWidth(-kItemLabelWidth);
	if (state.selection.clip >= 0) {
		DrawClipFields(state, *track);
	} else {
		ImGui::TextDisabled("クリップを選ぶと、ここで直せる");
	}
	ImGui::PopItemWidth();
	ImGui::EndChild();
```

**`Editor/Widgets/CurveEditor.h`**

`Draw` と `DrawCanvas` の宣言を直す。Inspector の `.curve` は `fieldsWidth` を渡さないので、今までどおり絵の下に数値が出る。

```cpp
	/// <param name="ui">選んでいるキーの数値を描く係</param>
	/// <param name="fieldsWidth">数値の欄を絵の右に置くときの幅（0 なら絵の下に置く）</param>
	/// <returns>このフレームで curve を変えたか</returns>
	bool Draw(const char* id, Curve& curve, float canvasHeight, ComponentUI& ui, float fieldsWidth = 0.0f);
```

```cpp
	void DrawCanvas(Curve& curve, float canvasWidth, float canvasHeight);
```

**`Editor/Widgets/CurveEditor.cpp`**

`Draw` の宣言と、`DrawCanvas`・`DrawKeyFields` を呼ぶ2行を直す。

```cpp
bool CurveEditor::Draw(const char* id, Curve& curve, float canvasHeight, ComponentUI& ui, float fieldsWidth) {
```

```cpp
	if (fieldsWidth > 0.0f) {
		// --- 絵の右に数値の欄 ---
		DrawCanvas(curve, ImGui::GetContentRegionAvail().x - fieldsWidth - ImGui::GetStyle().ItemSpacing.x, canvasHeight);
		ImGui::SameLine();
		ImGui::BeginChild("##keyFields", ImVec2(fieldsWidth, canvasHeight));
		DrawKeyFields(curve, ui);
		ImGui::EndChild();
	} else {
		DrawCanvas(curve, ImGui::GetContentRegionAvail().x, canvasHeight);
		DrawKeyFields(curve, ui);
	}
```

`DrawCanvas` の宣言と `canvasSize_` の行を直す。

```cpp
void CurveEditor::DrawCanvas(Curve& curve, float canvasWidth, float canvasHeight) {
```

```cpp
	canvasSize_ = ImVec2((std::max)(canvasWidth, 1.0f), (std::max)(canvasHeight, 1.0f));
```

## パートC 確認

### C-1 確認用のファイル

`Docs/Tasks/TimelineWindowTest/` に置いた。ゲームの `project` フォルダへ写す。

| ファイル | 写す先 |
|---|---|
| `TimelineWindowTest.scene.json` | `Resources/scenes/` |
| `Window.timeline` | `Resources/timelines/` |

### C-2 確認の手順

1. Debug をビルドして起動する。ログに `[timeline] self test OK (162 / 162)` が出れば、計算と値の戻し方の確認は通っている。
2. Project から `TimelineWindowTest` を開いて、Hierarchy の `Panel` を選ぶ。Timeline ウィンドウをドッキングする。
3. 下の表を順に試す。1.4 の手順で、`Title` の Image の `a` を消していく Timeline を作るのが最初の確認になる。

### C-3 確認項目

「写しで確認」は、一時フォルダの写しで実際に動かした結果。写経の後に同じ項目を確かめる。

**自動の確認（`TimelineSelfTest`）**

| # | 区分 | 確かめること | 写しで確認 |
|---|---|---|---|
| 1 | 値を読む | `GetValue`・`GetComponentNames`・`GetValueFields`・`MakePath` | 合う |
| 2 | 写して戻す | 回転を含めて 1000 回書いて戻す | 1ビットも変わらない |
| 3 | プレビュー | Update で書く・FinishPreview で戻る・頼まなければ書かない・Mute・Play 中・無効な Entity | 合う |
| 4 | トラック | 足す・消す・並べ替え、知らない種類、範囲の外 | 合う |
| 5 | クリップ | 時間の順に入る、追い越すと番号が変わる、負の時間と NaN、イベントの長さは 0 | 合う |
| 6 | キー | 同じ時間を1つに見せる、形を変えずに足す、まとめて動かす・消す | 合う |
| 7 | Undo の文字 | 文字で往復して同じ、壊れた文字は読まない | 合う |
| 8 | 時間の計算 | 拡大縮小の中心・上限下限・スナップ・目盛りの間隔 | 合う |

**コードを書かずに作る（Debug で ImGui へ入力を流した）**

| # | 確認すること | やり方 | 写しで確認 |
|---|---|---|---|
| 9 | 開く | `Panel` を選ぶ | ファイルと Entity の名前が出る |
| 10 | トラックを足す | Add Track | ファイルに書かれる |
| 11 | 対象を選ぶ | Target の一覧 | 子孫が出る |
| 12 | Component・項目を選ぶ | 一覧 | 持っている物だけ出る。名前が `Title.color` になる。合わない間は行が赤い |
| 13 | ドラッグで対象を選ぶ | Hierarchy の `Title` を行へ | Target が `Title` になる |
| 14 | クリップを足す | 何も無い所をダブルクリック | 0.1 秒にそろう。今の色で平らなキーが入る |
| 15 | キーの値を打つ | 右端のキーを選んで `a` を 0 | 値が変わる |
| 16 | 成分を外す | r・g・b のチェック | キーが消える。`a` のカーブが出る |
| 17 | 再生 | Play | Stop まで繰り返し動く（`a` が 1 → 0） |

**編集**

| # | 確認すること | やり方 | 写しで確認 |
|---|---|---|---|
| 18 | クリップを動かす | つまむ | 0.5 → 0.7。0.1 秒にそろう |
| 19 | スナップを外す | Ctrl を押して動かす | 0.5827 |
| 20 | 長さを変える | 右端の外をつまむ | 1.0 → 1.2。キーは動かない |
| 21 | キーを足す | クリップをダブルクリック | 形を変えずに増える |
| 22 | キーを動かす | つまむ | 0.5 → 0.6 |
| 23 | キー・クリップ・トラックを消す | Delete | 一番細かい物から消える |
| 24 | カーブで直す | カーブの絵のキーをつまむ | 値が変わる。Undo で戻る |
| 25 | イベント | 列をダブルクリック・Name を打つ | ファイルに書かれる |
| 26 | 並べ替え | 行をつまんで運ぶ | 順番が変わる |
| 27 | Mute | M | ファイルに書かれる |
| 28 | 右クリック | 列・行 | Add Clip / Add Key・Delete Clip / 行のメニューが出る。`Delete Track` で消えて、Undo・Redo が効く |
| 29 | 拡大縮小 | ホイール | マウスの下の時間が動かない。目盛りが細かくなる |
| 30 | スクロール | Shift + ホイール・スクロールバー・中ボタン | 横に動く |
| 31 | Fit | F | 全体が見える |
| 32 | Project から開く | `.timeline` をダブルクリック | ウィンドウに出る。再生する Entity が無ければプレビューできない |
| 33 | 知らない種類 | `type` を書き換えたファイルを開く | 消さずに個数を出す |

**プレビュー**

| # | 確認すること | やり方 | 写しで確認 |
|---|---|---|---|
| 34 | 再生位置をつまむ | 目盛りを 1.0・1.5 秒へ | 絵の `a` が 0.5・0 になる |
| 35 | 値を変えるとすぐ見える | プレビュー中にキーを直す | 次のフレームに出る |
| 36 | ▶ ⏸ ■ | ウィンドウのボタン | 動く・止まる。■ で元の絵 |
| 37 | Inspector | プレビュー中に見る | 元の値のまま（ImGui の段階で読むと `a` = 1） |
| 38 | Undo に混ざらない | プレビューの前後 | 履歴の回数が変わらない |
| 39 | Save に混ざらない | `a` = 0 の絵のまま Save | ファイルの `a` は 1.0 |
| 40 | Play に混ざらない | プレビューのまま Play → Stop | 元の値へ戻る |
| 41 | Inspector で打つ | プレビュー中に Transform を直す | 打った値が保存される（プレビューの x = 200 は入らない） |
| 42 | 隠す | タイトルの ▼ で畳む | 絵が元に戻る。開くとプレビューへ戻る |
| 43 | Entity を消す | Hierarchy で消す | プレビューが切れる。Undo で結び付きが戻る |
| 44 | シーンを開く | 別のシーン | プレビューが切れる。「再生する Entity なし」 |
| 45 | 画面の編集に入る | `.uiscreen` をダブルクリック | プレビューが切れる。画面の Timeline に切り替わる |
| 46 | 画面の編集中のプレビュー | 目盛りをつまんで Done | ファイルの値は変わらない（違いは Entity の番号だけ） |

**Play 中**

| # | 確認すること | やり方 | 写しで確認 |
|---|---|---|---|
| 47 | 時間を見る | Play | 再生位置が動く |
| 48 | ⏸ ▶ | ウィンドウのボタン | 止まる・続きから |
| 49 | シーク | 目盛りをつまむ | 0.5 の値になる |

**ImGui の ID**

| # | 確認すること | やり方 | 写しで確認 |
|---|---|---|---|
| 50 | ウィンドウ全体 | マウスで全部の場所をなぞる（横 14・縦 7 ピクセルごと。同じ名前のトラック3本、クリップとキーを選んだ状態） | 衝突 0 |
| 51 | 対象の一覧 | 同じ名前の兄弟がある一覧をなぞる | 衝突 0 |
| 52 | 検出が働くか | 直す前のコード | 成分とキーの値の `a` の衝突を検出した |

**ビルド**

| # | 確認すること | やり方 | 写しで確認 |
|---|---|---|---|
| 53 | 3 構成のビルド | Debug・Development・Release | ã¨ã©ã¼ 0ãä»åã³ã³ãã¤ã«ãããã¡ã¤ã«ã®è­¦åã¯ 0 |
| 54 | 3 構成の自己テスト | Debug の起動。Development・Release は一時的に呼ぶように変えて起動 | ã©ãã 162 / 162 |
| 55 | パートA の後 | パートB のファイルと行を除いた状態 | ãã«ãã¨èµ·åãéããèªå·±ãã¹ãã¯ 162 / 162 |
| 56 | Release の再生 | ウィンドウで作ったファイルをゲームで再生 | æéã§ `Title` ã®è²ãå¤ããï¼ç»é¢ã®ãã¯ã»ã«ã§ç¢ºãããï¼ |

### C-4 検証の結果

- **写し：** AL4_01 の形の一時フォルダで、エンジンを `047a7c3` に合わせて確かめた。md の新しいファイルのコードは、そのフォルダでコンパイルしたファイルから埋め込んだ。
- **入力：** 本物のマウスではなく、ImGui へ入力を流して確かめた。
- **確認の中で見つけて直した物：**
  - 成分の `a` とキーの値の `a` の ID が衝突した（1.6）。
  - Mute を押すと `isMuted` が変わって、`PushStyleColor` と `PopStyleColor` の数が合わず assert が出た。押す前の値を覚えるようにした。
  - 終わりにキーがあるクリップの右端をつかめなかった。右端のすぐ外をつかめるようにした。
  - 右クリックのメニューが開いてすぐ閉じた。ImGui は押したフレームに開いた物を閉じるので、離したときに開くようにした。
  - 画面の編集に入ると Timeline が開かなかった。Component は選ばれた次のフレームに付くので、付くまで覚えないようにした。
  - 選んでいる成分のチェックを外すとカーブが出なかった。キーのある成分へ移るようにした。
  - 写経の後の確認で、行の `Delete Track` を押すと落ちた（B-13）。最初の確認はメニューが出るところまでで、押していなかった。
  - 写経の後の確認で、下の詳細とカーブの絵が狭くて見づらかった（B-14）。ドッキングした 1920 × 514 と、狭くした浮いたウィンドウの絵で確かめた。

### C-5 未確認のこと・気を付けること

- **本物のマウス：** ドラッグのしやすさ、Ctrl を押したままのホイールなど、手の感触は確かめていない。
- **Hierarchy のフォルダ：** フォルダの子をドラッグしたときは試していない。
- **AL4_01 の実物：** 写しのゲームは `main.cpp` とシーンが違う。エンジンのコードは同じ。
- **Create → Timeline：** 同じコードを UI 段階3 で確かめた。今回は動かしていない。
- **`.timeline` の壊れたファイル：** Inspector の `CanLoad` は試していない。
- **Undo の範囲：** Play・Stop・シーンの切り替えで履歴は全部消える（今までと同じ）。画面の編集を出ると、編集中に積んだ `.timeline` の履歴も消える。どちらもファイルは直したまま。
- **Play 中の編集：** `.timeline` はファイルへ書かれるので、Stop しても残る。
- **同じファイルを再生する Entity：** 全部に同じ編集が効く。
- **プレビューと Scene ビューのギズモ：** ギズモは元の位置に出る。絵だけがプレビューの位置になる。
- **ゲーム側のトラック：** `CanPreview` を true にしない物はプレビューに出ない。
- **Image の Inspector の色：** プレビュー中の絵と Inspector の値は違って見える（0.3）。
