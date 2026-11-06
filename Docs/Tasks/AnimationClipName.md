参考：
- Unity の Animator.Play（ステートの名前で流す）：https://docs.unity3d.com/ScriptReference/Animator.Play.html
- glTF 2.0 の animations（name は任意。並びは書き出したソフトが決める）：https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#reference-animation
- Blender の glTF の書き出し（アニメーション）：https://docs.blender.org/manual/en/latest/addons/import_export/scene_gltf2.html#animation
- `std::optional`（値が無いこともある箱）：https://en.cppreference.com/w/cpp/utility/optional
- `std::ranges::find` の3つ目の引数（射影）：https://en.cppreference.com/w/cpp/algorithm/ranges/find
- `std::unordered_set::insert` の戻り値（入ったか）：https://en.cppreference.com/w/cpp/container/unordered_set/insert
- RAII（作ったときに変えて、消えるときに戻す）：https://en.cppreference.com/w/cpp/language/raii
- 前の md（アニメを番号で流す・待ちの部品）：Docs/Tasks/IdlePart.md

# アニメを名前で選ぶ（AnimationClipName）

Animator・Script・攻撃部品が、アニメーション（クリップ）を「ファイルの中の何番目か」ではなく**名前**で選ぶようにする。Blender でアクションを足す・消す・並べ替えても、同じ名前なら同じアニメが流れる。

- **パートA 名前で探す**（AnimationManager）：`FindAnimationIndex(handle, name)`・`FindAnimation(handle, name)`。名前の無いアニメ・同じ名前のアニメを読み込みのときに警告
- **パートB 名前で持つ・名前で流す**（Animator・ComponentUI・Inspector・AnimationSystem）：Animator の `clip` を名前（`AnimationClipName`）にする。`ui.ClipField` と `ClipSourceScope`。`AnimationSystem::Play(entity, "Run", …)`・`GetClipDuration`・`CalcSpeedToFit` を名前に。Sandbox の攻撃部品2つも名前に
- **パートC 攻撃表の見本のファイル**：攻撃表の Settings に **Sample Animation**。部品の Clip の欄に、そのファイルの中の名前を並べる
- **パートD Sandbox で確かめる**：クリップが3つのモデルと、並べ替えて1つ抜いたモデル、名前で順に流す Script（ClipCycler）、テストシーン ClipNameTest

パートは **A → B → C → D の順に写す**。A だけでビルドできる。**B は名前を変える所があるので、B を全部写してから1回ビルドする**（途中ではビルドが通らない）。C・D はそれぞれ写したらビルドできる。

ブランチは `feature/AnimationClipName`（develop 24e4d2b から）。

写しで確かめたこと：3構成（Debug・Development・Release）のビルドで自分のファイルの警告 0。自動テスト 54 項目（名前で探す 12・流す 23・保存と読み込み 6・ClipSourceScope 5・攻撃表 8）が Debug・Release とも合格。テストシーンを再生して見る 6 項目も合格。Inspector の Clip の欄（無い名前の赤・Script の欄・攻撃表の欄）も撮って確かめた。AL4_01 の PlayerController の直し（2章）は、写しの AL4_01 で Debug のビルドが通ることまで確かめた（Blender の作業の前なので、動きはまだ見ていない）。

## 0章 全体の形

### 0.1 何が困っていたか

シーン・Prefab の JSON は、もう名前で保存していた（`"clip": "00_Idle"`）。Inspector も、ファイルの中の名前の一覧から選ぶ形だった。困っていたのは、その外側。

- **Component の中は番号のまま**：保存・読み込みのときだけ名前と番号を直していたので、読み込んだ後は番号で動く。Animation のファイルを差し替えると、番号はそのままで別のアニメになる
- **名前が無いと、何も言わずに 0 番になる**：警告は出るが、次に保存すると 0 番の名前で上書きされ、元の名前が消える
- **Script の窓口が番号**：`AnimationSystem::Play(entity, 8, …)`・`CalcSpeedToFit(entity, 8, …)`。AL4_01 の PlayerController は `kClipDodgeJump = 8` と番号を書いている
- **攻撃部品の値も番号**：ChaseTarget・SpinAttack は `.attacktable` に `"clipIndex": 0` を数で保存している。攻撃表はモデルを知らないので、名前の一覧も出せない
- そのため AL4_01 の Player.gltf は、アクションの名前の頭に番号（00_Idle〜08_DodgeJump）を付けて並びを固定し、使わない 02_Dash も消せずに残していた

### 0.2 作る物・直す物

| パート | ファイル | すること |
|---|---|---|
| A | `Graphics/Animation/AnimationManager.h`・`.cpp` | `FindAnimationIndex`・`FindAnimation` を足す。名前の無いアニメに「#番号」の名前を付けて警告、同じ名前が2つあれば警告 |
| B | `Components/Animation/AnimatorComponent.h` | `clipIndex` → `clip`（`AnimationClipName` ＝ `FixedString<128>`） |
| B | `ECS/ComponentUI.h`・`.cpp` | 番号の `ChoiceField` を消して `ClipField` と `ClipSourceScope`。Inspector の Clip の欄（無い名前は赤） |
| B | `Editor/Windows/InspectorWindow.cpp` | Entity の Component を描くとき、Clip の候補をその Entity の Animator のファイルにする |
| B | `Systems/Animation/AnimationSystem.h`・`.cpp` | 名前で探して流す。`Play`・`GetClipDuration`（新しい）・`CalcSpeedToFit` を名前に。無い名前は1回だけ警告 |
| B | `Sandbox/Source/AttackParts/ChaseTarget.cpp`・`SpinAttack.cpp` | 番号の値を名前（`ClipField`）に |
| C | `AI/AttackTableManager.h`・`.cpp` | `AttackTable::sampleAnimation`（Settings の Sample Animation） |
| C | `Editor/Inspector/AttackTableEditor.cpp` | 部品の値を描くとき、Clip の候補を Sample Animation にする |
| C | `Sandbox/Resources/AttackTables/IdlePartTest.attacktable` | `"clipIndex": 0` → `"clip": "animation_AnimatedCube"`、Sample Animation |
| D | `Sandbox/Resources/Model/ClipNames/ClipNames.gltf`・`ClipNamesReordered.gltf`（新しい） | テスト用のモデル（中のデータも1ファイルに入っている） |
| D | `Sandbox/Source/Scripts/ClipCycler.h`・`.cpp`（新しい）・`Sandbox.vcxproj`・`.filters` | 名前で順に流す Script |
| D | `Sandbox/Resources/scenes/ClipNameTest.scene.json`（新しい） | テストシーン |

### 0.3 使い方

**Script・攻撃部品から**は、名前で流す。毎フレーム呼んでよい。

```cpp
AnimationSystem::Play(self, "Run", true, 1.0f);                         // 同じ名前が流れていれば、速さと loop だけ変わる
const float speed = AnimationSystem::CalcSpeedToFit(self, "Swing", 0.8f); // 0.8 秒でちょうど流しきる速さ
AnimationSystem::Play(self, "Swing", false, speed);
const float seconds = AnimationSystem::GetClipDuration(self, "Swing");    // 長さ（秒）。無ければ 0
```

名前は Animator のファイルの中から探す。無い名前を渡すと `Play` は false、`GetClipDuration` は 0、`CalcSpeedToFit` は 1 を返し、ログに警告を出す（同じファイル・同じ名前は1回だけ）。

**値として持つ**とき（Script・攻撃部品の Fields）は `AnimationClipName` と `ui.ClipField` を使う。

```cpp
AnimationClipName attackClip_ = "Swing"; // メンバ変数（Component にも入れられる、決まった長さの文字）

void Fields(ComponentUI& ui) override {
	ui.ClipField("Attack Clip###attackClip", attackClip_); // Inspector では名前の一覧から選ぶ
}
// 使うとき
AnimationSystem::Play(gameObject.handle, attackClip_.View(), false);
```

Clip の欄の候補は、描く側が決める。

| どこで描くか | 候補 |
|---|---|
| Entity の Inspector（Script の値など） | その Entity の Animator のファイル（Animator が無ければ、名前を打つ欄） |
| Animator の Clip | その Animator のファイル |
| 攻撃表の部品の値 | 攻撃表の Settings の **Sample Animation**（未選択なら、名前を打つ欄） |

### 0.4 決めたこと

| 項目 | 決めたこと | 理由 |
|---|---|---|
| Component に持たせる物 | **名前だけ**（`AnimationClipName clip`）。番号は AnimationSystem の実行中の状態だけが持ち、名前かファイルが変わったときに引き直す | Undo・コピー・Prefab・ファイルの差し替えが全部名前で動く。名前と番号の両方を持つと、書く所を全部そろえないとずれる |
| 名前の長さ | **`FixedString<128>`**（127 バイトまで） | Component は memcpy で写せる型しか持てない（ARCHITECTURE.md 原則2）。Blender のアクション名は 63 バイトまで。FBX は「Armature｜Idle」の形になるので、その倍 |
| シーン・Prefab の保存 | **キーは今と同じ `"clip"`、値は名前** | 今のシーン・Prefab はそのまま読める（直す所なし） |
| 名前が見つからないとき | **名前を消さずに持ち、流さない**（モデルに入っている形で描く）。Inspector では「(無い) 名前」を赤で出し、ログに1回警告 | 先頭のアニメに落とすと、気付かないうちに別のアニメになり、保存で元の名前が消える。assert は、アニメーターがファイルを直しただけでエディタが止まる。Blender で名前を戻せば、そのまま直る |
| Script の窓口 | **名前だけ**。番号の `Play`・`CalcSpeedToFit` は消す。長さを取る `GetClipDuration` を足す | 番号の窓口を残すと、また番号を直書きする人が出る。番号は `AnimationPlayer`（低い層）にだけ残す |
| 攻撃部品の値 | 名前で持ち、攻撃表の **Sample Animation の中の名前から選ぶ**（表に1つ） | 攻撃表はモデルを知らない。打ち間違いは実行するまで分からないので、一覧から選べるようにする。流すときは敵の Animator のファイルから同じ名前を探す |
| 候補の決め方 | **`ClipSourceScope`**（描く側が「この中は、このファイルの名前」と決める） | 何を描いているかを知っているのは描く側（Inspector・攻撃表）。部品・Script はファイルを知らなくても `ClipField` と書くだけで済む。Script の値にも同じ欄が使える（1.2） |
| 名前の比べ方 | **完全一致**（大文字・小文字も区別） | 一覧から選ぶので、あいまいに比べる必要がない。「Run」と「run」が別のアニメのファイルでも迷わない |
| 名前の無いアニメ | 読み込みのときに **「#番号」の名前を付けて**警告 | 名前で選べないと使えない。番号の名前は並びが変わるとずれるので、名前を付けるように警告する |
| 同じ名前が2つ | **先の方**を使い、読み込みのときに警告 | 名前で選ぶと後の方は選べないので、気付けるようにする |
| ファイルを選んだとき | Clip が空なら、**先頭のアニメの名前を入れる** | 今までどおり、Animator にファイルを選ぶだけで動いて見える |
| 無い名前の警告 | **（ファイル, 名前）ごとに1回** | Script は毎フレーム Play を呼ぶので、毎回出すとログが埋まる |
| 番号の `ChoiceField` | **消す**（使っていたのは Animator だけ） | 番号で持って名前で保存する欄は、この問題の元なので残さない |

### 0.5 保存の形

シーン・Prefab の Animator は**今と同じ**。

```json
"Animator": {
	"enabled": true,
	"animation": "resources/Model/ClipNames/ClipNames.gltf",
	"clip": "Bounce",
	"speed": 1.0,
	"loop": true,
	"playOnStart": true,
	"previewTime": 0.0
}
```

攻撃表は Settings に `sampleAnimation`（パス）が増え、部品の値は `"clipIndex": 0` が `"clip": "名前"` に変わる。前の形のファイル（`clipIndex`）を読むと、「読む所が無い項目」の警告が出て Clip は空になる（今あるのは Sandbox の IdlePartTest だけなので、C-4 で書き直す）。

```json
"settings": {
	"maxRepeat": 0,
	"sampleAnimation": "MyEngine/Resources/Model/AnimatedCube/AnimatedCube.gltf"
},
…
"idlePartFields": {
	"clip": "animation_AnimatedCube"
}
```

### 0.6 今回やらないこと

- **名前を付け直したことに気付く**：Blender で「Run」→「Sprint」にしたら、シーンの Animator は「(無い) Run」になる。選び直すか、シーンの JSON を直す（2章の AL4_01 の手順と同じ）
- **切り替えの混ぜ合わせ**（クロスフェード）：今までどおり、名前が変わるとすぐ次のアニメの 0 秒の形になる
- **FBX の「Armature｜」を外して比べる**：今は書き出された名前そのまま（一覧から選ぶので困らない）
- **Timeline でアニメを流すトラック**

## 1章 解説

### 1.1 名前を正にして、番号は実行中だけ持つ

Component には名前だけを入れ、ファイルの中の何番目かは AnimationSystem の実行中の状態（`AnimatorState`）が持つ `AnimationPlayer` の中にある。

```
AnimatorComponent { animationHandle, clip = "Run" }   ← 保存・Undo・コピー・Prefab はこれ
        │ 名前かファイルが変わったら（毎フレーム比べる）
        ▼
AnimatorState { animationHandle, clip = "Run", player }  ← 実行中だけ
        │ ResetPlayer：FindClip で名前 → 番号（無ければ警告して空の player）
        ▼
AnimationPlayer.Play(handle, 3, loop)                  ← 番号はここだけ
```

- 毎フレームの「変わったか」は `component.clip != animator->clip`（`FixedString` どうしの比べ。長さを比べてから中身なので、変わっていなければすぐ終わる）
- 名前で探す（`FindAnimationIndex`）のは、変わったときと `Play`・`GetClipDuration` のときだけ。クリップは数個〜数十個なので、上から順に比べても十分速い
- 無い名前は、空の `AnimationPlayer` にする。ノードの行列は「モデルに入っている形」になる（番号が範囲の外だったときと同じ）

### 1.2 ClipSourceScope（描く側が候補を決める）

`ClipField` は「アニメーションの名前の欄」というだけで、どのファイルの名前を並べるかを知らない。並べるファイルは、描く側が `ClipSourceScope` で決める。

```cpp
{
	const ClipSourceScope clipSource(ui, table.sampleAnimation); // ここから下の ClipField は、このファイルの名前を並べる
	part->Fields(ui);                                             // 部品は ui.ClipField("Clip", clip_) と書くだけ
} // ← ここで clipSource が消えて、前の値に戻る
```

- **RAII**：変数を作ったとき（コンストラクタ）に値を変え、`}` で消えるとき（デストラクタ）に戻す。途中で `return` しても必ず戻るので、戻し忘れが起きない
- 入れ子にしてよい（前の値を覚えて戻す）。Inspector は Entity 全体を「その Entity の Animator のファイル」で囲み、その中で Animator の欄は自分のファイルで囲み直す
- 保存・読み込みの係（JsonWriterUI など）は候補を使わない。`ClipField` は文字の項目（`Field(label, std::string&)`）として書く・読む
- 候補を `ClipField` の引数にすると、攻撃部品・Script が「どのファイルか」を知らないといけない。攻撃部品は表のファイルを知らないし、Script も自分の Animator を探す処理を毎回書くことになる

### 1.3 あまり使わない STL

- **`std::optional<uint32_t>`**：「番号か、無いか」を1つで返す箱。`has_value()` で入っているか、`*index` で中身。無いことを `-1` や `UINT32_MAX` で表すと、うっかりそのまま配列の番号に使ってしまう。`std::nullopt` が「無い」
- **`std::ranges::find(file->animations, name, &Animation::name)`**：3つ目は「射影」。並びの1つ1つから `name` メンバを取り出して、それを `name` と比べる。比べ方の関数（ラムダ）を書かなくてよい
- **`std::unordered_set::insert(x).second`**・**`std::set::emplace(…).second`**：入れた結果の2つ目が「新しく入ったか」（もう入っていたら false）。「初めて見た名前のときだけ警告する」を1行で書ける
- **`std::set<std::pair<uint32_t, std::string>>`**：（ファイルの番号, 名前）の組を並べて持つ。`pair` は1つ目、同じなら2つ目の順に比べられるので、そのまま `set` に入れられる

### 1.4 名前の無い・同じ名前のアニメ

glTF の `name` は書かなくてもよい。名前が無いと assimp は空の名前を返すので、読み込みのときに「#番号」（`#1` など）の名前を付ける。番号の名前は並びが変わると別のアニメを指すので、警告で名前を付けるように知らせる。

同じ名前が2つあるファイルでは、名前で探すと先の方になる（後の方は選べない）。これも読み込みのときに警告する。どちらの警告も、ファイルを読んだときに1回だけ出る（同じファイルは2回読まない）。

## パートA 名前で探す（AnimationManager）

### A-1 `Graphics/Animation/AnimationManager.h`

include を足す。

```cpp
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <filesystem>
#include <string_view>
#include <unordered_map>
```

`GetAnimation` の下に足す。

```cpp
	static const Animation* GetAnimation(uint32_t handle, uint32_t index = 0);

	/// <summary>
	/// 名前でアニメーションを探す（Blender でアクションを足す・消す・並べ替えても、同じ名前なら同じアニメーション）
	/// </summary>
	/// <param name="handle">Load が返したハンドル</param>
	/// <param name="name">アニメーションの名前（Blender ならアクションの名前）。大文字・小文字も区別する</param>
	/// <returns>ファイルの中の何番目か。無ければ空（std::nullopt）。同じ名前が2つあれば先の方。警告は出さない（出すのは呼ぶ側）</returns>
	static std::optional<uint32_t> FindAnimationIndex(uint32_t handle, std::string_view name);
	// 名前で探したアニメーション（無ければ nullptr。警告は出さない）
	static const Animation* FindAnimation(uint32_t handle, std::string_view name);
```

### A-2 `Graphics/Animation/AnimationManager.cpp`

include を足す。

```cpp
#include <format>
#include <algorithm>
#include <unordered_set>
```

`LoadAnimationFile` の中、名前を入れる所の下に足す（名前の無いアニメ）。

```cpp
		animation.name = source->mName.C_Str();
		// 名前の無いアニメーションは名前で選べないので、番号から名前を作る（「#2」）。並びが変わると別のアニメになるので警告する
		if (animation.name.empty()) {
			animation.name = std::format("#{}", animationIndex);
			LogManager::Warning(std::format("名前の無いアニメーションがあります \"{}\"（{} 番目）。「{}」で選べますが、並びが変わると別のアニメになるので、名前を付けてください", utf8Path, animationIndex, animation.name));
		}
		animation.duration = static_cast<float>(source->mDuration / ticksPerSecond);
```

`LoadAnimationFile` の最後、ループと `return animations;` の間に足す（同じ名前。最初の2行と最後の2行は今からある行）。

```cpp
		animations.push_back(std::move(animation));
	}

	// --- 同じ名前が2つあると、名前で選ぶときに後の方を選べない（先の方になる）---
	// unordered_set::insert は、もう入っていたら .second が false
	std::unordered_set<std::string_view> names;
	for (const Animation& animation : animations) {
		if (!names.insert(animation.name).second) {
			LogManager::Warning(std::format("同じ名前のアニメーションが2つあります \"{}\": 「{}」（名前で選ぶと先の方になります）", utf8Path, animation.name));
		}
	}
	return animations;
}
```

「取得」の所、`GetAnimationCount` の上に足す。

```cpp
std::optional<uint32_t> AnimationManager::FindAnimationIndex(uint32_t handle, std::string_view name) {
	const AnimationFile* file = Find(handle);
	if (file == nullptr) {
		return std::nullopt;
	}
	// ranges::find の3つ目（&Animation::name）は「何と比べるか」。各アニメーションの name を name と比べる
	const auto it = std::ranges::find(file->animations, name, &Animation::name);
	if (it == file->animations.end()) {
		return std::nullopt;
	}
	return static_cast<uint32_t>(it - file->animations.begin());
}

const Animation* AnimationManager::FindAnimation(uint32_t handle, std::string_view name) {
	const std::optional<uint32_t> index = FindAnimationIndex(handle, name);
	return index.has_value() ? &Find(handle)->animations[*index] : nullptr;
}

uint32_t AnimationManager::GetAnimationCount(uint32_t handle) {
```

（最後の行は今からある行）

ここまででビルドできる（まだ誰も呼ばない）。

## パートB 名前で持つ・名前で流す

**B-1〜B-7 を全部写してから1回ビルドする**（`clipIndex` と番号の `Play` を消すので、途中ではビルドが通らない）。

### B-1 `Components/Animation/AnimatorComponent.h`（全体を差し替え）

```cpp
#pragma once
#include <cstdint>
#include <type_traits>

#include "MyEngine/String/FixedString.h"

// アニメーション（クリップ）の名前。Blender ならアクションの名前（63 バイトまで）
// FBX の「Armature|Idle」（アーマチュアの名前｜アクションの名前）の形も入るように、127 バイトまでにする
using AnimationClipName = FixedString<128>;

// アニメーションを再生するためのデータ。Entityに付けるComponent（カテゴリ: Animation）
// 同じEntityのModel Rendererのノードを動かす（Model Rendererが無ければ何もしない）
// 今の時刻・ノードの行列（AnimationPlayer）は持たない。AnimationSystem が持つ
// ここに入れると、コピー・Undo・保存で丸ごと写されてしまう

struct AnimatorComponent {
	uint32_t animationHandle = 0; // AnimationManager::Loadの番号。保存はパス
	AnimationClipName clip;       // 流すアニメーションの名前（空なら流さない）。番号ではなく名前で持つので、ファイルの中で足す・消す・並べ替えても同じアニメが流れる
	float speed = 1.0f;           // 1で等速、2で倍速、負で逆再生
	bool loop = true;             // ループするか
	bool playOnStart = true;      // Playを押したとき（シーンが始まったとき）に再生する
	float previewTime = 0.0f;     // 停止中（Editing）に見せる時刻（秒）。Inspectorでつまんでポーズを確かめる
	bool enabled = true;          // false なら動かさない（Model Renderer はモデルに入っている形で描く）
};

static_assert(std::is_trivially_copyable_v<AnimatorComponent>);
```

`AnimationClipName` は、Animator のほかに Script・攻撃部品の値にも使う型なので、Animator と同じヘッダに置く（`AnimationSystem.h` を include すれば使える）。

### B-2 `ECS/ComponentUI.h`

`ChoiceField`（番号で持つ欄）を消して、その場所に `ClipField` を書く。

```cpp
	// ===== アニメーションのクリップの名前（持つのも保存も名前）=====
	// Inspector では、ClipSourceScope で決めたアニメーションのファイルの中の名前から選ぶ（決めていなければ、名前を打つ欄）
	// 候補に無い名前も消さずに持つ（Inspector では赤で出す。Blender で名前を戻せば、そのまま直る）
	// 保存・読み込みの係は、文字の項目と同じに扱う（候補は使わない）
	virtual void ClipField(const char* label, std::string& clip, FieldStyle style = {}) { Field(label, clip, style); }
	// 決まった長さまでの文字で持つ版（データの Component・攻撃部品の値）
	template<size_t N>
	void ClipField(const char* label, FixedString<N>& clip, FieldStyle style = {}) {
		std::string text = clip.ToString();
		ClipField(label, text, style);
		// 変わったときだけ書き戻す
		if (clip != text) {
			clip = text;
		}
	}
	// 今、クリップの候補を出しているアニメーションのファイル（AnimationManager の番号。0 なら決めていない）
	uint32_t GetClipSource() const { return clipSource_; }
```

クラスの最後（`EnumField` の下）に `clipSource_` を足し、クラスの後ろに `ClipSourceScope` を足す。

```cpp
	virtual void EnumField(const char* label, size_t& index, std::span<const std::string_view> names, FieldStyle style) = 0;

private:
	friend class ClipSourceScope;
	uint32_t clipSource_ = 0; // ClipField の候補を出すアニメーションのファイル（ClipSourceScope だけが変える）
};

/// <summary>
/// この中で描く ClipField の候補を、そのアニメーションのファイルの中の名前にする（抜けると前の値に戻る）
/// <para>Animator は自分のファイル、攻撃表は見本のファイル（Sample Animation）を渡す。Script の値などを描く側が決める</para>
/// <para>何を描いているかを知っているのは描く側なので、候補は ClipField の呼び手ではなく、ここで決める（攻撃部品はファイルを知らない）</para>
/// </summary>
class ClipSourceScope {
public:
	ClipSourceScope(ComponentUI& ui, uint32_t animationHandle) : ui_(ui), previous_(ui.clipSource_) { ui.clipSource_ = animationHandle; }
	~ClipSourceScope() { ui_.clipSource_ = previous_; }
	// 写すと、2回戻してしまうので禁止
	ClipSourceScope(const ClipSourceScope&) = delete;
	ClipSourceScope& operator=(const ClipSourceScope&) = delete;

private:
	ComponentUI& ui_;   // 候補を変えた UI
	uint32_t previous_; // 入る前の候補（入れ子にしても戻せるように）
};

// Inspectorに描く係（エンジンが1つだけ持っている。ゲームからは触らない）
ComponentUI& GetInspectorUI();
```

- `ClipField` の FixedString 版は、`Field` の FixedString 版と同じ形（`std::string` に写して、文字の版に任せ、変わったときだけ書き戻す）
- `clipSource_` は private で、`ClipSourceScope` だけが `friend` で変える（ほかから勝手に変えて戻し忘れることがない）

### B-3 `ECS/ComponentUI.cpp`

`MakeMaskPreview` の下（`InspectorComponentUI` の上）に色を足す。

```cpp
// ===== クリップの名前の欄 =====
constexpr ImU32 kMissingClipColor = IM_COL32(255, 96, 96, 255); // ファイルに無い名前の色（攻撃表の知らない部品と同じ赤）
```

`InspectorComponentUI` の `ColorField` の上に足す。

```cpp
	// アニメーションのクリップの名前。ClipSourceScope で決めたファイルの中の名前から選ぶ
	// ファイルが決まっていない（Sample Animation が未選択など）なら、名前を打つ欄にする
	void ClipField(const char* label, std::string& clip, FieldStyle style) override {
		const uint32_t source = GetClipSource();
		const uint32_t count = AnimationManager::GetAnimationCount(source); // 未選択なら 0
		if (count == 0) {
			Field(label, clip, style);
			return;
		}
		// --- 欄（ファイルに無い名前は、消さずに赤で出す）---
		const bool isMissing = !clip.empty() && !AnimationManager::FindAnimationIndex(source, clip).has_value();
		const std::string preview = clip.empty() ? "(none)" : (isMissing ? std::format("(無い) {}", clip) : clip);
		if (isMissing) {
			ImGui::PushStyleColor(ImGuiCol_Text, kMissingClipColor);
		}
		const bool isOpen = ImGui::BeginCombo(label, preview.c_str());
		if (isMissing) {
			ImGui::PopStyleColor(); // 赤は欄の文字だけ（一覧の中は、いつもの色）
		}
		if (isOpen) {
			// --- 一覧（一番上に、今の無い名前。選び直すまで、そのまま持つ）---
			if (isMissing) {
				ImGui::TextColored(ImColor(kMissingClipColor), "(無い) %s", clip.c_str());
				ImGui::Separator();
			}
			if (ImGui::Selectable("(none)", clip.empty())) {
				clip.clear();
			}
			for (uint32_t index = 0; index < count; ++index) {
				const Animation* animation = AnimationManager::GetAnimation(source, index);
				const bool isSelected = (animation->name == clip);
				ImGui::PushID(static_cast<int>(index)); // 同じ名前のアニメーションが2つあっても、別の行として押せるように
				if (ImGui::Selectable(animation->name.c_str(), isSelected)) {
					clip = animation->name;
				}
				ImGui::SetItemTooltip("%.2f 秒", animation->duration);
				if (isSelected) {
					ImGui::SetItemDefaultFocus(); // 開いたときに今の選択へスクロールする
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		if (isMissing) {
			ImGui::SetItemTooltip("「%s」という名前のアニメーションが %s にありません（流さずに、モデルに入っている形で描きます）。名前を戻すか、選び直してください", clip.c_str(),
			                      AnimationManager::GetPath(source).c_str());
		} else {
			DrawTooltip(style);
		}
	}

	void ColorField(const char* label, Vector3& rgb, FieldStyle style) override {
```

（最後の行は今からある行）

- 赤くするのは欄の文字と、右の項目の名前（Clip）だけ（`BeginCombo` の直後に戻す）。一覧の中は、今の無い名前の行だけを赤で出す
- `PushID(index)`：ImGui は表示の文字で行を見分けるので、同じ名前が2つあると片方が押せなくなる。番号で枠を分ける
- 保存・読み込みなどの係（ComponentSerializer.cpp）と Release の空の係は、何も足さなくてよい（`ClipField` の元の形が文字の項目に任せる）

### B-4 `Editor/Windows/InspectorWindow.cpp`

include を足す（自作のヘッダの所）。

```cpp
#include "MyEngine/Components/Animation/AnimatorComponent.h"
```

`Draw` の中、Component を並べる所を囲む。

```cpp
				// Script の値などの Clip の欄（ClipField）は、この Entity の Animator のファイルの中の名前から選ぶ
				// Animator の欄は、自分のファイルで決め直す（Animator が無ければ、名前を打つ欄）
				const AnimatorComponent* animator = EntityManager::Get<AnimatorComponent>(handle);
				const ClipSourceScope clipSource(GetInspectorUI(), (animator != nullptr) ? animator->animationHandle : 0);
				for (const std::unique_ptr<ComponentEditor>& editor : ComponentEditorRegistry::GetAll()) {
```

`clipSource` は `else { … }` の中の変数なので、Component を全部描き終わって `}` を抜けると元に戻る。

### B-5 `Systems/Animation/AnimationSystem.h`

include を足す。

```cpp
#include <vector>
#include <string_view>
```

「ゲームから使う」の頭から `CalcSpeedToFit` までを差し替える（`Play`・`CalcSpeedToFit` が名前に、`GetClipDuration` が増える）。

```cpp
	// ===== ゲームから使う =====
	// Script・攻撃部品から呼ぶ。どれも、呼んだフレームの Update（Script・攻撃表の AI より後に回る）から効く
	// 流し始めたフレームは時刻を進めないので、そのフレームはアニメの 0 秒の形が見える
	// アニメーションは名前で選ぶ（Blender ならアクションの名前。ファイルの中で並びが変わっても、同じアニメが流れる）
	// 名前が Animator のファイルに無いときは、ログに警告を出す（同じファイル・同じ名前は1回だけ）

	/// <summary>
	/// アニメーションを名前で流す（毎フレーム呼んでよい）。Animator の Clip・Loop・Speed をこの値に書き換える
	/// <para>名前が違う・止まっている（ループしないアニメが最後まで行った・Stop した）→ そのアニメを最初から</para>
	/// <para>同じ名前が流れている → 速さと loop だけ変える（最初には戻らない。途中で最初からにしたいときは Restart）</para>
	/// </summary>
	/// <param name="clip">アニメーションの名前（Animator の Clip と同じ名前）</param>
	/// <param name="loop">ループするか</param>
	/// <param name="speed">1 で等速（CalcSpeedToFit で「N 秒で流しきる」速さを作れる）</param>
	/// <returns>流れているか（Animator が無い・その名前のアニメが無い・Entity が無効なら false）</returns>
	static bool Play(Handle<Entity> entity, std::string_view clip, bool loop, float speed = 1.0f);
	// 今のアニメーション（Animator の Clip）を最初から流し直す。Animator が無い・アニメーションが未選択・ファイルに無いなら false
	static bool Restart(Handle<Entity> entity);
	// そのアニメーションの長さ（秒）。Animator・アニメが無ければ 0
	static float GetClipDuration(Handle<Entity> entity, std::string_view clip);
	// そのアニメーションを、ちょうど seconds 秒で流しきる速さ（長さ ÷ seconds）。Animator・アニメが無い・seconds が 0 以下なら 1
	static float CalcSpeedToFit(Handle<Entity> entity, std::string_view clip, float seconds);
```

### B-6 `Systems/Animation/AnimationSystem.cpp`

include（`<set>`・`<utility>`・`<optional>`・`LogManager.h` が増える）。

```cpp
#include <set>
#include <cfloat>
#include <format>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <optional>
#include <algorithm>
#include <string_view>

#include "MyEngine/Components/Core/TransformComponent.h"  
#include "MyEngine/Components/Rendering3D/ModelRendererComponent.h"
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Diagnostics/MyAssert.h"
```

`AnimatorState`（`clipIndex` → `clip`）と `AnimationState`（`warnedClips` が増える）。

```cpp
struct AnimatorState {
	Handle<Entity> entity;        // 持ち主
	uint32_t animationHandle = 0; // 再生しているアニメーションの番号
	AnimationClipName clip;       // 再生しているアニメーションの名前（Component の Clip が変わったかを見る）
	AnimationPlayer player;       // 今の時刻・ノードの行列
	bool isJustStarted = false;   // 流し始めたばかり（次の Update は時刻を進めず、0 秒の形を見せる）
};

/// <summary>
/// AnimationSystem が持つもの（Initializeで作り、Releaseで消す）
/// </summary>
struct AnimationState {
	std::vector<AnimatorState> animators; // 見つけたAnimatorの一覧。止まっていても持つ
	bool wasGameRunning = false;          // ゲームが動いているとして一覧を持っているか（Play / Stop の切り替わりで、一覧を捨てる。SwitchGameRunning）
	std::set<std::pair<uint32_t, std::string>> warnedClips; // 「ファイルに無い」と警告した（ファイルの番号, 名前）。同じ物は1回だけ出す（毎フレーム出さない）

	// --- 骨のギズモ ---
	Renderer::LineListConfig gizmoLines; // ギズモの線の置き場（配列のメモリを使い回す）
	std::vector<Vector3> nodePositions;  // ノードごとのワールドの位置（1体分）
	bool showGizmos = true;              // 線を出すか
};
std::unique_ptr<AnimationState> state;
```

`HasClip` を消して `FindClip` にし、`ResetPlayer` の頭を直す（`if (!play) {` から下は今のまま）。

```cpp
// ===== 名前から、ファイルの中の何番目かを探す =====
// ファイルに無い名前なら、ログに警告して空を返す（同じファイル・同じ名前の警告は1回だけ）
// ファイル・名前が未選択（0・空）なら、警告せずに空を返す
std::optional<uint32_t> FindClip(uint32_t animationHandle, std::string_view clip) {
	if (animationHandle == 0 || clip.empty()) {
		return std::nullopt;
	}
	const std::optional<uint32_t> index = AnimationManager::FindAnimationIndex(animationHandle, clip);
	// set::emplace は、もう入っていたら .second が false（＝前に警告した）
	if (!index.has_value() && state->warnedClips.emplace(animationHandle, std::string(clip)).second) {
		LogManager::Warning(std::format("アニメーション「{}」が {} にありません（流さずに、モデルに入っている形で描きます）", clip, AnimationManager::GetPath(animationHandle)));
	}
	return index;
}

// ===== Componentで選んでいるアニメーションを最初の時刻から持ち直す =====
// AnimationSystem::Restart（ゲームから呼ぶ方）と名前がぶつかるので、こちらは ResetPlayer
void ResetPlayer(AnimatorState& animator, const AnimatorComponent& component, bool play) {
	animator.animationHandle = component.animationHandle;
	animator.clip = component.clip;
	animator.isJustStarted = false;
	// 未選択・ファイルに無い名前: 空の AnimationPlayerにする（ノードの行列が空 → モデルに入っている形で描く）
	// 名前は Component に残るので、Blender で名前を戻す・選び直すと、また流れる
	const std::optional<uint32_t> index = FindClip(component.animationHandle, component.clip.View());
	if (!index.has_value()) {
		animator.player = AnimationPlayer{};
		return;
	}
	animator.player.Play(component.animationHandle, *index, component.loop);
```

`DescribeAnimator` の Animation の欄から長さの表示までを差し替える（Enabled の欄と、Speed の欄から下は今のまま）。

```cpp
	const uint32_t previousAnimation = value.animationHandle;
	ui.AssetField("Animation###animation", value.animationHandle, AssetType::Animation, Tip("resources の下の .gltf / .glb / .fbx（アニメーションが入っている物）"));
	// ファイルを選んだとき、まだ Clip を選んでいなければ、先頭のアニメーションを選んでおく（選び直さなくても、すぐ動いて見える）
	// 読み込みのときも通るが、すぐ下の Clip の欄が JSON の名前で上書きする（JSON に "clip" が無いときだけ、先頭の名前が残る）
	if (value.animationHandle != previousAnimation && value.clip.Empty() && AnimationManager::GetAnimationCount(value.animationHandle) > 0) {
		value.clip = AnimationManager::GetAnimation(value.animationHandle, 0)->name;
	}

	// --- クリップ：名前で持つ・名前で保存する（候補はこの Animator のファイルの中の名前）---
	// ファイルを差し替えても名前はそのまま。新しいファイルに同じ名前があれば、そのアニメが流れる
	{
		const ClipSourceScope clipSource(ui, value.animationHandle);
		ui.ClipField("Clip###clip", value.clip, Tip("流すアニメーションの名前（Blender ならアクションの名前）。名前で持つので、ファイルの中で並びが変わっても同じアニメが流れる"));
	}

	// --- 選んでいるアニメーションの長さ（飾りなので保存されない） ---
	const Animation* clip = AnimationManager::FindAnimation(value.animationHandle, value.clip.View());
	if (clip != nullptr) {
		const std::string info = std::format("{:.2f} 秒 ／ このファイルに {} 個", clip->duration, AnimationManager::GetAnimationCount(value.animationHandle));
		ui.Label(info.c_str());
	}

	ui.Field("Speed###speed", value.speed, Drag(0.01f).Tip("1で等速、2で倍速、負で逆再生"));
```

`Update` の中、選び直したかを見る所。

```cpp
		} else if (component.animationHandle != animator->animationHandle || component.clip != animator->clip) {
```

`Play` を全部差し替える。

```cpp
// ===== 名前で流す =====
bool AnimationSystem::Play(Handle<Entity> entity, std::string_view clip, bool loop, float speed) {
	MY_ASSERT_MSG(state != nullptr, "AnimationSystem::Initialize()を先に呼んでください");
	AnimatorComponent* component = EntityManager::Get<AnimatorComponent>(entity);
	if (component == nullptr) {
		return false; // Animator が無い（RequestAdd した直後は、次のフレームまで付いていない）
	}
	// --- Component に書く ---
	// Update は毎フレーム、Component の Speed・Loop を AnimationPlayer へ写し、Clip が変わっていたら最初から流す。ここに書かないと Update に戻されてしまう
	component->clip = clip;
	component->loop = loop;
	component->speed = speed;
	if (!EntityManager::IsActiveInHierarchy(entity)) {
		return false; // 無効な Entity（値だけ書いておく）
	}
	// --- 同じアニメが流れているなら、ここまで（速さと loop は Update が写す）---
	// 一覧を見る前に、ゲームが動いている側へ切り替えておく（理由は Restart と同じ）
	SwitchGameRunning(true);
	const AnimatorState* animator = FindAnimator(entity);
	if (animator != nullptr && animator->animationHandle == component->animationHandle && animator->clip == component->clip && animator->player.IsPlaying()) {
		return true;
	}
	// --- 名前が違う・止まっている：最初から（ファイルに無い名前なら、Restart が警告して false）---
	return Restart(entity);
}
```

（`Restart` は今のまま）`CalcSpeedToFit` を消して、この2つにする。

```cpp
// ===== アニメーションの長さ =====
float AnimationSystem::GetClipDuration(Handle<Entity> entity, std::string_view clip) {
	MY_ASSERT_MSG(state != nullptr, "AnimationSystem::Initialize()を先に呼んでください");
	const AnimatorComponent* component = EntityManager::Get<AnimatorComponent>(entity);
	if (component == nullptr) {
		return 0.0f;
	}
	// 名前は、この Entity の Animator のファイルから探す（ファイルに無ければ警告して 0）
	const std::optional<uint32_t> index = FindClip(component->animationHandle, clip);
	return index.has_value() ? AnimationManager::GetAnimation(component->animationHandle, *index)->duration : 0.0f;
}

// ===== N 秒で流しきる速さ =====
float AnimationSystem::CalcSpeedToFit(Handle<Entity> entity, std::string_view clip, float seconds) {
	const float duration = GetClipDuration(entity, clip);
	// 長さ 0 のアニメ（1枚の形だけ）・アニメが無いときは、どの速さでも同じなので等速
	return (seconds > 0.0f && duration > 0.0f) ? duration / seconds : 1.0f;
}

// ===== 止めて最初の時刻に戻す =====
```

（最後の行は今からある行）

- `FindClip`：ファイル・名前が未選択（0・空）なら警告しない（Animator を足したばかり・Clip を (none) にした）
- `DescribeAnimator`：読み込みのときは、AssetField がファイルを読んだ後で Clip の欄が JSON の名前を読むので、先頭の名前を入れても上書きされる。JSON に `"clip"` が無い（ファイルだけ書いた）ときだけ先頭の名前が残る
- `GetClipDuration`・`CalcSpeedToFit` も `FindClip` を通るので、Script の打ち間違いは長さを取っただけでも警告に出る

### B-7 Sandbox の攻撃部品（全体を差し替え）

`Sandbox/Source/AttackParts/SpinAttack.cpp`

```cpp
#include "MyEngine/AI/AttackPart.h"
#include "MyEngine/Systems/Animation/AnimationSystem.h"


/// <summary>
/// 予備動作の間に、アニメをちょうど1回流しきる（AnimationSystem::CalcSpeedToFit の確かめ。Sandbox のテスト用）
/// <para>同じ Entity に Animator を付ける。動きは付けない（攻撃の間、敵が止まっているかを見る）</para>
/// </summary>
class SpinAttack : public AttackPart {
public:
	void OnStage(const AttackPartContext& context) override;
	void Fields(ComponentUI& ui) override;

private:
	AnimationClipName clip_; // 流すアニメの名前（攻撃表の Sample Animation の中の名前から選ぶ）
};
ATTACK_PART(SpinAttack, "予備動作の間に、アニメをちょうど1回流しきる（Sandbox のテスト用）");


//=============================================================================
// 段階に入った
//=============================================================================
void SpinAttack::OnStage(const AttackPartContext& context) {
	if (context.stage != AttackStage::Windup) {
		return;
	}
	// 予備動作の長さで、ちょうど1回（ループしない）
	const Handle<Entity> self = context.self.handle;
	AnimationSystem::Play(self, clip_.View(), false, AnimationSystem::CalcSpeedToFit(self, clip_.View(), context.stageDuration));
	// 待ちの部品が同じ名前のアニメを流していると、Play は速さと loop だけ変える（最初には戻らない）。最初から流したいので Restart
	AnimationSystem::Restart(self);
}

//=============================================================================
// 攻撃表に出す値
//=============================================================================
void SpinAttack::Fields(ComponentUI& ui) { ui.ClipField("Clip###clip", clip_, Tip("流すアニメの名前（敵の Animator のファイルに、同じ名前のアニメーションが要る）")); }
```

`Sandbox/Source/AttackParts/ChaseTarget.cpp`

```cpp
#include "MyEngine/AI/AttackPart.h"
#include "MyEngine/Components/Physics/CharacterControllerComponent.h"
#include "MyEngine/Math/MathUtility.h"
#include "MyEngine/Systems/Animation/AnimationSystem.h"


/// <summary>
/// 相手の方を向き、遠ければ Character Controller で近づき、近ければ止まる（Sandbox のテスト用。攻撃表のフェーズの Idle Part に選ぶ）
/// <para>同じ Entity に Character Controller と Animator を付ける。アニメは歩いている間は等速、止まっている間はゆっくり流す</para>
/// </summary>
class ChaseTarget : public AttackPart {
public:
	void OnUpdate(const AttackPartContext& context) override;
	void OnEnd(const AttackPartContext& context, AttackPartEnd reason) override;
	void Fields(ComponentUI& ui) override;

private:
	float moveSpeed_ = 3.0f;       // 近づく速さ（m/s）
	float stopDistance_ = 2.5f;    // 相手とこの距離より近ければ止まる（m。地面に平らに測る）
	float turnSmoothTime_ = 0.15f; // 向きを合わせる速さ（秒。小さいほど速く向く）
	AnimationClipName clip_;       // 流すアニメの名前（攻撃表の Sample Animation の中の名前から選ぶ）
	float stopAnimSpeed_ = 0.25f;  // 止まっている間のアニメの速さ
};
ATTACK_PART(ChaseTarget, "相手の方を向き、遠ければ Character Controller で近づく（Sandbox のテスト用。Idle Part に選ぶ）");


//=============================================================================
// 毎フレーム
//=============================================================================
void ChaseTarget::OnUpdate(const AttackPartContext& context) {
	CharacterControllerComponent* controller = context.self.GetComponent<CharacterControllerComponent>();
	if (controller == nullptr || !context.target) {
		return; // Character Controller が無い・相手が居ない
	}

	// --- 相手への向き（地面に平ら）---
	TransformComponent& transform = context.self.transform;
	Vector3 toTarget = GetWorldPosition(context.target.transform) - GetWorldPosition(transform);
	toTarget.y = 0.0f;
	const float distance = Length(toTarget);

	// --- 向く：今の角度から一番近い回り方で、なめらかに（フレームの速さに左右されない）---
	if (distance > 0.0f) {
		const float yaw = GetEulerAngles(transform).y;
		const float targetYaw = MathUtility::DirectionToYaw(toTarget);
		const float rate = MathUtility::CalcSmoothRate(context.deltaTime, turnSmoothTime_);
		SetEulerAngles(transform, {0.0f, yaw + MathUtility::DeltaAngle(yaw, targetYaw) * rate, 0.0f});
	}

	// --- 近づく・止まる（X・Z だけ書く。Y は Character Controller の重力に任せる）---
	const bool isMoving = (distance > stopDistance_);
	const Vector3 velocity = isMoving ? Normalize(toTarget) * moveSpeed_ : Vector3{0.0f, 0.0f, 0.0f};
	controller->velocity.x = velocity.x;
	controller->velocity.z = velocity.z;

	// --- アニメ：同じ名前を毎フレーム渡しても、最初には戻らない（速さだけ変わる）---
	AnimationSystem::Play(context.self.handle, clip_.View(), true, isMoving ? 1.0f : stopAnimSpeed_);
}

//=============================================================================
// 終わり
//=============================================================================
// 攻撃を選んだときは、その攻撃の OnBegin より先に呼ばれる。ここで速さを消すので、攻撃が始まったフレームに歩きの速さが残らない
void ChaseTarget::OnEnd(const AttackPartContext& context, AttackPartEnd /*reason*/) {
	if (CharacterControllerComponent* controller = context.self.GetComponent<CharacterControllerComponent>()) {
		controller->velocity.x = 0.0f;
		controller->velocity.z = 0.0f;
	}
}

//=============================================================================
// 攻撃表に出す値
//=============================================================================
void ChaseTarget::Fields(ComponentUI& ui) {
	ui.Field("Move Speed###moveSpeed", moveSpeed_, AtLeast(0.0f).Tip("近づく速さ（m/s）"));
	ui.Field("Stop Distance###stopDistance", stopDistance_, AtLeast(0.0f).Tip("相手とこの距離より近ければ止まる（m。地面に平らに測る）"));
	ui.Field("Turn Smooth Time###turnSmoothTime", turnSmoothTime_, AtLeast(0.0f).Tip("向きを合わせる速さ（秒。小さいほど速く向く。0 ならすぐ向く）"));
	ui.ClipField("Clip###clip", clip_, Tip("流すアニメの名前（敵の Animator のファイルに、同じ名前のアニメーションが要る）"));
	ui.Field("Stop Anim Speed###stopAnimSpeed", stopAnimSpeed_, AtLeast(0.0f).Tip("止まっている間のアニメの速さ（歩いている間は 1）"));
}
```

ここで3構成ビルドできる。攻撃表の部品の Clip の欄は、まだ候補が無いので名前を打つ欄（パートC で一覧になる）。IdlePartTest.attacktable はまだ `clipIndex` なので、読むと警告が出て Clip が空になる（C-4 で直す）。

## パートC 攻撃表の見本のファイル

### C-1 `AI/AttackTableManager.h`

`AttackTable` に `sampleAnimation` を足す。

```cpp
struct AttackTable {
	int maxRepeat = 2;            // 同じ攻撃を続けてよい回数（これを超える攻撃は、ほかに使える攻撃がある間は選ばない。0 なら制限なし）
	uint32_t sampleAnimation = 0; // 部品の Clip の候補を出すためだけのアニメーションのファイル（AnimationManager の番号。保存はパス）。実際に流すのは敵の Animator のファイル
	std::vector<AttackPhaseData> phases = {
	    {"Phase 1", 1.0f, 0.0f}
    };
	std::vector<AttackData> attacks;

	bool operator==(const AttackTable&) const = default;
```

### C-2 `AI/AttackTableManager.cpp`

`DescribeTable` に Sample Animation の欄を足す。

```cpp
void AttackTableManager::DescribeTable(ComponentUI& ui, AttackTable& value) {
	ui.Field("Max Repeat###maxRepeat", value.maxRepeat, AtLeast(0.0f).Tip("同じ攻撃を続けてよい回数。これを超える攻撃は、ほかに使える攻撃がある間は選ばない（0 なら制限なし）"));
	ui.AssetField("Sample Animation###sampleAnimation", value.sampleAnimation, AssetType::Animation,
	              Tip("部品の Clip の欄に、このファイルの中のアニメーションの名前を並べる（選ぶためだけ。実際に流すのは、敵の Animator のファイルの同じ名前のアニメーション）"));
}
```

- `DescribeTable` はファイルの読み書きにも使うので、Sample Animation はパスで保存される。読み込むと、そのファイルのアニメも読み込む（敵の Animator と同じファイルなら、同じ番号が返るだけ）
- Sample Animation は候補を出すためだけ。実行中に流すときは、敵の Animator のファイルから同じ名前を探す（`AnimationSystem::Play`）

### C-3 `Editor/Inspector/AttackTableEditor.cpp`

`DrawPartFields` に引数を足し、部品の `Fields` を `ClipSourceScope` で囲む。

```cpp
// label は警告に出す名前（攻撃の名前・フェーズの名前）
// sampleAnimation は表の Sample Animation（部品の Clip の欄に、このファイルの中の名前を並べる）
void DrawPartFields(ComponentUI& ui, AttackPartSlot& slot, std::string_view label, uint32_t sampleAnimation) {
	std::unique_ptr<AttackPart> part = AttackPartRegistry::Create(slot.name);
	if (part == nullptr) {
		return;
	}
	std::set<std::string> warnings;
	const auto describe = [&part](ComponentUI& fields) { part->Fields(fields); };
	ComponentSerializer::ReadFields(label, describe, SceneJson::parse(slot.fields, nullptr, false), warnings);
	// 部品の欄の名前は部品が決めるので、表の欄（Name など）とぶつからないように ID の枠を分ける
	ImGui::PushID("partFields");
	{
		// 部品は表のファイルを知らないので、Clip の候補はここで決める（ClipSourceScope を抜けると元に戻る）
		const ClipSourceScope clipSource(ui, sampleAnimation);
		part->Fields(ui);
	}
	ImGui::PopID();
```

呼んでいる2か所に `table.sampleAnimation` を足す。

```cpp
		DrawPartFields(ui, phase.idlePart, phase.name, table.sampleAnimation);
```

```cpp
			DrawPartFields(ui, attack.part, attack.name, table.sampleAnimation);
```

`ReadFields`・`WriteFields`（保存の係）は囲まない。候補は Inspector に描くときだけ要る。

### C-4 `Sandbox/Resources/AttackTables/IdlePartTest.attacktable`（全体を差し替え）

```json
{
	"settings": {
		"maxRepeat": 0,
		"sampleAnimation": "MyEngine/Resources/Model/AnimatedCube/AnimatedCube.gltf"
	},
	"phases": [
		{
			"name": "Phase 1",
			"minIdleSeconds": 1.0,
			"idlePart": "ChaseTarget",
			"idlePartFields": {
				"moveSpeed": 3.0,
				"stopDistance": 2.5,
				"turnSmoothTime": 0.15,
				"clip": "animation_AnimatedCube",
				"stopAnimSpeed": 0.25
			}
		},
		{
			"name": "Phase 2",
			"healthThreshold": 0.5,
			"transitionSeconds": 1.0,
			"minIdleSeconds": 0.3,
			"idlePart": "ChaseTarget",
			"idlePartFields": {
				"moveSpeed": 6.0,
				"stopDistance": 2.5,
				"turnSmoothTime": 0.05,
				"clip": "animation_AnimatedCube",
				"stopAnimSpeed": 0.5
			}
		}
	],
	"attacks": [
		{
			"name": "Spin",
			"weight": 1.0,
			"minDistance": 0.0,
			"maxDistance": 3.5,
			"cooldown": 0.0,
			"windupSeconds": 0.8,
			"activeSeconds": 0.3,
			"recoverySeconds": 0.6,
			"phase0": true,
			"phase1": true,
			"part": "SpinAttack",
			"partFields": {
				"clip": "animation_AnimatedCube"
			}
		}
	]
}
```

## パートD Sandbox で確かめる

### D-1 `Sandbox/Resources/Model/ClipNames/ClipNames.gltf`（新しい）

箱1つに、アニメーションが3つ入ったモデル。並びは **Spin（Y で回る 2 秒）→ Bounce（上に跳ねる 1 秒）→ Grow（大きくなる 0.5 秒）**。箱とアニメのデータ（AnimatedCube.bin の中身に、跳ねる・大きくなるキーを足した物）を、最後の `uri` に base64 の文字で入れてあるので、ファイル1つで読める。

```json
{
	"asset": {"version": "2.0", "generator": "MyEngine Sandbox (hand written)"},
	"scene": 0,
	"scenes": [{"nodes": [0]}],
	"nodes": [{"name": "ClipNames", "mesh": 0}],
	"meshes": [{"name": "ClipNames", "primitives": [{"attributes": {"POSITION": 3, "NORMAL": 4, "TEXCOORD_0": 5}, "indices": 2, "material": 0}]}],
	"materials": [{"name": "ClipNames", "pbrMetallicRoughness": {"baseColorFactor": [0.9, 0.6, 0.2, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.6}}],
	"animations": [
		{"name": "Spin", "channels": [{"sampler": 0, "target": {"node": 0, "path": "rotation"}}], "samplers": [{"input": 0, "output": 1, "interpolation": "LINEAR"}]},
		{"name": "Bounce", "channels": [{"sampler": 0, "target": {"node": 0, "path": "translation"}}], "samplers": [{"input": 6, "output": 7, "interpolation": "LINEAR"}]},
		{"name": "Grow", "channels": [{"sampler": 0, "target": {"node": 0, "path": "scale"}}], "samplers": [{"input": 8, "output": 9, "interpolation": "LINEAR"}]}
	],
	"accessors": [
		{"bufferView": 0, "componentType": 5126, "count": 3, "type": "SCALAR", "min": [0.0], "max": [2.0]},
		{"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC4"},
		{"bufferView": 2, "componentType": 5123, "count": 36, "type": "SCALAR"},
		{"bufferView": 3, "componentType": 5126, "count": 36, "type": "VEC3", "min": [-1.0, -1.0, -1.0], "max": [1.0, 1.0, 1.000001]},
		{"bufferView": 4, "componentType": 5126, "count": 36, "type": "VEC3"},
		{"bufferView": 5, "componentType": 5126, "count": 36, "type": "VEC2"},
		{"bufferView": 6, "componentType": 5126, "count": 3, "type": "SCALAR", "min": [0.0], "max": [1.0]},
		{"bufferView": 7, "componentType": 5126, "count": 3, "type": "VEC3"},
		{"bufferView": 8, "componentType": 5126, "count": 3, "type": "SCALAR", "min": [0.0], "max": [0.5]},
		{"bufferView": 9, "componentType": 5126, "count": 3, "type": "VEC3"}
	],
	"bufferViews": [
		{"buffer": 0, "byteOffset": 0, "byteLength": 12},
		{"buffer": 0, "byteOffset": 12, "byteLength": 48},
		{"buffer": 0, "byteOffset": 60, "byteLength": 72, "target": 34963},
		{"buffer": 0, "byteOffset": 132, "byteLength": 432, "target": 34962},
		{"buffer": 0, "byteOffset": 564, "byteLength": 432, "target": 34962},
		{"buffer": 0, "byteOffset": 1572, "byteLength": 288, "target": 34962},
		{"buffer": 0, "byteOffset": 1860, "byteLength": 12},
		{"buffer": 0, "byteOffset": 1872, "byteLength": 36},
		{"buffer": 0, "byteOffset": 1908, "byteLength": 12},
		{"buffer": 0, "byteOffset": 1920, "byteLength": 36}
	],
	"buffers": [{"byteLength": 1956, "uri": "data:application/octet-stream;base64,AAAAAAAAgD8AAABAAAAAAAAAAAAAAAAAAACAPwAAAAAAAIA/AAAAAC69O7MAAAAALr27swAAAAAAAIC/AAABAAIAAwAEAAUABgAHAAgACQAKAAsADAANAA4ADwAQABEAEgATABQAFQAWABcAGAAZABoAGwAcAB0AHgAfACAAIQAiACMAAACAPwAAgL8AAIA/AACAvwAAgL8AAIC/AACAPwAAgL8AAIC/AACAvwAAgD8AAIC/7/9/PwAAgD8IAIA/AACAPwAAgD/v/3+/AACAPwAAgD/v/3+/AACAPwAAgL8AAIA/AACAPwAAgL8AAIC/7/9/PwAAgD8IAIA/AACAvwAAgL8AAIA/AACAPwAAgL8AAIA/AACAvwAAgL8AAIA/AACAvwAAgD8AAIC/AACAvwAAgL8AAIC/AACAPwAAgL8AAIC/AACAvwAAgD8AAIC/AACAPwAAgD/v/3+/AACAPwAAgL8AAIA/AACAvwAAgL8AAIA/AACAvwAAgL8AAIC/AACAvwAAgD8AAIC/AACAvwAAgD8AAIA/7/9/PwAAgD8IAIA/AACAPwAAgD/v/3+/7/9/PwAAgD8IAIA/AACAPwAAgL8AAIA/7/9/PwAAgD8IAIA/AACAvwAAgD8AAIA/AACAvwAAgL8AAIA/AACAvwAAgL8AAIA/AACAvwAAgD8AAIA/AACAvwAAgD8AAIC/AACAPwAAgL8AAIC/AACAvwAAgL8AAIC/AACAvwAAgD8AAIC/AAAAgAAAgL8AAAAAAAAAgAAAgL8AAAAAAAAAgAAAgL8AAAAAAAAAAAAAgD8AAACAAAAAAAAAgD8AAACAAAAAAAAAgD8AAACAAACAPwAAAIAAAACAAACAPwAAAIAAAACAAACAPwAAAIAAAACAAAAAgAAAAIAAAIA/AAAAgAAAAIAAAIA/AAAAgAAAAIAAAIA/AACAvwAAAIAAAACAAACAvwAAAIAAAACAAACAvwAAAIAAAACAAAAAAAAAAAAAAIC/AAAAAAAAAAAAAIC/AAAAAAAAAAAAAIC/AAAAAAAAgL8AAAAAAAAAAAAAgL8AAAAAAAAAAAAAgL8AAAAAAAAAAAAAgD8AAAAAAAAAAAAAgD8AAAAAAAAAAAAAgD8AAAAAAACAPwAAAAC9N4Y1AACAPwAAAAC9N4Y1AACAPwAAAAC9N4Y1AAAAgAAAAAAAAIA/AAAAgAAAAAAAAIA/AAAAgAAAAAAAAIA/AACAvwAAAIAAAACAAACAvwAAAIAAAACAAACAvwAAAIAAAACAAAAAAAAAAAAAAIC/AAAAAAAAAAAAAIC/AAAAAAAAAAAAAIC/AACAPwAAAIAAAACAAACAvwAAgD8AAACAAAAAgAAAgL8AAIA/AAAAgAAAAIAAAIC/AACAPwAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAgD8AAIA/AAAAgAAAAAAAAIA/AAAAAAAAAAAAAIC/AACAPwAAAAAAAAAAAACAvwAAgD8AAAAAAAAAAAAAgL8AAIA/AACAPwAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAgD8AAIA/AAAAAAAAAAAAAIA/AAAAAAAAAIAAAIC/AACAvwAAAAAAAACAAACAvwAAgL8AAAAAAAAAgAAAgL8AAIC/AACAPwAAAIAAAAAAAACAvwAAgD8AAACAAAAAAAAAgL8AAIA/AAAAgAAAAAAAAIC/AACAPwAAAIAAAACAAACAvwAAgD8AAAAAAAAAgAAAgL8AAIA/AAAAgAAAAIAAAIC/AACAPwAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAgD8AAIA/AAAAAAAAAAAAAIA/vTeGNQAAAAAAAIC/AACAP703hjUAAAAAAACAvwAAgD+9N4Y1AAAAAAAAgL8AAIA/AACAPwAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAgD8AAIA/AAAAAAAAAAAAAIA/AAAAAAAAAIAAAIC/AACAvwAAAAAAAACAAACAvwAAgL8AAAAAAAAAgAAAgL8AAIC/AACAPwAAAIAAAAAAAACAvwAAgD8AAACAAAAAAAAAgL8AAIA/AAAAgAAAAAAAAIC/AAAAAAAAAAAAAIC/AACAPwAAAAAAAIA/AAAAAAAAAAAAAIA/AACAvwAAgD8AAACAAACAPwAAAAAAAAAAAACAvwAAgD8AAIC/AACAPwAAAAAAAACAAACAvwAAgD8AAIC/AAAAAAAAAAAAAIA/AACAPwAAgD8AAAAAAAAAAAAAAAAAAIC/AACAPwAAAAAAAIA/AAAAAAAAAAAAAIC/AAAAAAAAgL8AAIA/AAAAAAAAAAAAAACAAACAvwAAgD8AAIC/AACAPwAAAAAAAACAAAAAAAAAAAAAAIC/AACAPwAAAAAAAACAAAAAAAAAAIAAAIC/AAAAAAAAAAAAAAAAAACAPwAAgD8AAIA/AAAAAAAAAAAAAIC/AAAAAAAAgL8AAIA/AAAAAAAAAD8AAIA/AAAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAAAAAAAAAAAAgD4AAAA/AACAPwAAgD8AAIA/AADAPwAAwD8AAMA/AACAPwAAgD8AAIA/"}]
}
```

### D-2 `Sandbox/Resources/Model/ClipNames/ClipNamesReordered.gltf`（新しい）

D-1 と同じ箱で、アニメーションの並びを変えて Bounce を抜いた物。**Grow → 名前なし（Bounce の動き）→ Spin → Spin（同じ名前。カクカク回る）**。並び替え・無い名前・名前の無いアニメ・同じ名前を確かめる。

```json
{
	"asset": {"version": "2.0", "generator": "MyEngine Sandbox (hand written)"},
	"scene": 0,
	"scenes": [{"nodes": [0]}],
	"nodes": [{"name": "ClipNames", "mesh": 0}],
	"meshes": [{"name": "ClipNames", "primitives": [{"attributes": {"POSITION": 3, "NORMAL": 4, "TEXCOORD_0": 5}, "indices": 2, "material": 0}]}],
	"materials": [{"name": "ClipNames", "pbrMetallicRoughness": {"baseColorFactor": [0.9, 0.6, 0.2, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.6}}],
	"animations": [
		{"name": "Grow", "channels": [{"sampler": 0, "target": {"node": 0, "path": "scale"}}], "samplers": [{"input": 8, "output": 9, "interpolation": "LINEAR"}]},
		{"channels": [{"sampler": 0, "target": {"node": 0, "path": "translation"}}], "samplers": [{"input": 6, "output": 7, "interpolation": "LINEAR"}]},
		{"name": "Spin", "channels": [{"sampler": 0, "target": {"node": 0, "path": "rotation"}}], "samplers": [{"input": 0, "output": 1, "interpolation": "LINEAR"}]},
		{"name": "Spin", "channels": [{"sampler": 0, "target": {"node": 0, "path": "rotation"}}], "samplers": [{"input": 0, "output": 1, "interpolation": "STEP"}]}
	],
	"accessors": [
		{"bufferView": 0, "componentType": 5126, "count": 3, "type": "SCALAR", "min": [0.0], "max": [2.0]},
		{"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC4"},
		{"bufferView": 2, "componentType": 5123, "count": 36, "type": "SCALAR"},
		{"bufferView": 3, "componentType": 5126, "count": 36, "type": "VEC3", "min": [-1.0, -1.0, -1.0], "max": [1.0, 1.0, 1.000001]},
		{"bufferView": 4, "componentType": 5126, "count": 36, "type": "VEC3"},
		{"bufferView": 5, "componentType": 5126, "count": 36, "type": "VEC2"},
		{"bufferView": 6, "componentType": 5126, "count": 3, "type": "SCALAR", "min": [0.0], "max": [1.0]},
		{"bufferView": 7, "componentType": 5126, "count": 3, "type": "VEC3"},
		{"bufferView": 8, "componentType": 5126, "count": 3, "type": "SCALAR", "min": [0.0], "max": [0.5]},
		{"bufferView": 9, "componentType": 5126, "count": 3, "type": "VEC3"}
	],
	"bufferViews": [
		{"buffer": 0, "byteOffset": 0, "byteLength": 12},
		{"buffer": 0, "byteOffset": 12, "byteLength": 48},
		{"buffer": 0, "byteOffset": 60, "byteLength": 72, "target": 34963},
		{"buffer": 0, "byteOffset": 132, "byteLength": 432, "target": 34962},
		{"buffer": 0, "byteOffset": 564, "byteLength": 432, "target": 34962},
		{"buffer": 0, "byteOffset": 1572, "byteLength": 288, "target": 34962},
		{"buffer": 0, "byteOffset": 1860, "byteLength": 12},
		{"buffer": 0, "byteOffset": 1872, "byteLength": 36},
		{"buffer": 0, "byteOffset": 1908, "byteLength": 12},
		{"buffer": 0, "byteOffset": 1920, "byteLength": 36}
	],
	"buffers": [{"byteLength": 1956, "uri": "data:application/octet-stream;base64,AAAAAAAAgD8AAABAAAAAAAAAAAAAAAAAAACAPwAAAAAAAIA/AAAAAC69O7MAAAAALr27swAAAAAAAIC/AAABAAIAAwAEAAUABgAHAAgACQAKAAsADAANAA4ADwAQABEAEgATABQAFQAWABcAGAAZABoAGwAcAB0AHgAfACAAIQAiACMAAACAPwAAgL8AAIA/AACAvwAAgL8AAIC/AACAPwAAgL8AAIC/AACAvwAAgD8AAIC/7/9/PwAAgD8IAIA/AACAPwAAgD/v/3+/AACAPwAAgD/v/3+/AACAPwAAgL8AAIA/AACAPwAAgL8AAIC/7/9/PwAAgD8IAIA/AACAvwAAgL8AAIA/AACAPwAAgL8AAIA/AACAvwAAgL8AAIA/AACAvwAAgD8AAIC/AACAvwAAgL8AAIC/AACAPwAAgL8AAIC/AACAvwAAgD8AAIC/AACAPwAAgD/v/3+/AACAPwAAgL8AAIA/AACAvwAAgL8AAIA/AACAvwAAgL8AAIC/AACAvwAAgD8AAIC/AACAvwAAgD8AAIA/7/9/PwAAgD8IAIA/AACAPwAAgD/v/3+/7/9/PwAAgD8IAIA/AACAPwAAgL8AAIA/7/9/PwAAgD8IAIA/AACAvwAAgD8AAIA/AACAvwAAgL8AAIA/AACAvwAAgL8AAIA/AACAvwAAgD8AAIA/AACAvwAAgD8AAIC/AACAPwAAgL8AAIC/AACAvwAAgL8AAIC/AACAvwAAgD8AAIC/AAAAgAAAgL8AAAAAAAAAgAAAgL8AAAAAAAAAgAAAgL8AAAAAAAAAAAAAgD8AAACAAAAAAAAAgD8AAACAAAAAAAAAgD8AAACAAACAPwAAAIAAAACAAACAPwAAAIAAAACAAACAPwAAAIAAAACAAAAAgAAAAIAAAIA/AAAAgAAAAIAAAIA/AAAAgAAAAIAAAIA/AACAvwAAAIAAAACAAACAvwAAAIAAAACAAACAvwAAAIAAAACAAAAAAAAAAAAAAIC/AAAAAAAAAAAAAIC/AAAAAAAAAAAAAIC/AAAAAAAAgL8AAAAAAAAAAAAAgL8AAAAAAAAAAAAAgL8AAAAAAAAAAAAAgD8AAAAAAAAAAAAAgD8AAAAAAAAAAAAAgD8AAAAAAACAPwAAAAC9N4Y1AACAPwAAAAC9N4Y1AACAPwAAAAC9N4Y1AAAAgAAAAAAAAIA/AAAAgAAAAAAAAIA/AAAAgAAAAAAAAIA/AACAvwAAAIAAAACAAACAvwAAAIAAAACAAACAvwAAAIAAAACAAAAAAAAAAAAAAIC/AAAAAAAAAAAAAIC/AAAAAAAAAAAAAIC/AACAPwAAAIAAAACAAACAvwAAgD8AAACAAAAAgAAAgL8AAIA/AAAAgAAAAIAAAIC/AACAPwAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAgD8AAIA/AAAAgAAAAAAAAIA/AAAAAAAAAAAAAIC/AACAPwAAAAAAAAAAAACAvwAAgD8AAAAAAAAAAAAAgL8AAIA/AACAPwAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAgD8AAIA/AAAAAAAAAAAAAIA/AAAAAAAAAIAAAIC/AACAvwAAAAAAAACAAACAvwAAgL8AAAAAAAAAgAAAgL8AAIC/AACAPwAAAIAAAAAAAACAvwAAgD8AAACAAAAAAAAAgL8AAIA/AAAAgAAAAAAAAIC/AACAPwAAAIAAAACAAACAvwAAgD8AAAAAAAAAgAAAgL8AAIA/AAAAgAAAAIAAAIC/AACAPwAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAgD8AAIA/AAAAAAAAAAAAAIA/vTeGNQAAAAAAAIC/AACAP703hjUAAAAAAACAvwAAgD+9N4Y1AAAAAAAAgL8AAIA/AACAPwAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAgD8AAIA/AAAAAAAAAAAAAIA/AAAAAAAAAIAAAIC/AACAvwAAAAAAAACAAACAvwAAgL8AAAAAAAAAgAAAgL8AAIC/AACAPwAAAIAAAAAAAACAvwAAgD8AAACAAAAAAAAAgL8AAIA/AAAAgAAAAAAAAIC/AAAAAAAAAAAAAIC/AACAPwAAAAAAAIA/AAAAAAAAAAAAAIA/AACAvwAAgD8AAACAAACAPwAAAAAAAAAAAACAvwAAgD8AAIC/AACAPwAAAAAAAACAAACAvwAAgD8AAIC/AAAAAAAAAAAAAIA/AACAPwAAgD8AAAAAAAAAAAAAAAAAAIC/AACAPwAAAAAAAIA/AAAAAAAAAAAAAIC/AAAAAAAAgL8AAIA/AAAAAAAAAAAAAACAAACAvwAAgD8AAIC/AACAPwAAAAAAAACAAAAAAAAAAAAAAIC/AACAPwAAAAAAAACAAAAAAAAAAIAAAIC/AAAAAAAAAAAAAAAAAACAPwAAgD8AAIA/AAAAAAAAAAAAAIC/AAAAAAAAgL8AAIA/AAAAAAAAAD8AAIA/AAAAAAAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAAAAAAAAAAAAgD4AAAA/AACAPwAAgD8AAIA/AADAPwAAwD8AAMA/AACAPwAAgD8AAIA/"}]
}
```

### D-3 `Sandbox/Source/Scripts/ClipCycler.h`（新しい）

```cpp
#pragma once
#include <array>
#include <cstddef>

#include "MyEngine/ECS/Script.h"
#include "MyEngine/Systems/Animation/AnimationSystem.h"

/// <summary>
/// アニメを名前で順に流す（Sandbox のテスト用。AnimationSystem::Play・CalcSpeedToFit を名前で呼ぶ見本）
/// <para>Clip 1 → Clip 2 → Clip 3 → Clip 1 … を、どれも Seconds Per Clip 秒でちょうど1回流しきる（ループしない）</para>
/// <para>同じ Entity に Animator を付ける。Clip の欄は、その Animator のファイルの中の名前から選べる</para>
/// </summary>
class ClipCycler : public Script {
public:
	void Start() override;
	void Update() override;
	void Fields(ComponentUI& ui) override;

private:
	// 次の欄のアニメを流す（空の欄は飛ばす）
	void PlayNext();

	static constexpr size_t kClipCount = 3; // 順に流すアニメの数

	std::array<AnimationClipName, kClipCount> clips_ = {"Spin", "Bounce", "Grow"}; // 順に流すアニメの名前（空の欄は飛ばす）
	float secondsPerClip_ = 1.0f; // 1つのアニメを流しきる秒（長さが違うアニメも、この秒に合わせる）
	size_t current_ = 0;          // 今流しているのは clips_ の何番目か（実行中だけ）
	float timer_ = 0.0f;          // 次のアニメまでの秒（実行中だけ）
};
```

### D-4 `Sandbox/Source/Scripts/ClipCycler.cpp`（新しい）

```cpp
#include "Source/Scripts/ClipCycler.h"

#include <format>
#include <string>
#include <string_view>

SCRIPT(ClipCycler, "Test");

//=============================================================================
// 毎フレーム
//=============================================================================
// ===== 始め =====
// 最後の欄を今の物にしておくと、PlayNext が最初の欄から流す
void ClipCycler::Start() {
	current_ = kClipCount - 1;
	PlayNext();
}

// ===== 毎フレーム =====
void ClipCycler::Update() {
	timer_ -= Time::GetDeltaTime();
	if (timer_ <= 0.0f) {
		PlayNext();
	}
}

// ===== 次のアニメ =====
void ClipCycler::PlayNext() {
	timer_ = secondsPerClip_;
	for (size_t step = 0; step < kClipCount; ++step) {
		current_ = (current_ + 1) % kClipCount;
		const std::string_view clip = clips_[current_].View();
		if (clip.empty()) {
			continue; // 空の欄は飛ばす（全部空なら何もしない）
		}
		// 長さが違うアニメも、ちょうど Seconds Per Clip 秒で流しきる（名前が Animator のファイルに無ければ、Play が警告して false）
		const float speed = AnimationSystem::CalcSpeedToFit(gameObject.handle, clip, secondsPerClip_);
		AnimationSystem::Play(gameObject.handle, clip, false, speed);
		return;
	}
}


//=============================================================================
// Inspector と保存
//=============================================================================
void ClipCycler::Fields(ComponentUI& ui) {
	for (size_t i = 0; i < kClipCount; ++i) {
		const std::string label = std::format("Clip {}###clip{}", i + 1, i); // 保存の名前は clip0・clip1・clip2
		ui.ClipField(label.c_str(), clips_[i], Tip("順に流すアニメの名前（同じ Entity の Animator のファイルの中の名前から選ぶ。空なら飛ばす）"));
	}
	ui.Field("Seconds Per Clip###secondsPerClip", secondsPerClip_, AtLeast(0.01f).Tip("1つのアニメを流しきる秒（長さが違うアニメも、この秒に合わせる）"));
}
```

### D-5 `Sandbox/Sandbox.vcxproj`・`Sandbox.vcxproj.filters`

`Sandbox.vcxproj`（Scripts の並びに1行ずつ）

```xml
    <ClCompile Include="Source\Scripts\ClipCycler.cpp" />
    <ClInclude Include="Source\Scripts\ClipCycler.h" />
```

`Sandbox.vcxproj.filters`

```xml
    <ClCompile Include="Source\Scripts\ClipCycler.cpp">
      <Filter>Source\Scripts</Filter>
    </ClCompile>
    <ClInclude Include="Source\Scripts\ClipCycler.h">
      <Filter>Source\Scripts</Filter>
    </ClInclude>
```

### D-6 `Sandbox/Resources/scenes/ClipNameTest.scene.json`（新しい）

左から **Bounce**（ClipNames の Bounce）・**Spin Reordered**（並べ替えたファイルの Spin）・**Missing Bounce**（並べ替えたファイルに無い Bounce）・**Cycler**（ClipCycler で Spin → Bounce → Grow を1秒ずつ）。

```json
{
	"version": 1,
	"entities": [
		{
			"id": 1,
			"name": "Main Camera",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [0.0, 5.0, -14.0],
					"rotation": [15.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Camera": {
					"fovY": 60.0,
					"nearZ": 0.1,
					"farZ": 1000.0,
					"priority": 0
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
					"color": [1.0, 1.0, 1.0]
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
					"translation": [0.0, -0.5, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [30.0, 1.0, 30.0]
				},
				"Primitive Renderer": {
					"enabled": true,
					"shape": "Box",
					"color": [0.6, 0.6, 0.6, 1.0]
				}
			}
		},
		{
			"id": 10,
			"name": "Bounce",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [-6.0, 1.0, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Model Renderer": {
					"enabled": true,
					"model": "resources/Model/ClipNames/ClipNames.gltf",
					"shadingType": "HalfLambert",
					"color": [1.0, 1.0, 1.0, 1.0],
					"overrideMaterial": false,
					"baseColor": [1.0, 1.0, 1.0, 1.0]
				},
				"Animator": {
					"enabled": true,
					"animation": "resources/Model/ClipNames/ClipNames.gltf",
					"clip": "Bounce",
					"speed": 1.0,
					"loop": true,
					"playOnStart": true,
					"previewTime": 0.0
				}
			}
		},
		{
			"id": 11,
			"name": "Spin Reordered",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [-2.0, 1.0, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Model Renderer": {
					"enabled": true,
					"model": "resources/Model/ClipNames/ClipNamesReordered.gltf",
					"shadingType": "HalfLambert",
					"color": [1.0, 1.0, 1.0, 1.0],
					"overrideMaterial": false,
					"baseColor": [1.0, 1.0, 1.0, 1.0]
				},
				"Animator": {
					"enabled": true,
					"animation": "resources/Model/ClipNames/ClipNamesReordered.gltf",
					"clip": "Spin",
					"speed": 1.0,
					"loop": true,
					"playOnStart": true,
					"previewTime": 0.0
				}
			}
		},
		{
			"id": 12,
			"name": "Missing Bounce",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [2.0, 1.0, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Model Renderer": {
					"enabled": true,
					"model": "resources/Model/ClipNames/ClipNamesReordered.gltf",
					"shadingType": "HalfLambert",
					"color": [1.0, 1.0, 1.0, 1.0],
					"overrideMaterial": false,
					"baseColor": [1.0, 1.0, 1.0, 1.0]
				},
				"Animator": {
					"enabled": true,
					"animation": "resources/Model/ClipNames/ClipNamesReordered.gltf",
					"clip": "Bounce",
					"speed": 1.0,
					"loop": true,
					"playOnStart": true,
					"previewTime": 0.0
				}
			}
		},
		{
			"id": 13,
			"name": "Cycler",
			"parent": 0,
			"active": true,
			"components": {
				"Transform": {
					"translation": [6.0, 1.0, 0.0],
					"rotation": [0.0, 0.0, 0.0],
					"scale": [1.0, 1.0, 1.0]
				},
				"Model Renderer": {
					"enabled": true,
					"model": "resources/Model/ClipNames/ClipNames.gltf",
					"shadingType": "HalfLambert",
					"color": [1.0, 1.0, 1.0, 1.0],
					"overrideMaterial": false,
					"baseColor": [1.0, 1.0, 1.0, 1.0]
				},
				"Animator": {
					"enabled": true,
					"animation": "resources/Model/ClipNames/ClipNames.gltf",
					"clip": "Spin",
					"speed": 1.0,
					"loop": true,
					"playOnStart": false,
					"previewTime": 0.0
				},
				"ClipCycler": {
					"clip0": "Spin",
					"clip1": "Bounce",
					"clip2": "Grow",
					"secondsPerClip": 1.0
				}
			}
		}
	]
}
```

### D-7 確かめること

Sandbox を起動して、Project ウィンドウから ClipNameTest を開く。

| # | すること | 見えること |
|---|---|---|
| 1 | 開いたときの Console | 「名前の無いアニメーションがあります … ClipNamesReordered.gltf（1 番目）。「#1」で選べますが…」「同じ名前のアニメーションが2つあります … 「Spin」」「アニメーション「Bounce」が resources/Model/ClipNames/ClipNamesReordered.gltf にありません」が1回ずつ |
| 2 | Hierarchy で Missing Bounce を選ぶ | Animator の Clip が赤で「(無い) Bounce」。マウスを乗せると理由が出る。開くと一番上に赤い「(無い) Bounce」、下に (none)・Grow・#1・Spin・Spin |
| 3 | Bounce を選ぶ | Clip は Bounce、下に「1.00 秒 ／ このファイルに 3 個」。Preview Time をつまむと箱が上下する |
| 4 | Spin Reordered を選んで Preview Time をつまむ | 箱が Y で回る（並べ替えたファイルの3番目＝先の Spin。カクカクしない） |
| 5 | Cycler を選ぶ | ClipCycler (Script) の Clip 1〜3 が一覧の欄になっていて、開くと Spin・Bounce・Grow が並ぶ（同じ Entity の Animator のファイルの名前） |
| 6 | Play | Bounce は跳ね続け、Spin Reordered は回り続け、Missing Bounce は止まったまま。Cycler は 回る → 跳ねる → 大きくなる を1秒ずつくり返す（長さが違っても、どれも1秒で流しきる） |
| 7 | Stop して Missing Bounce の Animation を ClipNames.gltf に替える | Clip の赤が消えて Bounce になり、Preview Time で跳ねる（名前はそのまま、ファイルだけ替わった） |
| 8 | 7 のまま Clip を開く | (none)・Spin・Bounce・Grow。Grow を選ぶと「0.50 秒 ／ このファイルに 3 個」 |
| 9 | Ctrl+Z | Clip が Bounce に戻る（Undo は名前ごと戻る）。もう一度 Ctrl+Z で Animation も戻り、また赤 |
| 10 | シーンを保存せずに開き直す（または 9 で元に戻してから保存して開き直す） | Missing Bounce は「(無い) Bounce」のまま（無い名前が保存で消えない） |
| 11 | Project ウィンドウで AttackTables/IdlePartTest.attacktable を選ぶ | Settings に Sample Animation（AnimatedCube.gltf）。各フェーズの ChaseTarget と Spin の Clip が一覧の欄で、開くと animation_AnimatedCube |
| 12 | 11 で Sample Animation を ClipNames.gltf に替える | Clip が赤で「(無い) animation_AnimatedCube」になり、一覧は Spin・Bounce・Grow（候補だけが替わり、値は消えない）。確かめたら AnimatedCube.gltf に戻す |
| 13 | IdlePartTest シーンを開いて Play | 前と同じく、Enemy が追ってきて Spin する（攻撃部品が名前でアニメを流す） |

## 2章 AL4_01 での直し方

エンジンのサブモジュールをこのコミットへ進めたら、次の順で直す（Blender の作業は別にやる）。**3 と 4 は同じときに入れる**（片方だけだと、名前が合わずにアニメが流れない）。

### 2.1 Blender：アクションの名前の番号を外し、02_Dash を消す

`project/Resources/Model/Player/Player.blend` を開く。

1. アウトライナーの表示を「Blender ファイル」にして、「アクション」を開く
2. 名前の頭の番号を外す（ダブルクリックで名前を変える）

| 今 | 後 |
|---|---|
| 00_Idle | Idle |
| 01_Run | Run |
| 02_Dash | （消す） |
| 03_Hit | Hit |
| 04_Down | Down |
| 05_Counter | Counter |
| 06_Ultimate | Ultimate |
| 07_Victory | Victory |
| 08_DodgeJump | DodgeJump |

3. 02_Dash を消す：アクションを右クリック → 削除。フェイクユーザー（盾の印）が付いていたら外す。NLA エディタにトラックがあれば、そのトラックも消す（残っていると書き出される）
4. 今までと同じ設定で `Player.gltf` に書き出す（上書き）。書き出した後の並びは名前順（Counter・DodgeJump・Down・Hit・Idle・Run・Ultimate・Victory）に変わるが、**名前で選ぶので並びは関係ない**
5. これからアクションを足すときも、番号は付けなくてよい（好きな名前で、どこに足してもよい）

### 2.2 エンジンで名前を確かめる

AL4_01 を起動し、Player の Animator の Clip を開くと、番号の無い8つの名前が並ぶ。GameScene の Player の Clip は「(無い) 00_Idle」と赤になっているので、2.3 で直す。

### 2.3 `project/Resources/scenes/GameScene.scene.json`

Player の Animator の Clip を `Idle` にする。Inspector で Idle を選んで保存するか、JSON を直す。

```json
"clip": "Idle",
```

（`GameScene.scene.json.bak` は古い写しなので直さなくてよい）

### 2.4 `project/Source/Scripts/Player/PlayerController.h`

「道具」の2行（`SetClip`・`CalcSpeedToFit`）を消す。エンジンの `AnimationSystem::Play`・`CalcSpeedToFit` を使う。

```cpp
	// ===== 道具 =====
	static float CalcGravity(const CharacterControllerComponent& controller); // このキャラにかかる重力の強さ（m/秒²。Character Controller と同じ値）
```

### 2.5 `project/Source/Scripts/Player/PlayerController.cpp`

include とアニメの名前（番号の3つを置き換え。Animator と AnimationManager の include はもう要らない）。

```cpp
#include <cmath>
#include <string_view>

#include <MyEngine/Curve/Curve.h>
#include <MyEngine/Curve/CurveManager.h>
#include <MyEngine/Input/InputManager.h>
#include <MyEngine/Systems/Animation/AnimationSystem.h>
#include <MyEngine/Systems/Physics/PhysicsSystem.h>

SCRIPT(PlayerController, "Player");

namespace {
// --- アニメの名前（Player.gltf の中の名前＝Blender のアクションの名前。ファイルの中の並びは関係ない）---
constexpr std::string_view kClipIdle = "Idle";           // 待機（ループ）
constexpr std::string_view kClipRun = "Run";             // 走り（ループ。杖を前に構えたまま）
constexpr std::string_view kClipDodgeJump = "DodgeJump"; // 回避ジャンプ（1回だけ。跳ぶ → 前に1回転 → 着地）
```

`Start` の最後の行。

```cpp
	AnimationSystem::Play(gameObject.handle, kClipIdle, true, 1.0f);
```

`StartDodge` の `dodgeElapsed_ = 0.0f;` の下に足す（回避ジャンプのアニメはここで1回だけ流す）。

```cpp
	// --- アニメ：1回転を、平らな地面で着地するまでの時間でちょうど流しきる（距離・高さ・重力を変えてもアニメが合う）---
	// 前の回避のアニメがまだ流れていても最初から（Play は同じ名前が流れていると速さしか変えないので、Restart も呼ぶ）
	AnimationSystem::Play(gameObject.handle, kClipDodgeJump, false, AnimationSystem::CalcSpeedToFit(gameObject.handle, kClipDodgeJump, dodgeAirTime_));
	AnimationSystem::Restart(gameObject.handle);
```

`UpdateAnimation` を差し替え、その下の「道具」の `SetClip`・`CalcSpeedToFit` の2つを消す（「道具」の大見出しと `CalcGravity` から下は残す）。

```cpp
// ===== アニメの切り替え =====
void PlayerController::UpdateAnimation(const Vector3& moveDirection) {
	switch (state_) {
	case PlayerState::Move: {
		const float moveAmount = Length(moveDirection); // スティックの倒し具合（0〜1）
		if (moveAmount > 0.0f) {
			// ゆっくり歩くときは足もゆっくり。std::lerp（C++20 の <cmath>）は a + (b - a) × t。エンジンの float の Lerp は MathUtility の中にあるので、STL の方を使う
			// Play は毎フレーム呼んでよい（同じ名前が流れていれば、速さだけ変わる）
			AnimationSystem::Play(gameObject.handle, kClipRun, true, std::lerp(kRunAnimMinSpeed, 1.0f, moveAmount));
		} else {
			AnimationSystem::Play(gameObject.handle, kClipIdle, true, 1.0f);
		}
		break;
	}
	case PlayerState::Dodge:
		// 回避ジャンプのアニメは StartDodge で1回だけ流す
		// ここで毎フレーム Play すると、台地の端から跳んでアニメが終わった後もまだ落ちているとき、Play が「止まっている」と見て最初から流し直してしまう
		break;
	}
}


//=============================================================================
// 道具
//=============================================================================
// ===== このキャラにかかる重力の強さ =====
```

- **回避ジャンプのアニメを StartDodge に移す理由**：エンジンの `Play` は「同じ名前が止まっていたら最初から」。今の `UpdateAnimation` のように Dodge の間ずっと Play を呼ぶと、台地の端から跳んでアニメ（平らな地面で着地する時間に合わせた長さ）が終わった後もまだ落ちているとき、最初から流し直してしまう。前の `SetClip` は番号が変わったときだけ切り替えていたので起きなかった
- 続けて回避したとき、前の回避のアニメがまだ流れていると `Play` は速さしか変えないので、`Restart` で最初からにする
- `Docs/PlayerMove.md`・`Docs/BossAttack.md` の番号の話（clipIndex・08_DodgeJump を後ろに足す）は古くなる

### 2.6 確かめること

| # | すること | 見えること |
|---|---|---|
| 1 | GameScene を開く | Console に「アニメーション「…」が … にありません」が出ない。Player の Clip が Idle |
| 2 | Play して止まったまま | Idle が流れる |
| 3 | 動く | Run（ゆっくり倒すと足もゆっくり）。離すと Idle |
| 4 | 回避ジャンプ | DodgeJump が1回、着地にちょうど合う。続けて回避しても毎回最初から流れる |
| 5 | 台地の端から回避ジャンプ | 1回転が終わったら、落ちている間は最後の形のまま（もう一度回らない） |

## 3章 次の候補

- **アニメの名前を付け直したとき**に、シーンの Animator・攻撃表の Clip をまとめて直す道具（今は「(無い) 名前」の赤を見て選び直す）
- **切り替えの混ぜ合わせ**（クロスフェード）：`Play(entity, "Run", true, 1.0f, 0.2f)` のように混ぜる秒を渡す
- **Animator の Inspector にアニメの一覧と長さ**（今は選んでいる物の長さだけ）
- **Timeline でアニメを流すトラック**（名前で選ぶ欄は `ClipField` がそのまま使える。候補は Timeline を再生する Entity の Animator）
