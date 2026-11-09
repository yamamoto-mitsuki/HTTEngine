参考：
- `std::mersenne_twister_engine`（`std::mt19937`）：https://en.cppreference.com/w/cpp/numeric/random/mersenne_twister_engine
- `std::ranges::shift_left`：https://en.cppreference.com/w/cpp/algorithm/ranges/shift
- `std::erase_if`：https://en.cppreference.com/w/cpp/container/vector/erase2
- `std::less<void>`（透過的な比較）：https://en.cppreference.com/w/cpp/utility/functional/less_void
- 既定の比較演算子（`operator== = default`）：https://en.cppreference.com/w/cpp/language/default_comparisons
- Dear ImGui の表（`ImGui::BeginTable`）：https://github.com/ocornut/imgui
- 参考にしたゲーム：『Furi』『ゼンレスゾーンゼロ』『崩壊：スターレイル』

# 攻撃表の AI（AttackTable）

敵の AI を、攻撃表で動かす機能としてエンジンに作る。2本のゲーム（個人制作の全方位シューティング・チーム制作のボス戦）のボスで使うが、ボスの専用にはしない。表を1フェーズにすれば、雑魚敵にもそのまま使える。

ビヘイビアツリーとノードエディタは作らない。**状態マシンと攻撃表**で動かし、値はエディタで直す。

作業の前に、develop からブランチを切る。

```bash
git switch -c feature/AttackTable develop
```

## 0章 全体の形

### 0.1 流れ

```
              ┌──────────────────────────────┐
              ↓                              │
Selecting ─→ Attacking ────────────────────┐ │
（選ぶ）     Windup → Active → Recovery ───┴─┘
             予備動作   攻撃     硬直

外からの割り込み（攻撃の途中でも止める）
  HP がしきい値以下  → PhaseTransition → Selecting
  Stagger(秒)        → Staggered       → Selecting
  HP が 0            → Defeated（ここで止まる）
```

### 0.2 何をどこに置くか

| 物 | 置き場所 | 理由 |
|---|---|---|
| 攻撃表（フェーズ・攻撃の値） | ファイル `.attacktable` | 行の数が変わるので、バイト列で写す Component には入れられない。Physic Material と同じ形にすると、Project ウィンドウ・Inspector の仕組みをそのまま使え、シーンと別のファイルなのでチームでも共有しやすい |
| 有効・表・Target・HP の割合 | `AttackTableComponent` | Entity ごとに違う値。保存・Prefab・Undo はふつうの Component と同じ |
| タイマー・クールタイム・攻撃部品 | `AttackTableSystem` が持つ | 再生中だけの値。行の数で大きさが変わる（Script・音と同じ「System が実体を持つ」形） |
| 攻撃の中身 | ゲーム側のクラス | `AttackPart` を継承して `ATTACK_PART(...)` を1行書く |

### 0.3 決めたこと

| 項目 | 決めたこと | 理由 |
|---|---|---|
| HP | エンジンは持たない。ゲームが `healthRatio`（0〜1）を書く | ダメージの計算はゲームごとに違う。2本で HP の持ち方が違っても共有できる |
| 時間 | `Time::GetDeltaTime()` だけで進める。0 の間は部品も呼ばない | ポーズ・時間停止・ヒットストップが全部これ1つで止まる。止まった時間の中で弾が出ない |
| クールタイム | 攻撃が終わった（止められた）時から数える | 攻撃より短いクールタイムが意味を失わないように |
| フェーズの移行 | 攻撃の途中でもすぐ移る。前のフェーズには戻らない | 必殺が当たった直後に演出へ入れるように |
| 距離 | 3D の距離 | 見下ろし型を前提にしない |
| 使いすぎの攻撃 | Max Repeat を超える攻撃は、ほかに候補がある間は選ばない | 候補が無いときに敵が止まらないように |
| 再生中の表の編集 | 値はその場で効く。行の足し引きと部品の変更は停止中だけ | 実行中の敵は行の番号で覚えている。再生中に直した値はファイルに残る（調整を消さないように） |
| 確認の画面 | Inspector ではなく Attack Table ウィンドウ | Inspector で表を直しながら、同時に確率を見るため |
| Undo | 表には無い。1つ前は `.bak` に残る | Physic Material と同じ |
| 名前 | 全部 `AttackTable`（攻撃表）か `Attack〜` にそろえる。`Boss`・`AI` は付けない | ボスの専用ではなく、`AI` だけでは手法が分からない。将来 `StateMachineComponent`・`BehaviorTreeComponent` を足しても、手法の名前で並ぶ |

### 0.4 パートの順番

**A → B → C → D → E の順に写す。** ほかの md と違い、後のパートが前のパートの型を使うので、順番を変えられない。

| パート | 内容 | 新しいファイル | ビルド |
|---|---|---|---|
| A | Component と攻撃部品の基底 | 3 | C の後でまとめて |
| B | 攻撃表（`.attacktable`） | 2 | C の後でまとめて |
| C | `AttackTableSystem` と組み込み | 2 | **ここで通る** |
| D | エディタ（表の編集・Attack Table ウィンドウ） | 4 | **ここで通る** |
| E | テスト用のボス（ゲーム側） | 2 | **ここで動く** |

新しいファイルは、Visual Studio の「既存の項目を追加」で `HTTEngine` プロジェクトに入れる。全部入れた後に次を実行すると、件数が **11 files、1 filter 増える**（この md を書いた時点の develop なら 417 files, 81 filters）。静的ライブラリは足し忘れてもビルドが通るので、件数で確かめる。

```bash
powershell -ExecutionPolicy Bypass -File Tools\UpdateFilters.ps1 HTTEngine.vcxproj
```

---

## パートA：Component と攻撃部品の基底

### A-1 `HTTEngine/Components/Gameplay/AttackTableComponent.h`（新しいファイル）

`healthRatio` と `enabled` はゲームが直接書いてよい。Component に実行中の値を入れないのは、攻撃の数だけ大きさが変わる値を、バイト列で写す Component に持てないため。

```cpp
#pragma once
#include <cstdint>
#include <type_traits>

#include "HTTEngine/ECS/Entity.h"

// 攻撃表の AI。Entityに付けるComponent（カテゴリ：Gameplay）
// 何をするかは攻撃表のファイル（.attacktable）に書く。ここは「どの表を使うか」と、ゲームから書く値だけを持つ
// タイマー・クールタイム・攻撃部品の実体は AttackTableSystem が持つ（数が表で変わるので、バイト列で写す Component には入れられない）

/// <summary>
/// AI の状態。Attacking の間だけ、AttackStage も動く
/// </summary>
enum class AttackTableState {
	Selecting,       // 次の攻撃を選んでいる（使える攻撃が無ければ、ここで待つ）
	Attacking,       // 攻撃している（予備動作 → 攻撃 → 硬直）
	Staggered,       // 怯んでいる（AttackTableSystem::Stagger）
	PhaseTransition, // フェーズの移行中
	Defeated,        // 撃破された（ここからは動かない）
};

/// <summary>
/// 攻撃の段階
/// </summary>
enum class AttackStage {
	None,     // 攻撃していない
	Windup,   // 予備動作
	Active,   // 攻撃
	Recovery, // 硬直
};


/// <summary>
/// 攻撃表の AI を動かす設定と、ゲームから書く値
/// </summary>
struct AttackTableComponent {
	bool enabled = true;      // false の間は AI を止める（タイマーも止まる。AttackTableSystem::SetEnabled でも変えられる）
	uint32_t table = 0;       // 攻撃表（AssetType::AttackTable の番号）
	EntityRef target;         // 狙う相手。距離の条件に使う
	float healthRatio = 1.0f; // 残りの HP の割合（0〜1）。ゲームが書く。フェーズの切り替えと撃破はこの値で決まる
};

static_assert(std::is_trivially_copyable_v<AttackTableComponent>);
```

### A-2 `HTTEngine/AI/AttackPart.h`（新しいファイル）

`HTTEngine/AI` フォルダを新しく作る。

部品の実体は敵1体の攻撃1行につき1つできる。だからメンバ変数に、攻撃の途中の値（走る向きなど）を持ってよい。

```cpp
#pragma once
#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "HTTEngine/Components/Gameplay/AttackTableComponent.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/ECS/GameObject.h"

// 敵の攻撃の中身（攻撃部品）を書くための基底クラス。ゲーム側はこのヘッダ1つをインクルードする
//
//   // DashAttack.cpp
//   class DashAttack : public AttackPart {
//   public:
//       void OnStage(const AttackPartContext& context) override;
//       void OnUpdate(const AttackPartContext& context) override;
//       void Fields(ComponentUI& ui) override { ui.Field("Speed", speed_); } // 攻撃表に出す・保存する値
//   private:
//       float speed_ = 20.0f;
//   };
//   ATTACK_PART(DashAttack, "相手の向きへまっすぐ走る"); // これで攻撃表の Part の一覧に出る（説明は省略できる）
//
// 実体は敵1体の攻撃1行につき1つできるので、メンバ変数に攻撃の途中の値を持ってよい

/// <summary>
/// 攻撃部品に渡す今の様子
/// </summary>
struct AttackPartContext {
	GameObject self;                       // 自分（この敵）
	GameObject target;                     // 狙う相手（居なければ if (context.target) が false）
	AttackStage stage = AttackStage::None; // 今の段階
	float stageTime = 0.0f;                // 今の段階に入ってからの秒
	float stageDuration = 0.0f;            // 今の段階の長さ（秒）
	float deltaTime = 0.0f;                // このフレームの秒（スローが掛かった物）
	int phase = 0;                         // 今のフェーズ（0 から）

	// 今の段階がどこまで進んだか（0〜1）
	float GetStageProgress() const { return (stageDuration > 0.0f) ? (std::min)(stageTime / stageDuration, 1.0f) : 1.0f; }
};

/// <summary>
/// 攻撃の終わり方
/// </summary>
enum class AttackPartEnd {
	Finished,    // 硬直まで終わった
	Interrupted, // 途中で止められた（怯み・フェーズの移行・撃破・敵が消えた）
};


/// <summary>
/// 攻撃部品。継承して、使う関数だけ override する
/// <para>呼ばれるのは時間が進んでいるときだけ（ポーズ・時間停止・ヒットストップ・AI 停止の間は呼ばれない）</para>
/// <para>Update の順番は ①OnBegin ②OnStage（予備動作）③OnUpdate を毎フレーム ④段階が変わるたびに OnStage ⑤OnEnd</para>
/// </summary>
class AttackPart {
public:
	virtual ~AttackPart() = default;

	virtual void OnBegin(const AttackPartContext& /*context*/) {}                         // 攻撃に選ばれた
	virtual void OnStage(const AttackPartContext& /*context*/) {}                         // 段階に入った（context.stage で分ける）
	virtual void OnUpdate(const AttackPartContext& /*context*/) {}                        // 毎フレーム
	virtual void OnEnd(const AttackPartContext& /*context*/, AttackPartEnd /*reason*/) {} // 終わった（途中で止められたときも必ず呼ばれる）

	// 攻撃表に出す・保存する値（書き方は Script::Fields と同じ）。攻撃1行ごとに別の値を持てる
	// 表はシーンの外のファイルなので、Entity への参照（EntityRef・GameObject）は書けない。Prefab は AssetField で書ける
	virtual void Fields(ComponentUI& /*ui*/) {}
};


/// <summary>
/// 攻撃部品の種類の表（名前 → 作り方）。ATTACK_PART(...) が登録する
/// </summary>
class AttackPartRegistry {
public:
	using CreateFunction = std::unique_ptr<AttackPart> (*)();

	static void Register(const char* name, CreateFunction create, const char* description);
	// 名前から作る（知らない名前なら nullptr）
	static std::unique_ptr<AttackPart> Create(std::string_view name);
	// 名前の一覧（名前の順。攻撃表の Part の選択肢）
	static std::vector<std::string> GetNames();
	// 説明（ATTACK_PART(...) の2つ目。知らない名前・書いていなければ空）
	static const std::string& GetDescription(std::string_view name);
};

/// <summary>
/// ATTACK_PART(...) が1つ作る物。作られたときに表へ登録する
/// </summary>
template<class T> class AttackPartRegistrar {
	static_assert(std::is_base_of_v<AttackPart, T>, "ATTACK_PART(...) に書けるのは、AttackPart を継承したクラスだけです");
	static_assert(std::is_default_constructible_v<T>, "攻撃部品は引数の無いコンストラクタで作れるようにしてください（エンジンが名前から作るため）");

public:
	AttackPartRegistrar(const char* name, const char* description) { AttackPartRegistry::Register(name, &Create, description); }

private:
	static std::unique_ptr<AttackPart> Create() { return std::make_unique<T>(); }
};

/// <summary>
/// 攻撃部品を登録する。.cpp に ATTACK_PART(クラス名); か ATTACK_PART(クラス名, "説明"); と1行書く
/// <para>クラス名がそのまま、攻撃表の Part の名前になる（変えると、前に保存した表は読めなくなる）。説明は Part の欄に出る</para>
/// </summary>
// "" __VA_ARGS__ は、説明を書かなければ ""、書けば "" "説明" ＝ "説明"（並んだ文字列はつながる。SCRIPT と同じ）
#define ATTACK_PART(Type, ...) inline const AttackPartRegistrar<Type> attackPartRegistrarOf_##Type(#Type, "" __VA_ARGS__)
```

### A-3 `HTTEngine/AI/AttackPart.cpp`（新しいファイル）

```cpp
#include "AttackPart.h"

#include <format>
#include <map>

#include "HTTEngine/Diagnostics/LogManager.h"

namespace {
// 表の1行
struct PartEntry {
	AttackPartRegistry::CreateFunction create; // 作り方
	std::string description;                   // Part の欄に出す説明
};

// 名前 → 作り方と説明 の表
// ATTACK_PART は main より前に登録するので、ふつうの static 変数だと表の方がまだ作られていないことがある。関数の中の static にする（REGISTER_SCENE と同じ）
// std::less<>：std::string_view のまま探せるようにする（探すたびに std::string を作らない）
std::map<std::string, PartEntry, std::less<>>& PartTable() {
	static std::map<std::string, PartEntry, std::less<>> table;
	return table;
}
} // namespace

//=============================================================================
// 登録・作る
//=============================================================================
void AttackPartRegistry::Register(const char* name, CreateFunction create, const char* description) { PartTable()[name] = {create, description}; }

std::unique_ptr<AttackPart> AttackPartRegistry::Create(std::string_view name) {
	const auto found = PartTable().find(name);
	return (found != PartTable().end()) ? found->second.create() : nullptr;
}

std::vector<std::string> AttackPartRegistry::GetNames() {
	std::vector<std::string> names;
	names.reserve(PartTable().size());
	for (const auto& [name, entry] : PartTable()) {
		names.push_back(name);
	}
	return names;
}

const std::string& AttackPartRegistry::GetDescription(std::string_view name) {
	static const std::string kEmpty; // 見つからないときに返す空文字（参照で返すので static にする）
	const auto found = PartTable().find(name);
	return (found != PartTable().end()) ? found->second.description : kEmpty;
}
```

**`std::map<..., std::less<>>` とは：** 比較に `std::less<>` を指定すると、`find` に `std::string` 以外の型（ここでは `std::string_view`）をそのまま渡せる。指定しないと、探すたびに `std::string` を作り直すことになる。

---

## パートB：攻撃表（`.attacktable`）

### B-1 `HTTEngine/AI/AttackTableManager.h`（新しいファイル）

```cpp
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// 前方宣言
class ComponentUI;

constexpr size_t kMaxAttackPhases = 8; // フェーズの数の上限（攻撃ごとの「使えるフェーズ」を決まった大きさの配列で持つため）

// 全部のフェーズで使える、の配列を作る（攻撃の初期値）
constexpr std::array<bool, kMaxAttackPhases> MakeAllPhases() {
	std::array<bool, kMaxAttackPhases> phases{};
	phases.fill(true);
	return phases;
}

/// <summary>
/// フェーズ1つ
/// </summary>
struct AttackPhaseData {
	std::string name = "Phase";     // 名前（確認の画面に出す）
	float healthThreshold = 0.5f;   // HP の割合がこの値以下になったら、このフェーズへ移る（一番上のフェーズでは使わない）
	float transitionSeconds = 2.0f; // このフェーズへ移るときの長さ（0 なら移行の状態を飛ばす）

	bool operator==(const AttackPhaseData&) const = default;
};

/// <summary>
/// 攻撃1行
/// </summary>
struct AttackData {
	std::string name = "Attack";   // 名前（確認の画面・RequestAttack に使う）
	std::string part;              // 攻撃部品（ATTACK_PART(...) の名前。空なら時間が進むだけ）
	std::string partFields = "{}"; // 攻撃部品の Fields に書いた値（JSON の文字）
	float weight = 1.0f;           // 抽選の重み（大きいほど選ばれやすい）
	float minDistance = 0.0f;      // 相手との距離がこれより近いと使わない
	float maxDistance = 0.0f;      // 相手との距離がこれより遠いと使わない（0 なら上限なし）
	float cooldown = 0.0f;         // 終わってから、次に使えるようになるまでの秒
	float windupSeconds = 0.5f;    // 予備動作の秒
	float activeSeconds = 0.5f;    // 攻撃の秒
	float recoverySeconds = 1.0f;  // 硬直の秒
	std::array<bool, kMaxAttackPhases> usablePhases = MakeAllPhases(); // [フェーズの番号] 使えるか

	bool operator==(const AttackData&) const = default;
};

/// <summary>
/// 攻撃表（ファイル1つの中身）。フェーズは必ず1つ以上ある
/// </summary>
struct AttackTable {
	int maxRepeat = 2; // 同じ攻撃を続けてよい回数（これを超える攻撃は、ほかに使える攻撃がある間は選ばない。0 なら制限なし）
	std::vector<AttackPhaseData> phases = {{"Phase 1", 1.0f, 0.0f}};
	std::vector<AttackData> attacks;

	bool operator==(const AttackTable&) const = default;
};


/// <summary>
/// 攻撃表のファイル（.attacktable）を読む・持つ・保存する（PhysicMaterialManager と同じ形）
/// <para>同じファイルを選んだ敵はみんな同じ表を見るので、ファイル1つを直すと、使っている全部に効く</para>
/// <para>中身は Inspector で直す（Project ウィンドウでファイルを選ぶ → AttackTableEditor）</para>
/// </summary>
class AttackTableManager {
public:
	static constexpr std::string_view kFileExtension = ".attacktable"; // 攻撃表のファイルの拡張子

	static void Initialize();
	static void Release();

	/// <summary>
	/// ファイルを読む。同じファイルなら、前に読んだ物の番号を返す
	/// </summary>
	/// <returns>番号（1から）。読めなければ 0</returns>
	static uint32_t Load(const std::filesystem::path& filePath);

	// 番号からパスを引く（知らない番号なら空）
	static const std::string& GetPath(uint32_t handle);

	// 中身（0・知らない番号なら nullptr）。ポインタは使い捨て
	static const AttackTable* Get(uint32_t handle);

	// 中身を書き換える（Inspector から。ファイルにはまだ書かない）。書き換えるたびに版が1つ増える
	static void Set(uint32_t handle, const AttackTable& table);

	// 中身の版（AttackTableSystem が、部品の値を読み直すかを決めるのに使う）
	static uint32_t GetRevision(uint32_t handle);

	// ファイルに書く
	static bool Save(uint32_t handle);

	// 新しいファイルを、既定の中身で作る（Project ウィンドウの Create → Attack Table）。同じ名前のファイルがあれば作らない
	static bool CreateNewFile(const std::filesystem::path& filePath);

	// ===== 見せ方（Inspector とファイルの両方がこの並びを使う）=====
	// 表全体の値・フェーズ1つ・攻撃1行。攻撃の Part と部品の値は、形が違うので別に扱う（Part は選択肢が実行中に決まる）
	static void DescribeTable(ComponentUI& ui, AttackTable& value);
	static void DescribePhase(ComponentUI& ui, AttackPhaseData& value, bool isFirst);
	static void DescribeAttack(ComponentUI& ui, AttackData& value, const std::vector<AttackPhaseData>& phases);
};
```

**`bool operator==(...) const = default;` とは：** メンバを上から順に全部比べる `==` をコンパイラに作らせる書き方。Inspector は「表の写しを直して、元と違えば書き戻す」作りなので、全部の項目を比べる必要がある。自分で書くと、項目を足したときに比べ忘れる。

**`constexpr` の `MakeAllPhases`：** `std::array` の `fill` はコンパイル時にも動くので、`{true, true, ...}` と8個並べずに済む。`kMaxAttackPhases` を変えても初期値を直さなくてよい。

### B-2 `HTTEngine/AI/AttackTableManager.cpp`（新しいファイル）

ファイルの形はこうなる。

```json
{
	"settings": { "maxRepeat": 2 },
	"phases": [ { "name": "Phase 1" }, { "name": "Phase 2", "healthThreshold": 0.5, "transitionSeconds": 1.0 } ],
	"attacks": [ { "name": "Dash", "weight": 2.0, "phase0": true, "phase1": true, "part": "BossTestDash", "partFields": { "Speed": 15.0 } } ]
}
```

1行の中身は `DescribeAttack` などの1つの関数を、書く・読む・Inspector の3つで使い回す（Component の `ui.Field` と同じ仕組み）。`part` と `partFields` だけは形が違うので手で読み書きする。読むときに残しておくと「読む所がない」と警告が出るので、`fields.erase` で取り除いてから `ReadFields` に渡す。

```cpp
#include "AttackTableManager.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <memory>
#include <set>

#include <externals/nlohmann/json.hpp>

#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/Scene/SceneSerializer.h"
#include "HTTEngine/String/ConvertString.h"

namespace {
constexpr std::string_view kTypeName = "Attack Table"; // 警告に出す名前

/// <summary>
/// 読み込んだファイル1つ
/// </summary>
struct AttackTableAsset {
	std::string path;      // ファイル（ToPathKey の形）
	AttackTable table;     // 中身
	uint32_t revision = 0; // 中身の版（Set のたびに増える）
};

/// <summary>
/// AttackTableManager が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct AttackTableStorage {
	std::vector<AttackTableAsset> assets; // 番号 - 1 番目のファイル
};
std::unique_ptr<AttackTableStorage> state;

const std::string kEmptyPath; // 知らない番号のパス

// 番号のファイル（知らない番号なら nullptr）
AttackTableAsset* Find(uint32_t handle) {
	if (state == nullptr || handle == 0 || handle > state->assets.size()) {
		return nullptr;
	}
	return &state->assets[handle - 1];
}

//=============================================================================
// JSON にする
//=============================================================================
// ファイルの形（"phases"・"attacks" は [ ] の並び。1つの中身は Describe〜 の項目）
//   { "settings": {...}, "phases": [{...}], "attacks": [{...}] }

// ===== 攻撃1行 =====
// Describe の項目のほかに、Part と部品の値を書く
SceneJson AttackToJson(AttackData attack, const std::vector<AttackPhaseData>& phases, std::set<std::string>& warnings) {
	SceneJson json;
	ComponentSerializer::WriteFields(kTypeName, [&](ComponentUI& ui) { AttackTableManager::DescribeAttack(ui, attack, phases); }, json, warnings);
	json["part"] = attack.part;
	json["partFields"] = SceneJson::parse(attack.partFields, nullptr, false);
	return json;
}

SceneJson ToJson(AttackTable table) {
	std::set<std::string> warnings;
	SceneJson settings;
	ComponentSerializer::WriteFields(kTypeName, [&table](ComponentUI& ui) { AttackTableManager::DescribeTable(ui, table); }, settings, warnings);

	SceneJson phases = SceneJson::array();
	for (size_t i = 0; i < table.phases.size(); ++i) {
		AttackPhaseData phase = table.phases[i];
		SceneJson item;
		ComponentSerializer::WriteFields(kTypeName, [&](ComponentUI& ui) { AttackTableManager::DescribePhase(ui, phase, i == 0); }, item, warnings);
		phases.push_back(std::move(item));
	}

	SceneJson attacks = SceneJson::array();
	for (const AttackData& attack : table.attacks) {
		attacks.push_back(AttackToJson(attack, table.phases, warnings));
	}

	SceneJson root = SceneJson::object();
	root["settings"] = std::move(settings);
	root["phases"] = std::move(phases);
	root["attacks"] = std::move(attacks);
	for (const std::string& warning : warnings) {
		LogManager::Warning(warning);
	}
	return root;
}

//=============================================================================
// JSON から読む
//=============================================================================
// ===== 攻撃1行 =====
AttackData AttackFromJson(const SceneJson& json, const std::vector<AttackPhaseData>& phases, std::set<std::string>& warnings) {
	AttackData attack;
	SceneJson fields = json; // Part と部品の値を除いた残りを、Describe の並びで読む（残すと「読む所がない」と警告が出る）
	if (const auto part = fields.find("part"); part != fields.end()) {
		if (part->is_string()) {
			attack.part = part->get<std::string>();
		}
		fields.erase(part);
	}
	if (const auto partFields = fields.find("partFields"); partFields != fields.end()) {
		if (partFields->is_object()) {
			attack.partFields = partFields->dump();
		}
		fields.erase(partFields);
	}
	ComponentSerializer::ReadFields(kTypeName, [&](ComponentUI& ui) { AttackTableManager::DescribeAttack(ui, attack, phases); }, fields, warnings);
	return attack;
}

AttackTable FromJson(const SceneJson& root, std::set<std::string>& warnings) {
	AttackTable table;
	if (const auto settings = root.find("settings"); settings != root.end()) {
		ComponentSerializer::ReadFields(kTypeName, [&table](ComponentUI& ui) { AttackTableManager::DescribeTable(ui, table); }, *settings, warnings);
	}
	// --- フェーズ（攻撃の「使えるフェーズ」が名前を見るので、先に読む）---
	if (const auto phases = root.find("phases"); phases != root.end() && phases->is_array() && !phases->empty()) {
		table.phases.clear();
		for (const SceneJson& item : *phases) {
			if (table.phases.size() >= kMaxAttackPhases) {
				warnings.insert(std::format("フェーズは {} 個までです。後ろは読みません", kMaxAttackPhases));
				break;
			}
			const bool isFirst = table.phases.empty();
			AttackPhaseData phase = isFirst ? AttackTable{}.phases.front() : AttackPhaseData{}; // 一番上のフェーズはしきい値を書かないので、表の既定の値から始める
			ComponentSerializer::ReadFields(kTypeName, [&](ComponentUI& ui) { AttackTableManager::DescribePhase(ui, phase, isFirst); }, item, warnings);
			table.phases.push_back(std::move(phase));
		}
	}
	// --- 攻撃 ---
	if (const auto attacks = root.find("attacks"); attacks != root.end() && attacks->is_array()) {
		for (const SceneJson& item : *attacks) {
			table.attacks.push_back(AttackFromJson(item, table.phases, warnings));
		}
	}
	return table;
}

// ファイルに書く（前のファイルは .bak として1つ残る）
bool WriteFile(const std::string& path, const AttackTable& table) {
	if (!SceneSerializer::WriteFile(path, ToJson(table))) {
		LogManager::Error(std::format("攻撃表を保存できませんでした: {}", path));
		return false;
	}
	return true;
}
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
void AttackTableManager::Initialize() {
	HTT_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<AttackTableStorage>();
}

void AttackTableManager::Release() { state.reset(); }

//=============================================================================
// 読み込み
//=============================================================================
uint32_t AttackTableManager::Load(const std::filesystem::path& filePath) {
	HTT_ASSERT_MSG(state != nullptr, "AttackTableManager::Initialize()を先に呼んでください");
	// --- 読み込み済みなら、同じ番号を返す ---
	const std::string key = ToPathKey(filePath);
	const auto cached = std::ranges::find(state->assets, key, &AttackTableAsset::path);
	if (cached != state->assets.end()) {
		return static_cast<uint32_t>(cached - state->assets.begin()) + 1;
	}
	// --- ファイルから読む（ファイルに無い項目は、既定の値のまま）---
	std::ifstream stream(filePath);
	if (!stream) {
		LogManager::Warning(std::format("攻撃表のファイルを開けませんでした: {}", key));
		return 0;
	}
	const SceneJson json = SceneJson::parse(stream, nullptr, false); // false: 壊れていても例外を投げず、discarded を返す
	if (json.is_discarded() || !json.is_object()) {
		LogManager::Error(std::format("攻撃表のファイルが壊れています: {}", key));
		return 0;
	}
	std::set<std::string> warnings;
	AttackTable table = FromJson(json, warnings);
	for (const std::string& warning : warnings) {
		LogManager::Warning(std::format("{}: {}", key, warning));
	}
	state->assets.push_back({key, std::move(table)});
	const uint32_t handle = static_cast<uint32_t>(state->assets.size());
	LogManager::Log(std::format("Loaded attack table: {} -> {}", key, handle));
	return handle;
}

const std::string& AttackTableManager::GetPath(uint32_t handle) {
	const AttackTableAsset* asset = Find(handle);
	return (asset != nullptr) ? asset->path : kEmptyPath;
}

const AttackTable* AttackTableManager::Get(uint32_t handle) {
	const AttackTableAsset* asset = Find(handle);
	return (asset != nullptr) ? &asset->table : nullptr;
}

//=============================================================================
// 書き換え・保存
//=============================================================================
void AttackTableManager::Set(uint32_t handle, const AttackTable& table) {
	AttackTableAsset* asset = Find(handle);
	if (asset == nullptr) {
		return;
	}
	asset->table = table;
	++asset->revision;
}

uint32_t AttackTableManager::GetRevision(uint32_t handle) {
	const AttackTableAsset* asset = Find(handle);
	return (asset != nullptr) ? asset->revision : 0;
}

bool AttackTableManager::Save(uint32_t handle) {
	const AttackTableAsset* asset = Find(handle);
	return (asset != nullptr) && WriteFile(asset->path, asset->table);
}

bool AttackTableManager::CreateNewFile(const std::filesystem::path& filePath) {
	std::error_code error;
	if (std::filesystem::exists(filePath, error)) {
		return false; // 上書きしない
	}
	return WriteFile(ToUtf8(filePath), AttackTable{});
}

//=============================================================================
// 見せ方（Inspector とファイル）
//=============================================================================
// ===== 表全体 =====
void AttackTableManager::DescribeTable(ComponentUI& ui, AttackTable& value) {
	ui.Field("Max Repeat###maxRepeat", value.maxRepeat, AtLeast(0.0f).Tip("同じ攻撃を続けてよい回数。これを超える攻撃は、ほかに使える攻撃がある間は選ばない（0 なら制限なし）"));
}

// ===== フェーズ1つ =====
void AttackTableManager::DescribePhase(ComponentUI& ui, AttackPhaseData& value, bool isFirst) {
	ui.Field("Name###name", value.name);
	// 一番上のフェーズは最初から入っているので、しきい値も移行の長さも使わない（保存もしない）
	if (isFirst) {
		return;
	}
	ui.Field("HP Threshold###healthThreshold", value.healthThreshold, Range(0.0f, 1.0f).Tip("HP の割合がこの値以下になったら、このフェーズへ移る（0.5 で 50%）"));
	ui.Field("Transition Seconds###transitionSeconds", value.transitionSeconds, AtLeast(0.0f).Tip("このフェーズへ移るときの長さ（秒）。0 なら移行の状態を飛ばす"));
}

// ===== 攻撃1行 =====
void AttackTableManager::DescribeAttack(ComponentUI& ui, AttackData& value, const std::vector<AttackPhaseData>& phases) {
	ui.Field("Name###name", value.name);
	ui.Field("Weight###weight", value.weight, AtLeast(0.0f).Tip("抽選の重み（大きいほど選ばれやすい。0 なら RequestAttack でしか出ない）"));
	ui.Field("Min Distance###minDistance", value.minDistance, AtLeast(0.0f).Tip("相手との距離がこれより近いと使わない"));
	ui.Field("Max Distance###maxDistance", value.maxDistance, AtLeast(0.0f).Tip("相手との距離がこれより遠いと使わない（0 なら上限なし）"));
	ui.Field("Cooldown###cooldown", value.cooldown, AtLeast(0.0f).Tip("攻撃が終わってから、次に使えるようになるまでの秒"));
	ui.Field("Windup Seconds###windupSeconds", value.windupSeconds, AtLeast(0.0f).Tip("予備動作の秒"));
	ui.Field("Active Seconds###activeSeconds", value.activeSeconds, AtLeast(0.0f).Tip("攻撃の秒"));
	ui.Field("Recovery Seconds###recoverySeconds", value.recoverySeconds, AtLeast(0.0f).Tip("硬直の秒（攻撃の後、次の攻撃を選ぶまでの間）"));
	// --- 使えるフェーズ（今あるフェーズの分だけ。ファイルの項目の名前は phase0, phase1, ... ）---
	ui.Separator("Usable Phases");
	for (size_t i = 0; i < phases.size() && i < kMaxAttackPhases; ++i) {
		const std::string label = std::format("{}###phase{}", phases[i].name, i);
		ui.Field(label.c_str(), value.usablePhases[i]);
	}
}
```

### B-3 `HTTEngine/ECS/ComponentUI.h`

変更前：

```cpp
	PhysicMaterial, // PhysicMaterialManager::Load の番号（.physmat）
};
```

変更後：

```cpp
	PhysicMaterial, // PhysicMaterialManager::Load の番号（.physmat）
	AttackTable,    // AttackTableManager::Load の番号（.attacktable）
};
```

### B-4 `HTTEngine/ECS/ComponentUI.cpp`（5か所）

include：

変更前：

```cpp
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/ECS/EntityManager.h"
```

変更後：

```cpp
#include "HTTEngine/AI/AttackTableManager.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/ECS/EntityManager.h"
```

`GetAssetPath`：

変更前：

```cpp
	case AssetType::PhysicMaterial:
		return PhysicMaterialManager::GetPath(handle);
	}
```

変更後：

```cpp
	case AssetType::PhysicMaterial:
		return PhysicMaterialManager::GetPath(handle);
	case AssetType::AttackTable:
		return AttackTableManager::GetPath(handle);
	}
```

`LoadAsset`：

変更前：

```cpp
	case AssetType::PhysicMaterial:
		return PhysicMaterialManager::Load(filePath);
	}
```

変更後：

```cpp
	case AssetType::PhysicMaterial:
		return PhysicMaterialManager::Load(filePath);
	case AssetType::AttackTable:
		return AttackTableManager::Load(filePath);
	}
```

拡張子の一覧：

変更前：

```cpp
constexpr std::array<std::string_view, 1> kPhysicMaterialExtensions = {PhysicMaterialManager::kFileExtension}; // ".physmat"
```

変更後：

```cpp
constexpr std::array<std::string_view, 1> kPhysicMaterialExtensions = {PhysicMaterialManager::kFileExtension}; // ".physmat"
constexpr std::array<std::string_view, 1> kAttackTableExtensions = {AttackTableManager::kFileExtension}; // ".attacktable"
```

`ExtensionsOf`：

変更前：

```cpp
	case AssetType::PhysicMaterial:
		return kPhysicMaterialExtensions;
	}
```

変更後：

```cpp
	case AssetType::PhysicMaterial:
		return kPhysicMaterialExtensions;
	case AssetType::AttackTable:
		return kAttackTableExtensions;
	}
```

---

## パートC：`AttackTableSystem` と組み込み

### C-1 `HTTEngine/Systems/Gameplay/AttackTableSystem.h`（新しいファイル）

```cpp
#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "HTTEngine/Components/Gameplay/AttackTableComponent.h"
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"

/// <summary>
/// 敵の今の様子（AttackTableSystem::GetStatus）。読むたびに作る写しなので、持ち続けても壊れないが、値は古くなる
/// </summary>
struct AttackTableStatus {
	bool isRunning = false;                               // 動き始めている（再生中で、表が選ばれている）
	bool isEnabled = false;                               // AI が止められていない
	AttackTableState state = AttackTableState::Selecting; // 状態
	AttackStage stage = AttackStage::None;                // 攻撃の段階（Attacking の間だけ）
	int phase = 0;                                        // 今のフェーズ（0 から）
	std::string phaseName;                                // 今のフェーズの名前
	std::string attackName;                               // 今の攻撃の名前（攻撃していなければ空）
	std::string lastAttackName;                           // 最後に選んだ攻撃の名前（1つも選んでいなければ空）
	float time = 0.0f;                                    // 今の段階・状態に入ってからの秒
	float duration = 0.0f;                                // 今の段階・状態の長さ（秒。Selecting・Defeated は 0）

	// 残りの秒
	float GetRemaining() const { return (duration > time) ? duration - time : 0.0f; }
};

/// <summary>
/// 攻撃1行を今使えるかの結果（確認の画面用）
/// </summary>
struct AttackCheck {
	bool isPhaseOk = false;         // 今のフェーズで使える
	bool isDistanceOk = false;      // 距離が範囲の中
	bool isCooldownOk = false;      // クールタイムが終わっている
	bool isRepeatOk = false;        // 続けて使いすぎていない
	float cooldownRemaining = 0.0f; // クールタイムの残りの秒
	int repeatCount = 0;            // 今何回続けて使っているか
	float probability = 0.0f;       // 今選ぶなら、選ばれる確率（0〜1）
};


/// <summary>
/// 攻撃表の AI を動かす係（状態マシン・フェーズ・攻撃の抽選・段階の進行）
/// <para>流れ：Selecting → Attacking（Windup → Active → Recovery）→ Selecting。怯み・フェーズの移行・撃破が、攻撃を途中で止める</para>
/// <para>タイマー・クールタイム・攻撃部品の実体は、ここが敵ごとに持つ（再生が始まって最初の Update で作り、Stop で Entity と一緒に消える）</para>
/// <para>時間は Time::GetDeltaTime() だけで進める。0 の間（一時停止・ポーズ・時間停止・ヒットストップ）は、攻撃部品も含めて何も呼ばない</para>
/// </summary>
class AttackTableSystem {
public:
	// AttackTableComponent を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// 全部の敵を1フレーム進める。シーンの Update・Script・SYSTEM の後、ワールド行列の計算の前に呼ぶ（攻撃部品が動かした位置を、そのフレームの描画に使うため）
	/// </summary>
	/// <param name="isGameRunning">再生中か（一時停止中も true。そのときは deltaTime が 0 なので止まる）</param>
	/// <param name="deltaTime">経過秒（Time::GetDeltaTime）</param>
	static void Update(bool isGameRunning, float deltaTime);

	// ===== 外から指示する（ゲーム・Timeline）=====
	/// <summary>
	/// 怯ませる。攻撃の途中なら止める。怯んでいる間にもう一度呼んだら、残りの長い方
	/// </summary>
	/// <returns>怯んだか（撃破・フェーズの移行の間と、まだ動いていないときは false）</returns>
	static bool Stagger(Handle<Entity> entity, float seconds);

	// AI を止める・動かす（止めている間はタイマーも止まり、攻撃の途中ならそこから続く）
	static void SetEnabled(Handle<Entity> entity, bool isEnabled);

	/// <summary>
	/// 次の攻撃を名前で決める（条件を見ずに、次に選ぶときに必ずこの攻撃になる）
	/// </summary>
	/// <returns>その名前の攻撃があったか</returns>
	static bool RequestAttack(Handle<Entity> entity, std::string_view attackName);

	// ===== 外から読む（ゲームの判定・確認の画面）=====
	static AttackTableStatus GetStatus(Handle<Entity> entity);
	// 攻撃表の行ごとの結果を out に入れる（行の順。まだ動いていなければ空）
	static void GetAttackChecks(Handle<Entity> entity, std::vector<AttackCheck>& out);
	// 相手との距離（相手が居なければ無限大）。敵の位置の高さも含めた 3D の距離
	static float CalcTargetDistance(Handle<Entity> entity);
	// 敵が1体でも動いているか（再生中は、攻撃表の行を増やす・消すのを止める）
	static bool IsAnyRunning();

private:
	// Entity が消える直前（EntityManager から呼ばれる）
	static void OnEntityDestroyed(Handle<Entity> entity);
};
```

### C-2 `HTTEngine/Systems/Gameplay/AttackTableSystem.cpp`（新しいファイル）

読むときの要点は4つ。

1. **`CheckAttacks` が1つだけ。** 本当の抽選も Attack Table ウィンドウの確率も、同じ関数の結果を使う。だから画面の確率と実際の動きがずれない。
2. **重み付きの抽選（`PickAttack`）。** 確率の幅を横に並べた帯を作り、0〜1 の乱数がどこに落ちたかで選ぶ。A が 75%、B が 25% なら、乱数が 0.75 未満で A、それ以上で B になる。乱数は既にある `RandomEngine`（中身は `std::mt19937`）を使う。
3. **時間が 0 なら全部飛ばす（`UpdateEntity`）。** クールタイムを減らす処理も、部品を呼ぶ処理も、この1か所の `return` より後にある。
4. **部品の中から `Stagger` を呼んでもよい。** `OnBegin`・`OnUpdate` の直後に状態を見直して、攻撃が終わっていたら先へ進まない。

**`std::erase_if(runtimes, ...)`：** 条件に合う要素を全部消す関数（C++20）。`erase` と `remove_if` を組み合わせる昔の書き方を1行にした物。

```cpp
#include "AttackTableSystem.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <memory>
#include <set>

#include <externals/nlohmann/json.hpp>

#include "HTTEngine/AI/AttackTableManager.h"
#include "HTTEngine/AI/AttackPart.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/Math/RandomEngine.h"

namespace {
constexpr int kNoAttack = -1; // 攻撃を指していない番号

//=============================================================================
// 敵1体分の実行中の値
//=============================================================================
struct AttackTableRuntime {
	Handle<Entity> entity;                          // どの敵か
	uint32_t table = 0;                             // 作ったときの攻撃表（Component の表が替わったら作り直す）
	uint32_t revision = 0;                          // 読んだ攻撃表の版（変わったら部品の値を読み直す）
	std::vector<std::unique_ptr<AttackPart>> parts; // [攻撃の行] 攻撃部品（Part が空・知らない名前なら nullptr）
	std::vector<float> cooldowns;                   // [攻撃の行] クールタイムの残りの秒

	AttackTableState state = AttackTableState::Selecting; // 状態
	AttackStage stage = AttackStage::None;                // 攻撃の段階
	int phase = 0;                                        // 今のフェーズ
	int attack = kNoAttack;                               // 今の攻撃の行
	int lastAttack = kNoAttack;                           // 最後に選んだ攻撃の行（同じ攻撃が続いた回数を数える）
	int repeatCount = 0;                                  // lastAttack を続けて選んだ回数
	int requestedAttack = kNoAttack;                      // RequestAttack で決められた次の攻撃の行
	float time = 0.0f;                                    // 今の段階・状態に入ってからの秒
	float duration = 0.0f;                                // 今の段階・状態の長さ
};

std::vector<AttackTableRuntime> runtimes; // 動いている敵（数が少ないので、一列に並べて順に探す）

// その敵の実行中の値（まだ動いていなければ nullptr）
AttackTableRuntime* FindRuntime(Handle<Entity> entity) {
	const auto found = std::ranges::find(runtimes, entity, &AttackTableRuntime::entity);
	return (found != runtimes.end()) ? &*found : nullptr;
}

//=============================================================================
// 攻撃部品の値を読む
//=============================================================================
void ReadPartFields(AttackPart& part, const AttackData& attack, std::string_view tablePath) {
	const SceneJson json = SceneJson::parse(attack.partFields, nullptr, false);
	std::set<std::string> warnings;
	ComponentSerializer::ReadFields(attack.name, [&part](ComponentUI& ui) { part.Fields(ui); }, json, warnings);
	for (const std::string& warning : warnings) {
		LogManager::Warning(std::format("{}: {}", tablePath, warning));
	}
}

void ReadAllPartFields(AttackTableRuntime& runtime, const AttackTable& table) {
	for (size_t i = 0; i < runtime.parts.size(); ++i) {
		if (runtime.parts[i] != nullptr) {
			ReadPartFields(*runtime.parts[i], table.attacks[i], AttackTableManager::GetPath(runtime.table));
		}
	}
}

//=============================================================================
// フェーズ
//=============================================================================
// HP の割合から、今いるべきフェーズを決める（今より前には戻らない。一度に2つ越えたら、先の方）
int CalcPhase(const AttackTable& table, int currentPhase, float healthRatio) {
	int phase = currentPhase;
	for (int i = currentPhase + 1; i < static_cast<int>(table.phases.size()); ++i) {
		if (healthRatio <= table.phases[i].healthThreshold) {
			phase = i;
		}
	}
	return phase;
}

//=============================================================================
// 攻撃の開始・終了
//=============================================================================
// 攻撃部品に渡す今の様子を作る
AttackPartContext MakeContext(const AttackTableRuntime& runtime, const AttackTableComponent& component, float deltaTime) {
	AttackPartContext context;
	context.self = GameObject{runtime.entity};
	context.target = GameObject{EntityManager::Find(component.target)};
	context.stage = runtime.stage;
	context.stageTime = runtime.time;
	context.stageDuration = runtime.duration;
	context.deltaTime = deltaTime;
	context.phase = runtime.phase;
	return context;
}

// 状態を変える（タイマーも最初から）
void EnterState(AttackTableRuntime& runtime, AttackTableState state, float duration) {
	runtime.state = state;
	runtime.stage = AttackStage::None;
	runtime.time = 0.0f;
	runtime.duration = duration;
}

// 段階の長さ
float StageSeconds(const AttackData& attack, AttackStage stage) {
	switch (stage) {
	case AttackStage::None:
		return 0.0f;
	case AttackStage::Windup:
		return attack.windupSeconds;
	case AttackStage::Active:
		return attack.activeSeconds;
	case AttackStage::Recovery:
		return attack.recoverySeconds;
	}
	return 0.0f;
}

// 段階に入る
void EnterStage(AttackTableRuntime& runtime, const AttackTableComponent& component, const AttackData& attack, AttackStage stage, float deltaTime) {
	runtime.stage = stage;
	runtime.time = 0.0f;
	runtime.duration = StageSeconds(attack, stage);
	if (AttackPart* part = runtime.parts[runtime.attack].get()) {
		part->OnStage(MakeContext(runtime, component, deltaTime));
	}
}

// 攻撃を始める（同じ攻撃が続いた回数も、ここで数える）
void BeginAttack(AttackTableRuntime& runtime, const AttackTableComponent& component, const AttackTable& table, int attack, float deltaTime) {
	runtime.repeatCount = (attack == runtime.lastAttack) ? runtime.repeatCount + 1 : 1;
	runtime.lastAttack = attack;
	runtime.attack = attack;
	runtime.state = AttackTableState::Attacking;
	runtime.stage = AttackStage::None;
	if (AttackPart* part = runtime.parts[attack].get()) {
		part->OnBegin(MakeContext(runtime, component, deltaTime));
	}
	// OnBegin の中で Stagger を呼ばれたら、攻撃はもう終わっている
	if (runtime.state == AttackTableState::Attacking) {
		EnterStage(runtime, component, table.attacks[attack], AttackStage::Windup, deltaTime);
	}
}

// 今の攻撃の部品へ、終わったことを知らせる（攻撃していなければ何もしない）
void NotifyAttackEnd(AttackTableRuntime& runtime, const AttackTableComponent& component, AttackPartEnd reason, float deltaTime) {
	if (runtime.attack == kNoAttack) {
		return;
	}
	if (AttackPart* part = runtime.parts[runtime.attack].get()) {
		part->OnEnd(MakeContext(runtime, component, deltaTime), reason);
	}
}

// 攻撃を終える（クールタイムは、終わったときから数える）
void EndAttack(AttackTableRuntime& runtime, const AttackTableComponent& component, const AttackTable& table, AttackPartEnd reason, float deltaTime) {
	NotifyAttackEnd(runtime, component, reason, deltaTime);
	if (runtime.attack == kNoAttack) {
		return;
	}
	runtime.cooldowns[runtime.attack] = table.attacks[runtime.attack].cooldown;
	runtime.attack = kNoAttack;
	runtime.stage = AttackStage::None;
}

//=============================================================================
// 攻撃の抽選
//=============================================================================
// 行ごとに条件を調べて、選ばれる確率まで入れる（抽選と確認の画面がこれ1つを使うので、画面の確率と本当の抽選がずれない）
void CheckAttacks(const AttackTableRuntime& runtime, const AttackTable& table, float distance, std::vector<AttackCheck>& out) {
	out.assign(table.attacks.size(), AttackCheck{});
	// --- 条件を調べる ---
	for (size_t i = 0; i < table.attacks.size(); ++i) {
		const AttackData& attack = table.attacks[i];
		AttackCheck& check = out[i];
		const bool isLast = (static_cast<int>(i) == runtime.lastAttack);
		check.isPhaseOk = attack.usablePhases[runtime.phase];
		check.isDistanceOk = (distance >= attack.minDistance) && (attack.maxDistance <= 0.0f || distance <= attack.maxDistance);
		check.cooldownRemaining = runtime.cooldowns[i];
		check.isCooldownOk = (check.cooldownRemaining <= 0.0f);
		check.repeatCount = isLast ? runtime.repeatCount : 0;
		check.isRepeatOk = (table.maxRepeat <= 0) || (check.repeatCount < table.maxRepeat);
	}
	// --- 重みを足す。使いすぎの攻撃を除いて候補が残らないときだけ、使いすぎの攻撃も候補にする（敵が止まらないように）---
	const auto sumWeights = [&table, &out](bool useRepeat) {
		float total = 0.0f;
		for (size_t i = 0; i < out.size(); ++i) {
			const AttackCheck& check = out[i];
			if (check.isPhaseOk && check.isDistanceOk && check.isCooldownOk && (check.isRepeatOk || !useRepeat)) {
				total += table.attacks[i].weight;
			}
		}
		return total;
	};
	const bool useRepeat = (sumWeights(true) > 0.0f);
	const float total = sumWeights(useRepeat);
	if (total <= 0.0f) {
		return; // 使える攻撃が無い
	}
	// --- 重みを確率にする ---
	for (size_t i = 0; i < out.size(); ++i) {
		AttackCheck& check = out[i];
		if (check.isPhaseOk && check.isDistanceOk && check.isCooldownOk && (check.isRepeatOk || !useRepeat)) {
			check.probability = table.attacks[i].weight / total;
		}
	}
}

// 確率に合わせて1つ選ぶ（選べなければ kNoAttack）
int PickAttack(const std::vector<AttackCheck>& checks) {
	// 0〜1 の乱数を、確率の幅で並べた帯のどこに落ちたかで選ぶ（RandomEngine の中身は std::mt19937）
	const float roll = RandomEngine::GetFloat(0.0f, 1.0f);
	float sum = 0.0f;
	int last = kNoAttack; // 小数の誤差で帯の端を超えたときは、最後の候補
	for (size_t i = 0; i < checks.size(); ++i) {
		if (checks[i].probability <= 0.0f) {
			continue;
		}
		sum += checks[i].probability;
		last = static_cast<int>(i);
		if (roll < sum) {
			break;
		}
	}
	return last;
}

//=============================================================================
// 距離
//=============================================================================
float CalcDistance(Handle<Entity> entity, const AttackTableComponent& component) {
	const TransformComponent* self = EntityManager::Get<TransformComponent>(entity);
	const TransformComponent* target = EntityManager::Get<TransformComponent>(component.target);
	if (self == nullptr || target == nullptr) {
		return (std::numeric_limits<float>::infinity)(); // 相手が居ない（上限のある攻撃は使えない）
	}
	return Length(GetWorldPosition(*target) - GetWorldPosition(*self));
}

//=============================================================================
// 実行中の値を作る
//=============================================================================
// 表に合わせて攻撃部品を作る
void Build(AttackTableRuntime& runtime, const AttackTableComponent& component, const AttackTable& table) {
	runtime.table = component.table;
	runtime.revision = AttackTableManager::GetRevision(component.table);
	runtime.parts.clear();
	for (const AttackData& attack : table.attacks) {
		std::unique_ptr<AttackPart> part = AttackPartRegistry::Create(attack.part);
		if (part == nullptr && !attack.part.empty()) {
			LogManager::Warning(std::format("攻撃 \"{}\" の部品 \"{}\" がありません（ATTACK_PART(...) を確かめてください）。時間だけ進めます", attack.name, attack.part));
		}
		runtime.parts.push_back(std::move(part));
	}
	ReadAllPartFields(runtime, table);
	runtime.cooldowns.assign(table.attacks.size(), 0.0f);
	runtime.attack = kNoAttack;
	runtime.lastAttack = kNoAttack;
	runtime.repeatCount = 0;
	runtime.requestedAttack = kNoAttack;
	runtime.phase = CalcPhase(table, 0, component.healthRatio); // 始めから HP が減っていれば、移行の状態を飛ばしてそのフェーズから
	EnterState(runtime, AttackTableState::Selecting, 0.0f);
}

// その敵の実行中の値を、今の Component に合わせて返す（表が選ばれていなければ nullptr）
AttackTableRuntime* PrepareRuntime(Handle<Entity> entity, const AttackTableComponent& component, const AttackTable& table) {
	AttackTableRuntime* runtime = FindRuntime(entity);
	if (runtime == nullptr) {
		runtime = &runtimes.emplace_back();
		runtime->entity = entity;
		Build(*runtime, component, table);
		return runtime;
	}
	// --- 表が替わった・行の数が変わった：今の攻撃を止めて作り直す ---
	if (runtime->table != component.table || runtime->parts.size() != table.attacks.size()) {
		NotifyAttackEnd(*runtime, component, AttackPartEnd::Interrupted, 0.0f); // 表はもう新しい物なので、クールタイムは数えない（Build で全部 0 に戻る）
		Build(*runtime, component, table);
		return runtime;
	}
	// --- 値が変わった（再生中に表を直した）：部品の値だけ読み直す。今の攻撃は続ける ---
	const uint32_t revision = AttackTableManager::GetRevision(component.table);
	if (runtime->revision != revision) {
		runtime->revision = revision;
		ReadAllPartFields(*runtime, table);
	}
	return runtime;
}

//=============================================================================
// 1フレーム進める
//=============================================================================
// ===== HP を見て、撃破・フェーズの移行へ =====
// 攻撃の途中でも、すぐに移る。移ったら true
bool UpdateHealth(AttackTableRuntime& runtime, const AttackTableComponent& component, const AttackTable& table, float deltaTime) {
	if (runtime.state == AttackTableState::Defeated) {
		return true;
	}
	if (component.healthRatio <= 0.0f) {
		EndAttack(runtime, component, table, AttackPartEnd::Interrupted, deltaTime);
		EnterState(runtime, AttackTableState::Defeated, 0.0f);
		return true;
	}
	const int phase = CalcPhase(table, runtime.phase, component.healthRatio);
	if (phase == runtime.phase) {
		return false;
	}
	EndAttack(runtime, component, table, AttackPartEnd::Interrupted, deltaTime);
	runtime.phase = phase;
	EnterState(runtime, AttackTableState::PhaseTransition, table.phases[phase].transitionSeconds);
	return true;
}

// ===== 選ぶ =====
void UpdateSelecting(AttackTableRuntime& runtime, const AttackTableComponent& component, const AttackTable& table, float deltaTime) {
	int attack = runtime.requestedAttack;
	runtime.requestedAttack = kNoAttack;
	if (attack == kNoAttack) {
		static std::vector<AttackCheck> checks; // 毎フレーム配列を作り直さないように使い回す
		CheckAttacks(runtime, table, CalcDistance(runtime.entity, component), checks);
		attack = PickAttack(checks);
	}
	if (attack != kNoAttack) {
		BeginAttack(runtime, component, table, attack, deltaTime);
	}
}

// ===== 攻撃を進める =====
void UpdateAttacking(AttackTableRuntime& runtime, const AttackTableComponent& component, const AttackTable& table, float deltaTime) {
	const AttackData& attack = table.attacks[runtime.attack];
	runtime.time += deltaTime;
	runtime.duration = StageSeconds(attack, runtime.stage); // 再生中に秒を直しても、すぐ効くように
	if (AttackPart* part = runtime.parts[runtime.attack].get()) {
		part->OnUpdate(MakeContext(runtime, component, deltaTime));
	}
	// OnUpdate の中で Stagger を呼ばれたら、攻撃はもう終わっている
	if (runtime.state != AttackTableState::Attacking || runtime.time < runtime.duration) {
		return;
	}
	// --- 段階が終わった ---
	switch (runtime.stage) {
	case AttackStage::Windup:
		EnterStage(runtime, component, attack, AttackStage::Active, deltaTime);
		break;
	case AttackStage::Active:
		EnterStage(runtime, component, attack, AttackStage::Recovery, deltaTime);
		break;
	case AttackStage::Recovery:
	case AttackStage::None:
		EndAttack(runtime, component, table, AttackPartEnd::Finished, deltaTime);
		EnterState(runtime, AttackTableState::Selecting, 0.0f);
		break;
	}
}

// ===== 怯み・フェーズの移行：時間が来たら選ぶ状態へ =====
void UpdateTimedState(AttackTableRuntime& runtime, float deltaTime) {
	runtime.time += deltaTime;
	if (runtime.time >= runtime.duration) {
		EnterState(runtime, AttackTableState::Selecting, 0.0f);
	}
}

// ===== 敵1体 =====
void UpdateEntity(Handle<Entity> entity, const AttackTableComponent& component, float deltaTime) {
	const AttackTable* table = AttackTableManager::Get(component.table);
	if (table == nullptr) {
		return; // 表が選ばれていない
	}
	AttackTableRuntime* runtime = PrepareRuntime(entity, component, *table);
	// 止まっている間は何も進めない（クールタイムも、攻撃部品の呼び出しも）。時間が 0 なのに部品を呼ぶと、止まった時間の中で攻撃が動いてしまう
	if (!component.enabled || deltaTime <= 0.0f || !EntityManager::IsActiveInHierarchy(entity)) {
		return;
	}
	for (float& cooldown : runtime->cooldowns) {
		cooldown = (std::max)(cooldown - deltaTime, 0.0f);
	}
	if (UpdateHealth(*runtime, component, *table, deltaTime)) {
		return;
	}
	switch (runtime->state) {
	case AttackTableState::Selecting:
		UpdateSelecting(*runtime, component, *table, deltaTime);
		break;
	case AttackTableState::Attacking:
		UpdateAttacking(*runtime, component, *table, deltaTime);
		break;
	case AttackTableState::Staggered:
	case AttackTableState::PhaseTransition:
		UpdateTimedState(*runtime, deltaTime);
		break;
	case AttackTableState::Defeated:
		break;
	}
}

//=============================================================================
// Inspector・保存
//=============================================================================
void DescribeAttackTableComponent(ComponentUI& ui, AttackTableComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと AI が止まる（タイマーも止まる）"));
	ui.AssetField("Table###table", value.table, AssetType::AttackTable, Tip("攻撃表（.attacktable）。Project ウィンドウの Create → Attack Table で作る"));
	ui.Field("Target###target", value.target, Tip("狙う相手。距離の条件に使う"));
	ui.Field("Health Ratio###healthRatio", value.healthRatio, Range(0.0f, 1.0f).Tip("残りの HP の割合（0〜1）。ゲームが書く。再生中につまむと、フェーズの移行と撃破を試せる"));
}
} // namespace


//=============================================================================
// 初期化 / 解放
//=============================================================================
void AttackTableSystem::Initialize() {
	// Attack Table の置き場所・保存・Inspector を登録する（見せ方は DescribeAttackTableComponent 1つで、保存と Inspector の両方に使う）
	EntityManager::RegisterComponent<AttackTableComponent>();
	ComponentSerializer::Register<AttackTableComponent>("Attack Table", &DescribeAttackTableComponent);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<AttackTableComponent>>("Attack Table", "Gameplay", &DescribeAttackTableComponent));
	// 敵が消えるときに、攻撃の途中なら OnEnd を呼べるようにする
	EntityManager::AddDestroyListener(&AttackTableSystem::OnEntityDestroyed);
}

void AttackTableSystem::Release() { runtimes.clear(); }

//=============================================================================
// 毎フレーム
//=============================================================================
void AttackTableSystem::Update(bool isGameRunning, float deltaTime) {
	if (!isGameRunning) {
		return; // 停止中は動かさない（実行中の値も作らない）
	}
	// --- Component を外された敵を片付ける ---
	std::erase_if(runtimes, [](const AttackTableRuntime& runtime) { return EntityManager::Get<AttackTableComponent>(runtime.entity) == nullptr; });
	EntityManager::ForEach<AttackTableComponent>([deltaTime](Handle<Entity> entity, AttackTableComponent& component) { UpdateEntity(entity, component, deltaTime); });
}

//=============================================================================
// 外から指示する
//=============================================================================
bool AttackTableSystem::Stagger(Handle<Entity> entity, float seconds) {
	AttackTableRuntime* runtime = FindRuntime(entity);
	const AttackTableComponent* component = EntityManager::Get<AttackTableComponent>(entity);
	const AttackTable* table = (component != nullptr) ? AttackTableManager::Get(component->table) : nullptr;
	if (runtime == nullptr || table == nullptr || seconds <= 0.0f) {
		return false;
	}
	switch (runtime->state) {
	case AttackTableState::Defeated:
	case AttackTableState::PhaseTransition:
		return false; // 撃破とフェーズの移行は、怯みで止めない
	case AttackTableState::Staggered:
		runtime->duration = (std::max)(runtime->duration, runtime->time + seconds); // 残りの長い方
		return true;
	case AttackTableState::Selecting:
	case AttackTableState::Attacking:
		break;
	}
	EndAttack(*runtime, *component, *table, AttackPartEnd::Interrupted, 0.0f);
	EnterState(*runtime, AttackTableState::Staggered, seconds);
	return true;
}

void AttackTableSystem::SetEnabled(Handle<Entity> entity, bool isEnabled) {
	if (AttackTableComponent* component = EntityManager::Get<AttackTableComponent>(entity)) {
		component->enabled = isEnabled;
	}
}

bool AttackTableSystem::RequestAttack(Handle<Entity> entity, std::string_view attackName) {
	AttackTableRuntime* runtime = FindRuntime(entity);
	const AttackTableComponent* component = EntityManager::Get<AttackTableComponent>(entity);
	const AttackTable* table = (component != nullptr) ? AttackTableManager::Get(component->table) : nullptr;
	if (runtime == nullptr || table == nullptr) {
		return false;
	}
	const auto found = std::ranges::find(table->attacks, attackName, &AttackData::name);
	if (found == table->attacks.end()) {
		LogManager::Warning(std::format("RequestAttack：攻撃 \"{}\" がありません", attackName));
		return false;
	}
	runtime->requestedAttack = static_cast<int>(found - table->attacks.begin());
	return true;
}

//=============================================================================
// 外から読む
//=============================================================================
AttackTableStatus AttackTableSystem::GetStatus(Handle<Entity> entity) {
	AttackTableStatus status;
	const AttackTableRuntime* runtime = FindRuntime(entity);
	const AttackTableComponent* component = EntityManager::Get<AttackTableComponent>(entity);
	const AttackTable* table = (component != nullptr) ? AttackTableManager::Get(component->table) : nullptr;
	if (runtime == nullptr || table == nullptr) {
		return status;
	}
	const auto attackName = [table](int attack) { return (attack != kNoAttack && static_cast<size_t>(attack) < table->attacks.size()) ? table->attacks[attack].name : std::string(); };
	status.isRunning = true;
	status.isEnabled = component->enabled;
	status.state = runtime->state;
	status.stage = runtime->stage;
	status.phase = runtime->phase;
	status.phaseName = table->phases[runtime->phase].name;
	status.attackName = attackName(runtime->attack);
	status.lastAttackName = attackName(runtime->lastAttack);
	status.time = runtime->time;
	status.duration = runtime->duration;
	return status;
}

void AttackTableSystem::GetAttackChecks(Handle<Entity> entity, std::vector<AttackCheck>& out) {
	out.clear();
	const AttackTableRuntime* runtime = FindRuntime(entity);
	const AttackTableComponent* component = EntityManager::Get<AttackTableComponent>(entity);
	const AttackTable* table = (component != nullptr) ? AttackTableManager::Get(component->table) : nullptr;
	if (runtime != nullptr && table != nullptr && runtime->cooldowns.size() == table->attacks.size()) {
		CheckAttacks(*runtime, *table, CalcDistance(entity, *component), out);
	}
}

float AttackTableSystem::CalcTargetDistance(Handle<Entity> entity) {
	const AttackTableComponent* component = EntityManager::Get<AttackTableComponent>(entity);
	return (component != nullptr) ? CalcDistance(entity, *component) : (std::numeric_limits<float>::infinity)();
}

bool AttackTableSystem::IsAnyRunning() { return !runtimes.empty(); }

//=============================================================================
// Entity が消える直前
//=============================================================================
void AttackTableSystem::OnEntityDestroyed(Handle<Entity> entity) {
	AttackTableRuntime* runtime = FindRuntime(entity);
	if (runtime == nullptr) {
		return;
	}
	const AttackTableComponent* component = EntityManager::Get<AttackTableComponent>(entity);
	if (component != nullptr) {
		NotifyAttackEnd(*runtime, *component, AttackPartEnd::Interrupted, 0.0f); // Component はまだ読める。攻撃部品が出した物を片付けられるように
	}
	std::erase_if(runtimes, [entity](const AttackTableRuntime& other) { return other.entity == entity; });
}
```

### C-3 `HTTEngine/Systems/EngineSystems.cpp`（4か所）

include：

変更前：

```cpp
#include "HTTEngine/Systems/EngineSystems.h"

#include "HTTEngine/ECS/ComponentSerializer.h"
```

変更後：

```cpp
#include "HTTEngine/Systems/EngineSystems.h"

#include "HTTEngine/AI/AttackTableManager.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
```

変更前：

```cpp
#include "HTTEngine/Systems/Effects/TrailSystem.h"
#include "HTTEngine/Systems/Gameplay/ScriptSystem.h"
```

変更後：

```cpp
#include "HTTEngine/Systems/Effects/TrailSystem.h"
#include "HTTEngine/Systems/Gameplay/AttackTableSystem.h"
#include "HTTEngine/Systems/Gameplay/ScriptSystem.h"
```

`Initialize`：

変更前：

```cpp
	TriggerActivatorSystem::Initialize(); // Trigger Activator
	UIScreen::Initialize();               // UI Screen・UI Screen Element
```

変更後：

```cpp
	TriggerActivatorSystem::Initialize(); // Trigger Activator
	AttackTableManager::Initialize();     // 攻撃表（.attacktable）。AttackTableSystem が読むので先に
	AttackTableSystem::Initialize();           // Attack Table
	UIScreen::Initialize();               // UI Screen・UI Screen Element
```

`Release`：

変更前：

```cpp
	TriggerActivatorSystem::Release();
	UIScreen::Release();
```

変更後：

```cpp
	TriggerActivatorSystem::Release();
	AttackTableSystem::Release();
	AttackTableManager::Release();
	UIScreen::Release();
```

`Update`（シーン・Script・SYSTEM の後、ワールド行列の前）：

変更前：

```cpp
	ParticleManager::SetCamera(gameCamera); // 見つけたカメラを毎フレーム渡す（シーンが替わっても古いカメラを掴まない）
	AnimationSystem::Update(isGameRunning, Time::GetDeltaTime()); // アニメーション。停止中もInspectorのPreview Timeのポーズを見せる。
```

変更後：

```cpp
	ParticleManager::SetCamera(gameCamera); // 見つけたカメラを毎フレーム渡す（シーンが替わっても古いカメラを掴まない）
	// 攻撃表の AI（シーン・Script・SYSTEM の後。攻撃部品が動かした位置を、このフレームのワールド行列・物理に使う）
	AttackTableSystem::Update(isGameRunning, Time::GetDeltaTime());
	AnimationSystem::Update(isGameRunning, Time::GetDeltaTime()); // アニメーション。停止中もInspectorのPreview Timeのポーズを見せる。
```

### C-4 `HTTEngine/ECS/GameComponent.h`

Script・SYSTEM から `AttackTableSystem::GetStatus` を使えるようにする。

変更前：

```cpp
#include "HTTEngine/Systems/Rendering2D/SpriteRenderSystem.h"
#include "HTTEngine/Systems/Effects/TrailSystem.h"
```

変更後：

```cpp
#include "HTTEngine/Systems/Rendering2D/SpriteRenderSystem.h"
#include "HTTEngine/Systems/Gameplay/AttackTableSystem.h"
#include "HTTEngine/Systems/Effects/TrailSystem.h"
```

**ここでビルドが通る。** Add Component → Gameplay に Attack Table が出る（Table の一覧はまだ空）。

---

## パートD：エディタ

### D-1 `HTTEngine/Editor/Inspector/AttackTableEditor.h`（新しいファイル）

```cpp
#pragma once

/// <summary>
/// .attacktable のファイルを Inspector で直す係（AssetInspector に登録する）。エディタだけ
/// <para>行の足し引きがあるので、Physic Material と違って ImGui を直接使う（1行の中身は AttackTableManager::Describe〜 に任せる）</para>
/// </summary>
class AttackTableEditor {
public:
	// AssetInspector に登録する
	static void Initialize();
};
```

### D-2 `HTTEngine/Editor/Inspector/AttackTableEditor.cpp`（新しいファイル）

Physic Material は `ComponentUI` だけで描けたが、ここは行の足し引きと折りたたみがあるので ImGui を直接使う。ファイル全体が `#ifdef USE_IMGUI` の中にあるので、Release には入らない。

`DrawPartFields` が少し変わった作りになっている。部品の値は表に JSON の文字で入っているが、どんな項目があるかはゲームのクラスの `Fields` しか知らない。そこで**部品を1つ作り、JSON を読ませ、`Fields` を Inspector に描き、JSON へ書き戻す**。開いている行の分だけ毎フレーム行う。

**`std::ranges::shift_left`：** 範囲の要素を前へずらす関数（C++23。範囲の指定を `begin, end` で渡す形）。フェーズを消したとき、後ろのフェーズの「使える」の印を1つずつ前へ詰める。空いた末尾は `true` に戻す。

```cpp
#ifdef USE_IMGUI
#include "AttackTableEditor.h"

#include <algorithm>
#include <format>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <externals/imgui/imgui.h>
#include <externals/nlohmann/json.hpp>

#include "HTTEngine/AI/AttackTableManager.h"
#include "HTTEngine/AI/AttackPart.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/Editor/Inspector/AssetInspector.h"
#include "HTTEngine/Systems/Gameplay/AttackTableSystem.h"

namespace {
constexpr std::string_view kTypeName = "Attack Table"; // Inspector の見出しに出す名前
constexpr const char* kNoPartDescription = "部品を使わない（中身は動かず、時間だけ進む）"; // Part が (none) のときの説明

//=============================================================================
// 攻撃部品
//=============================================================================
// ===== Part の選択欄 =====
// 選択肢は ATTACK_PART(...) で登録した部品。選んでいる部品の説明を、欄の下に出す
// 知らない名前（別のゲームで作った部品など）は、消さずに赤で出す
void DrawPartCombo(AttackData& attack, const std::vector<std::string>& names) {
	const bool isUnknown = !attack.part.empty() && std::ranges::find(names, attack.part) == names.end();
	if (isUnknown) {
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 96, 96, 255));
	}
	const std::string preview = attack.part.empty() ? "(none)" : attack.part;
	if (ImGui::BeginCombo("Part###part", preview.c_str())) {
		if (ImGui::Selectable("(none)", attack.part.empty())) {
			attack.part.clear();
			attack.partFields = "{}";
		}
		for (const std::string& name : names) {
			if (ImGui::Selectable(name.c_str(), name == attack.part) && name != attack.part) {
				attack.part = name;
				attack.partFields = "{}"; // 別の部品の値は使えない
			}
		}
		ImGui::EndCombo();
	}
	if (isUnknown) {
		ImGui::PopStyleColor();
		ImGui::SetItemTooltip("この名前の攻撃部品がありません（ATTACK_PART(...) を確かめる）");
	}
}

// ===== 部品の値 =====
// 部品を1つ作って、表の値を読み、Fields を描いて、書き戻す（Fields の実体はゲームのクラスの中にあるので、表だけでは描けない）
void DrawPartFields(ComponentUI& ui, AttackData& attack) {
	std::unique_ptr<AttackPart> part = AttackPartRegistry::Create(attack.part);
	if (part == nullptr) {
		return;
	}
	std::set<std::string> warnings;
	const auto describe = [&part](ComponentUI& fields) { part->Fields(fields); };
	ComponentSerializer::ReadFields(attack.name, describe, SceneJson::parse(attack.partFields, nullptr, false), warnings);
	part->Fields(ui);
	SceneJson json;
	ComponentSerializer::WriteFields(attack.name, describe, json, warnings);
	attack.partFields = json.dump();
	for (const std::string& warning : warnings) {
		LogManager::Warning(warning);
	}
}

//=============================================================================
// 行を増やす・消す
//=============================================================================
// ===== フェーズ =====
void DrawPhases(ComponentUI& ui, AttackTable& table, bool isLocked) {
	int removePhase = -1;
	for (int i = 0; i < static_cast<int>(table.phases.size()); ++i) {
		ImGui::PushID(i);
		ImGui::SeparatorText(std::format("{}. {}", i + 1, table.phases[i].name).c_str());
		AttackTableManager::DescribePhase(ui, table.phases[i], i == 0);
		if (i != 0) {
			ImGui::BeginDisabled(isLocked);
			if (ImGui::SmallButton("Remove Phase")) {
				removePhase = i;
			}
			ImGui::EndDisabled();
		}
		ImGui::PopID();
	}
	if (removePhase >= 0) {
		table.phases.erase(table.phases.begin() + removePhase);
		// 攻撃の「使えるフェーズ」も1つずつ詰める（後ろのフェーズの設定が、1つ前へずれないように）
		for (AttackData& attack : table.attacks) {
			std::ranges::shift_left(attack.usablePhases.begin() + removePhase, attack.usablePhases.end(), 1);
			attack.usablePhases.back() = true;
		}
	}
	ImGui::BeginDisabled(isLocked || table.phases.size() >= kMaxAttackPhases);
	if (ImGui::Button("Add Phase")) {
		AttackPhaseData phase;
		phase.name = std::format("Phase {}", table.phases.size() + 1);
		table.phases.push_back(std::move(phase));
	}
	ImGui::EndDisabled();
}

// ===== 攻撃 =====
void DrawAttacks(ComponentUI& ui, AttackTable& table, bool isLocked) {
	const std::vector<std::string> partNames = AttackPartRegistry::GetNames();
	int removeAttack = -1;
	int copyAttack = -1;
	for (int i = 0; i < static_cast<int>(table.attacks.size()); ++i) {
		AttackData& attack = table.attacks[i];
		ImGui::PushID(i);
		// ### の後ろを番号にする（名前を打ち換えても、開いたままにする）
		if (ImGui::CollapsingHeader(std::format("{}###attack", attack.name).c_str())) {
			AttackTableManager::DescribeAttack(ui, attack, table.phases);
			ImGui::SeparatorText("Part");
			ImGui::BeginDisabled(isLocked);
			DrawPartCombo(attack, partNames);
			ImGui::EndDisabled();
			DrawPartFields(ui, attack);
			ImGui::Spacing();
			ImGui::BeginDisabled(isLocked);
			if (ImGui::SmallButton("Duplicate")) {
				copyAttack = i;
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Delete")) {
				removeAttack = i;
			}
			ImGui::EndDisabled();
			ImGui::Spacing();
		}
		ImGui::PopID();
	}
	// 配列を動かすのは、全部描いた後（描いている途中で動かすと、attack の参照が壊れる）
	if (copyAttack >= 0) {
		AttackData copy = table.attacks[copyAttack];
		copy.name += " Copy";
		table.attacks.insert(table.attacks.begin() + copyAttack + 1, std::move(copy));
	}
	if (removeAttack >= 0) {
		table.attacks.erase(table.attacks.begin() + removeAttack);
	}
	ImGui::BeginDisabled(isLocked);
	if (ImGui::Button("Add Attack")) {
		AttackData attack;
		attack.name = std::format("Attack {}", table.attacks.size() + 1);
		table.attacks.push_back(std::move(attack));
	}
	ImGui::EndDisabled();
}

//=============================================================================
// Inspector
//=============================================================================
/// <summary>
/// .attacktable のファイルを Inspector で直す係
/// </summary>
class AttackTableAssetEditor final : public AssetEditor {
public:
	std::string_view GetExtension() const override { return AttackTableManager::kFileExtension; }
	std::string_view GetTypeName() const override { return kTypeName; }

	bool Draw(const std::filesystem::path& filePath, ComponentUI& ui) override {
		const uint32_t handle = AttackTableManager::Load(filePath);
		const AttackTable* current = AttackTableManager::Get(handle);
		if (current == nullptr) {
			ui.Label("このファイルは読めません（JSON が壊れている）");
			return false;
		}
		// 写しを直して、変わっていたら書き戻す（値は、再生中でも次のフレームからすぐ効く）
		AttackTable table = *current;
		const bool isLocked = AttackTableSystem::IsAnyRunning(); // 再生中は行を増やす・消す・部品を替えるのを止める（敵が行の番号で覚えているので）
		AttackTableManager::DescribeTable(ui, table);
		ImGui::SeparatorText("Phases");
		// フェーズと攻撃は、どちらも行の番号を ID にするので、区画ごとに枠を分ける（分けないと同じ番号の Name 欄がぶつかる）
		ImGui::PushID("Phases");
		DrawPhases(ui, table, isLocked);
		ImGui::PopID();
		ImGui::Spacing();
		ImGui::SeparatorText("Attacks");
		ImGui::PushID("Attacks");
		DrawAttacks(ui, table, isLocked);
		ImGui::PopID();
		ImGui::Spacing();
		ui.Label(isLocked ? "再生中は値だけ直せる（行の足し引きと部品の変更は Stop してから）" : "このファイルを選んだ敵みんなに効く（つまむ・打つのが終わると保存。Undo は無い。1つ前は .bak に残る）");
		if (table == *current) {
			return false;
		}
		AttackTableManager::Set(handle, table);
		return true;
	}

	void Save(const std::filesystem::path& filePath) override { AttackTableManager::Save(AttackTableManager::Load(filePath)); }
};
} // namespace


//=============================================================================
// 初期化
//=============================================================================
void AttackTableEditor::Initialize() { AssetInspector::Register(std::make_unique<AttackTableAssetEditor>()); }
#endif
```

### D-3 `HTTEngine/Editor/Windows/AttackTableWindow.h`（新しいファイル）

```cpp
#pragma once
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"

/// <summary>
/// Attack Table ウィンドウ。再生中の敵の様子（フェーズ・状態・段階・残り時間）と、攻撃ごとの抽選確率・条件・クールタイムを出す
/// <para>Hierarchy で Attack Table の付いた Entity を選んでいればその敵、選んでいなければ最初の敵を出す</para>
/// </summary>
class AttackTableWindow {
public:
	// 毎フレーム1回（ImGuiManager::Begin から）
	static void Draw();

private:
	// 出す敵を決める（居なければ無効な Handle）
	static Handle<Entity> FindEntity();
	// 上の段（フェーズ・状態・段階・残り時間）
	static void DrawStatus(Handle<Entity> entity);
	// 下の段（攻撃ごとの表）
	static void DrawAttacks(Handle<Entity> entity);
};
```

### D-4 `HTTEngine/Editor/Windows/AttackTableWindow.cpp`（新しいファイル）

```cpp
#ifdef USE_IMGUI
#include "AttackTableWindow.h"

#include <cfloat>
#include <cmath>
#include <format>
#include <string>
#include <vector>

#include <externals/imgui/imgui.h>
#include <externals/magic_enum/magic_enum.hpp>

#include "HTTEngine/AI/AttackTableManager.h"
#include "HTTEngine/Components/Gameplay/AttackTableComponent.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/Editor/Windows/HierarchyWindow.h"
#include "HTTEngine/Systems/Gameplay/AttackTableSystem.h"

namespace {
constexpr ImU32 kOkColor = IM_COL32(120, 220, 120, 255); // 条件を満たしている
constexpr ImU32 kNgColor = IM_COL32(240, 110, 110, 255); // 条件を満たしていない
constexpr float kStaggerTestSeconds = 1.0f;              // Stagger ボタンで怯ませる秒

// 条件の表の1マス（満たしていれば緑、いなければ赤）
void DrawCondition(bool isOk, const std::string& text) {
	ImGui::PushStyleColor(ImGuiCol_Text, isOk ? kOkColor : kNgColor);
	ImGui::TextUnformatted(text.c_str());
	ImGui::PopStyleColor();
}
} // namespace

//=============================================================================
// 描画
//=============================================================================
void AttackTableWindow::Draw() {
	if (ImGui::Begin("Attack Table")) {
		const Handle<Entity> entity = FindEntity();
		if (entity.IsValid()) {
			DrawStatus(entity);
			DrawAttacks(entity);
		} else {
			ImGui::TextDisabled("Attack Table の付いた Entity がありません");
		}
	}
	ImGui::End();
}

// ===== 出す敵を決める =====
Handle<Entity> AttackTableWindow::FindEntity() {
	const Handle<Entity> selected = HierarchyWindow::GetSelected();
	if (EntityManager::Get<AttackTableComponent>(selected) != nullptr) {
		return selected;
	}
	Handle<Entity> first;
	EntityManager::ForEach<AttackTableComponent>([&first](Handle<Entity> entity, AttackTableComponent&) {
		if (!first.IsValid()) {
			first = entity;
		}
	});
	return first;
}

// ===== 上の段 =====
void AttackTableWindow::DrawStatus(Handle<Entity> entity) {
	const Entity* info = EntityManager::Get(entity);
	const AttackTableComponent* component = EntityManager::Get<AttackTableComponent>(entity);
	const AttackTableStatus status = AttackTableSystem::GetStatus(entity);
	ImGui::Text("Entity: %s", info->name.c_str());
	ImGui::SameLine();
	ImGui::TextDisabled("%s", AttackTableManager::GetPath(component->table).c_str());
	if (!status.isRunning) {
		ImGui::TextDisabled(component->table == 0 ? "Table が選ばれていません" : "再生すると様子が出ます");
		return;
	}
	const std::string state(magic_enum::enum_name(status.state));
	const std::string stage(magic_enum::enum_name(status.stage));
	ImGui::Text("Phase: %d %s", status.phase + 1, status.phaseName.c_str());
	ImGui::Text("State: %s", state.c_str());
	if (status.stage != AttackStage::None) {
		ImGui::SameLine();
		ImGui::Text("/ Stage: %s", stage.c_str());
	}
	if (!status.isEnabled) {
		ImGui::SameLine();
		ImGui::TextColored(ImColor(kNgColor), "（AI 停止中）");
	}
	ImGui::Text("Attack: %s", status.attackName.empty() ? "-" : status.attackName.c_str());
	ImGui::SameLine();
	ImGui::TextDisabled("（最後に選んだ：%s）", status.lastAttackName.empty() ? "-" : status.lastAttackName.c_str());
	// --- 残り時間 ---
	const float progress = (status.duration > 0.0f) ? status.time / status.duration : 0.0f;
	ImGui::ProgressBar(progress, ImVec2(-FLT_MIN, 0.0f), std::format("残り {:.2f} / {:.2f} 秒", status.GetRemaining(), status.duration).c_str());
	// --- 距離・HP ---
	const float distance = AttackTableSystem::CalcTargetDistance(entity);
	ImGui::Text("Distance: %s", std::isinf(distance) ? "(Target なし)" : std::format("{:.2f}", distance).c_str());
	ImGui::SameLine();
	ImGui::Text("/ HP: %.0f%%", component->healthRatio * 100.0f);
	// --- 試す（ゲームを書かなくても怯み・停止を確かめられるように）---
	if (ImGui::SmallButton("Stagger")) {
		AttackTableSystem::Stagger(entity, kStaggerTestSeconds);
	}
	ImGui::SameLine();
	if (ImGui::SmallButton(status.isEnabled ? "Stop AI" : "Resume AI")) {
		AttackTableSystem::SetEnabled(entity, !status.isEnabled);
	}
}

// ===== 下の段 =====
void AttackTableWindow::DrawAttacks(Handle<Entity> entity) {
	const AttackTableComponent* component = EntityManager::Get<AttackTableComponent>(entity);
	const AttackTable* table = AttackTableManager::Get(component->table);
	std::vector<AttackCheck> checks;
	AttackTableSystem::GetAttackChecks(entity, checks);
	if (table == nullptr || checks.size() != table->attacks.size()) {
		return;
	}
	ImGui::SeparatorText("Attacks");
	constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
	if (!ImGui::BeginTable("##attackTable", 7, kFlags)) {
		return;
	}
	ImGui::TableSetupColumn("Attack");
	ImGui::TableSetupColumn("Chance");
	ImGui::TableSetupColumn("Phase");
	ImGui::TableSetupColumn("Distance");
	ImGui::TableSetupColumn("Cooldown");
	ImGui::TableSetupColumn("Repeat");
	ImGui::TableSetupColumn("");
	ImGui::TableHeadersRow();
	for (size_t i = 0; i < table->attacks.size(); ++i) {
		const AttackData& attack = table->attacks[i];
		const AttackCheck& check = checks[i];
		ImGui::PushID(static_cast<int>(i));
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextUnformatted(attack.name.c_str());
		ImGui::TableNextColumn();
		DrawCondition(check.probability > 0.0f, std::format("{:.1f}%", check.probability * 100.0f));
		ImGui::TableNextColumn();
		DrawCondition(check.isPhaseOk, check.isPhaseOk ? "OK" : "NG");
		ImGui::TableNextColumn();
		const std::string maxDistance = (attack.maxDistance > 0.0f) ? std::format("{:.1f}", attack.maxDistance) : "∞";
		DrawCondition(check.isDistanceOk, std::format("{:.1f}〜{}", attack.minDistance, maxDistance));
		ImGui::TableNextColumn();
		DrawCondition(check.isCooldownOk, check.isCooldownOk ? "OK" : std::format("{:.2f}", check.cooldownRemaining));
		ImGui::TableNextColumn();
		DrawCondition(check.isRepeatOk, (table->maxRepeat > 0) ? std::format("{}/{}", check.repeatCount, table->maxRepeat) : std::to_string(check.repeatCount));
		ImGui::TableNextColumn();
		if (ImGui::SmallButton("Next")) {
			AttackTableSystem::RequestAttack(entity, attack.name); // 次にこの攻撃を出す（同じ名前の行があると、先の行になる）
		}
		ImGui::SetItemTooltip("次に選ぶときに、この攻撃を出す");
		ImGui::PopID();
	}
	ImGui::EndTable();
}
#endif
```

### D-5 `HTTEngine/Editor/ImGuiManager.cpp`（2か所）

変更前：

```cpp
#include "HTTEngine/Editor/Widgets/ImeInput.h"
#include "HTTEngine/Editor/Windows/CodeWindow.h"
```

変更後：

```cpp
#include "HTTEngine/Editor/Widgets/ImeInput.h"
#include "HTTEngine/Editor/Windows/AttackTableWindow.h"
#include "HTTEngine/Editor/Windows/CodeWindow.h"
```

変更前：

```cpp
	CodeWindow::Draw();

	// --- シーンのファイルの確認（保存しますか？・名前を付けて保存など） ---
```

変更後：

```cpp
	CodeWindow::Draw();

	// --- 攻撃表の AI の様子（再生中の状態・抽選確率） ---
	AttackTableWindow::Draw();

	// --- シーンのファイルの確認（保存しますか？・名前を付けて保存など） ---
```

### D-6 `HTTEngine/Systems/EngineSystems.cpp`（2か所）

変更前：

```cpp
#include "HTTEngine/Editor/History/EditorHistory.h"
#include "HTTEngine/Editor/Windows/UIScreenEditor.h"
```

変更後：

```cpp
#include "HTTEngine/Editor/History/EditorHistory.h"
#include "HTTEngine/Editor/Inspector/AttackTableEditor.h"
#include "HTTEngine/Editor/Windows/UIScreenEditor.h"
```

変更前：

```cpp
	UIScreenEditor::Initialize(); // .uiscreen のファイルを Inspector に出す（Edit UI Screen のボタン）
#endif
```

変更後：

```cpp
	UIScreenEditor::Initialize(); // .uiscreen のファイルを Inspector に出す（Edit UI Screen のボタン）
	AttackTableEditor::Initialize(); // .attacktable のファイルを Inspector で直す
#endif
```

### D-7 Project ウィンドウの Create

`CreatePhysicMaterial` の中身を、Physic Material と攻撃表で共通の `CreateAssetFile` にする（同じ処理を2つ並べないため）。

`HTTEngine/Editor/Windows/ProjectWindow.h`：

変更前：

```cpp
#include <filesystem>
#include <string>
#include <vector>
```

変更後：

```cpp
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
```

変更前：

```cpp
	static void CreatePhysicMaterial(); // 今のフォルダに、既定の中身の .physmat を作る
```

変更後：

```cpp
	// 今のフォルダに、既定の中身のファイルを作る（Physic Material・攻撃表。create はファイルを作る関数）
	static void CreateAssetFile(std::string_view baseName, std::string_view extension, bool (*create)(const std::filesystem::path&));
```

`HTTEngine/Editor/Windows/ProjectWindowActions.cpp`（3か所）：

変更前：

```cpp
#include "HTTEngine/Physics/PhysicMaterialManager.h"
```

変更後：

```cpp
#include "HTTEngine/AI/AttackTableManager.h"
#include "HTTEngine/Physics/PhysicMaterialManager.h"
```

変更前：

```cpp
		if (ImGui::MenuItem("Physic Material")) {
			CreatePhysicMaterial();
		}
		ImGui::SetItemTooltip("今のフォルダに作ります（跳ね返り・摩擦。Collider の Physic Material で選ぶ）");
```

変更後：

```cpp
		if (ImGui::MenuItem("Physic Material")) {
			CreateAssetFile("NewPhysicMaterial", PhysicMaterialManager::kFileExtension, &PhysicMaterialManager::CreateNewFile);
		}
		ImGui::SetItemTooltip("今のフォルダに作ります（跳ね返り・摩擦。Collider の Physic Material で選ぶ）");
		if (ImGui::MenuItem("Attack Table")) {
			CreateAssetFile("NewAttackTable", AttackTableManager::kFileExtension, &AttackTableManager::CreateNewFile);
		}
		ImGui::SetItemTooltip("今のフォルダに作ります（敵のフェーズと攻撃。Attack Table の Table で選ぶ）");
```

変更前：

```cpp
// ===== Physic Material を作る =====
void ProjectWindow::CreatePhysicMaterial() {
	// まだ使われていない名前（NewPhysicMaterial.physmat・NewPhysicMaterial1.physmat …）。拡張子の前に番号を付ける
	std::filesystem::path path;
	std::error_code error;
	for (int number = 0;; ++number) {
		const std::string name = "NewPhysicMaterial" + (number == 0 ? std::string() : std::to_string(number));
		path = currentFolder_ / (name + std::string(PhysicMaterialManager::kFileExtension));
		if (!std::filesystem::exists(path, error)) {
			break;
		}
	}
	if (!PhysicMaterialManager::CreateNewFile(path)) {
		return;
	}
```

変更後：

```cpp
// ===== 既定の中身のファイルを作る =====
void ProjectWindow::CreateAssetFile(std::string_view baseName, std::string_view extension, bool (*create)(const std::filesystem::path&)) {
	// まだ使われていない名前（NewAttackTable.attacktable・NewAttackTable1.attacktable …）。拡張子の前に番号を付ける
	std::filesystem::path path;
	std::error_code error;
	for (int number = 0;; ++number) {
		const std::string name = std::string(baseName) + (number == 0 ? std::string() : std::to_string(number));
		path = currentFolder_ / (name + std::string(extension));
		if (!std::filesystem::exists(path, error)) {
			break;
		}
	}
	if (!create(path)) {
		return;
	}
```

**ここでビルドが通る。** Project ウィンドウの右クリック → Create に Attack Table が出て、Attack Table ウィンドウが開く。

---

## パートE：テスト用のボス（ゲーム側）

エンジンには入れない。今のゲームのプロジェクトに `Source/Scripts/BossTest/` を作って置く（プレイ会のゲームを作るときは消してよい）。

### E-1 `Source/Scripts/BossTest/BossTestAttacks.cpp`（新しいファイル）

```cpp
#include <cmath>
#include <numbers>

#include <HTTEngine/AI/AttackPart.h>

// ボスの AI を確かめるための、ゲーム側の攻撃部品（突進・全方向弾）と弾
// 新しい攻撃を足すときは、このファイルを見本にする

//=============================================================================
// 弾
//=============================================================================
// ===== 弾のデータ =====
struct BossTestBullet {
	Vector3 velocity = {0.0f, 0.0f, 0.0f}; // 1秒に進む量
	float lifeTime = 3.0f;                 // 残りの秒
};

COMPONENT(BossTestBullet, "BossTest") {
	ui.Field("Velocity", value.velocity);
	ui.Field("Life Time", value.lifeTime, AtLeast(0.0f));
}

// ===== 弾を動かす =====
void BossTestBulletUpdate(Handle<Entity> entity, BossTestBullet& bullet, TransformComponent& transform, float deltaTime) {
	transform.translation += bullet.velocity * deltaTime;
	bullet.lifeTime -= deltaTime;
	if (bullet.lifeTime <= 0.0f) {
		EntityManager::Destroy(entity);
	}
}
SYSTEM(BossTestBulletUpdate);

//=============================================================================
// 突進
//=============================================================================
/// <summary>
/// 予備動作で相手の向きを決めて、攻撃の間だけまっすぐ走る
/// </summary>
class BossTestDash : public AttackPart {
public:
	void OnStage(const AttackPartContext& context) override {
		// 攻撃の始めに向きを決める（走っている途中は曲がらない）
		if (context.stage == AttackStage::Active && context.target) {
			direction_ = Normalize(GetWorldPosition(context.target.transform) - GetWorldPosition(context.self.transform));
		}
	}

	void OnUpdate(const AttackPartContext& context) override {
		if (context.stage == AttackStage::Active) {
			context.self.transform.translation += direction_ * (speed_ * context.deltaTime);
		}
	}

	void Fields(ComponentUI& ui) override { ui.Field("Speed", speed_, AtLeast(0.0f).Tip("走る速さ（m/秒）")); }

private:
	float speed_ = 10.0f;                   // 走る速さ
	Vector3 direction_ = {0.0f, 0.0f, 1.0f}; // 走る向き（実行中だけの値なので、Fields に書かない）
};
ATTACK_PART(BossTestDash, "相手の向きへ、攻撃の間だけまっすぐ走る");

//=============================================================================
// 全方向弾
//=============================================================================
/// <summary>
/// 攻撃の始めに、周りへ弾を一斉に撃つ
/// </summary>
class BossTestRadial : public AttackPart {
public:
	void OnStage(const AttackPartContext& context) override {
		if (context.stage != AttackStage::Active) {
			return;
		}
		const Vector3 center = GetWorldPosition(context.self.transform);
		for (int i = 0; i < count_; ++i) {
			const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(count_);
			GameObject bullet = GameObject::Create("BossBullet");
			bullet.transform.translation = center;
			bullet.transform.scale = {0.3f, 0.3f, 0.3f};
			bullet.AddComponent<BossTestBullet>({{std::cos(angle) * speed_, 0.0f, std::sin(angle) * speed_}, 3.0f});
			bullet.AddComponent<PrimitiveRendererComponent>();
		}
	}

	void Fields(ComponentUI& ui) override {
		ui.Field("Count", count_, Range(1.0f, 128.0f).Tip("弾の数"));
		ui.Field("Speed", speed_, AtLeast(0.0f).Tip("弾の速さ（m/秒）"));
	}

private:
	int count_ = 16;     // 弾の数
	float speed_ = 6.0f; // 弾の速さ
};
ATTACK_PART(BossTestRadial, "攻撃の始めに、周りへ弾を一斉に撃つ");
```

### E-2 `Source/Scripts/BossTest/BossTestController.cpp`（新しいファイル）

```cpp
#include <algorithm>

#include <HTTEngine/ECS/Script.h>
#include <HTTEngine/Input/InputManager.h>

// ボスの AI を確かめるための Script。ボスの Entity に付ける
// ・HP を持って、Attack Table へ割合を渡す（H キーで減る）
// ・ボスの色を状態で変える（様子が画面で分かるように）
// ・隙（硬直・怯み）の間だけ F キーで必殺が当たる（必殺の発動判定の見本）
class BossTestController : public Script {
public:
	void Update() override {
		// --- HP を減らす（ゲームのダメージの代わり）---
		if (InputManager::IsKeyTriggered(DIK_H)) {
			Damage(10.0f);
		}
		// --- 必殺：隙の間だけ当たる ---
		const AttackTableStatus status = AttackTableSystem::GetStatus(gameObject.handle);
		const bool isOpening = (status.stage == AttackStage::Recovery) || (status.state == AttackTableState::Staggered);
		if (isOpening && InputManager::IsKeyTriggered(DIK_F)) {
			Time::HitStop(0.2f);
			Damage(30.0f);
			AttackTableSystem::Stagger(gameObject.handle, staggerSeconds_);
		}
		ApplyColor(status);
	}

	void Fields(ComponentUI& ui) override {
		ui.Field("Max HP", maxHp_, AtLeast(1.0f));
		ui.Field("Stagger Seconds", staggerSeconds_, AtLeast(0.0f).Tip("必殺が当たったときに怯む秒"));
	}

private:
	// HP を減らして、割合を Attack Table へ渡す（Component に書くだけ。切り替えは AttackTableSystem が見る）
	void Damage(float amount) {
		hp_ = (std::max)(hp_ - amount, 0.0f);
		if (AttackTableComponent* boss = GetComponent<AttackTableComponent>()) {
			boss->healthRatio = hp_ / maxHp_;
		}
	}

	// 状態ごとに色を変える
	void ApplyColor(const AttackTableStatus& status) {
		PrimitiveRendererComponent* renderer = GetComponent<PrimitiveRendererComponent>();
		if (renderer == nullptr) {
			return;
		}
		Vector4 color = {0.6f, 0.6f, 0.6f, 1.0f}; // Selecting
		switch (status.state) {
		case AttackTableState::Selecting:
			break;
		case AttackTableState::Attacking:
			switch (status.stage) {
			case AttackStage::None:
				break;
			case AttackStage::Windup:
				color = {1.0f, 0.8f, 0.2f, 1.0f}; // 黄：予備動作
				break;
			case AttackStage::Active:
				color = {1.0f, 0.2f, 0.2f, 1.0f}; // 赤：攻撃
				break;
			case AttackStage::Recovery:
				color = {0.2f, 0.5f, 1.0f, 1.0f}; // 青：硬直（必殺が当たる）
				break;
			}
			break;
		case AttackTableState::Staggered:
			color = {0.2f, 1.0f, 1.0f, 1.0f}; // 水色：怯み
			break;
		case AttackTableState::PhaseTransition:
			color = {1.0f, 0.2f, 1.0f, 1.0f}; // 紫：フェーズの移行
			break;
		case AttackTableState::Defeated:
			color = {0.1f, 0.1f, 0.1f, 1.0f}; // 黒：撃破
			break;
		}
		renderer->material.color = color;
	}

	float maxHp_ = 100.0f;         // 最大の HP
	float staggerSeconds_ = 1.5f;  // 必殺が当たったときに怯む秒
	float hp_ = 100.0f;            // 今の HP
};
SCRIPT(BossTestController, "BossTest");
```

### E-3 エディタで置く

1. Project ウィンドウで `resources/boss/` を作り、右クリック → Create → **Attack Table**。名前を `TestBoss` にする。
2. そのファイルを選ぶと Inspector に表が出る。次の通りに直す（つまむ・打つのが終わると保存される）。

| 場所 | 項目 | 値 |
|---|---|---|
| Phases | Add Phase → 2. の HP Threshold | 0.5 |
| Phases | 2. の Transition Seconds | 1 |
| Attacks | Add Attack → Name | Dash |
| Dash | Weight・Min Distance・Cooldown | 2・4・1 |
| Dash | Windup・Active・Recovery Seconds | 0.3・0.3・0.8 |
| Dash | Part・Speed | BossTestDash・15 |
| Attacks | Add Attack → Name | Radial |
| Radial | Weight | 1 |
| Radial | Windup・Active・Recovery Seconds | 0.5・0.2・1 |
| Radial | Part・Count | BossTestRadial・16 |

3. Hierarchy で Entity を2つ作る。
   - **Player**：Primitive Renderer を付け、Position を (8, 0, 0) にする。
   - **Boss**：Primitive Renderer（Shape は Sphere）・Attack Table・BossTestController を付ける。Attack Table の Table に `TestBoss.attacktable`、Target に Player を入れる。
4. カメラを上に置いて見下ろす（Position (0, 15, -10)・Rotation (55, 0, 0) くらい）。Save する。

### E-4 確かめる

**Play を押す。** Attack Table ウィンドウと、ボスの色を見る。

| 見る物 | 期待する動き |
|---|---|
| ボスの色 | 灰（選ぶ）→ 黄（予備動作）→ 赤（攻撃）→ 青（硬直）→ 灰 を繰り返す |
| Dash | 赤の間だけ Player へ走る。Player に 4m 以内に近づくと、Distance の欄が赤くなり 0% になる |
| Radial | 赤になった瞬間に弾が周りへ出る |
| Attack Table ウィンドウ | 状態・段階・残り時間が動く。Chance の合計が 100% になる。Dash の使用後は Cooldown の欄に残りの秒が赤く出る |

**時間を止める。**

| 操作 | 期待する動き |
|---|---|
| Control の一時停止 | ボスも弾も、残り時間も止まる。もう一度押すと続きから動く |
| Attack Table ウィンドウの Stop AI | 残り時間が止まり、Resume AI で続きから動く |
| Attack Table ウィンドウの Stagger | 攻撃が止まり、水色で1秒怯む |
| H キーを6回（HP 40%） | 攻撃の途中でも紫になり、1秒後に第2フェーズから動く |
| 青か水色の間に F キー | ヒットストップの後、怯んで HP が 30 減る（隙の間だけ当たる） |
| HP を 0 にする | 黒くなって止まる |

**表を直す。**

1. Play のまま `TestBoss.attacktable` を選び、Dash の Weight を 0 にする。Attack Table ウィンドウの Dash の Chance がすぐ 0% になる。
2. Stop して、もう一度開く。直した値が残っている（シーンの Play / Stop では戻らない）。
3. アプリを閉じて開き直しても、表が同じ値で出る。

---

## 新しい攻撃を足す手順

**エンジンは変更しない。** ゲーム側に .cpp を1つ足すだけでよい。

1. ゲームのプロジェクトに .cpp を足す。

```cpp
#include <HTTEngine/AI/AttackPart.h>

class LaserAttack : public AttackPart {
public:
	void OnBegin(const AttackPartContext& context) override {
		// 選ばれた（狙いを決める・音を鳴らす）
	}
	void OnStage(const AttackPartContext& context) override {
		// 段階に入った瞬間（context.stage で分ける）
	}
	void OnUpdate(const AttackPartContext& context) override {
		// 毎フレーム（context.deltaTime・context.GetStageProgress() を使う）
	}
	void OnEnd(const AttackPartContext& context, AttackPartEnd reason) override {
		// 終わった（出した物を片付ける。止められたときも呼ばれる）
	}
	void Fields(ComponentUI& ui) override {
		ui.Field("Width", width_, AtLeast(0.0f));
		ui.AssetField("Beam Prefab", beamPrefab_, AssetType::Prefab);
	}

private:
	float width_ = 1.0f;      // Fields に書いた値は、攻撃の行ごとに表へ保存される
	uint32_t beamPrefab_ = 0;
	GameObject beam_;         // 実行中だけの値は Fields に書かない
};
ATTACK_PART(LaserAttack, "太いビームを撃つ");
```

2. ビルドすると、攻撃表の Part の一覧に `LaserAttack` が出る。
3. 表の行で Part を選び、重み・距離・秒数を入れる。

### やってはいけないこと

```cpp
// ✗ 部品をエンジン（静的ライブラリ）に入れる
//    → 誰も参照していないのでリンカに捨てられ、Part の一覧に出ない。COMPONENT(...) と同じ。部品は必ずゲーム側に置く

// ✗ Fields に Entity への参照を書く
ui.Field("Target", target_); // ✗ 表はシーンの外のファイルなので、EntityId を持てない → context.target を使う

// ✗ 部品のクラスの名前を変える
//    → 保存した表の Part が読めなくなる（Inspector で赤く出る。選び直すと直る）
```

---

## ほかの機能とつなぐ

エンジンの窓口は全部 `AttackTableSystem`。Entity は `Handle<Entity>` で渡す（Script なら `gameObject.handle`）。

### ゲームから

```cpp
// ===== HP を渡す（フェーズの切り替えと撃破は、これだけで動く）=====
GetComponent<AttackTableComponent>()->healthRatio = hp_ / maxHp_;

// ===== 必殺を当ててよいか =====
const AttackTableStatus status = AttackTableSystem::GetStatus(boss.handle);
const bool isOpening = (status.stage == AttackStage::Recovery) || (status.state == AttackTableState::Staggered);

// ===== 怯ませる（撃破・フェーズの移行の間は false が返る）=====
AttackTableSystem::Stagger(boss.handle, 1.5f);

// ===== 状態が変わった瞬間を知る（前の値を覚えて比べる）=====
const AttackTableState state = AttackTableSystem::GetStatus(boss.handle).state;
if (state != lastState_) {
	lastState_ = state;
	if (state == AttackTableState::PhaseTransition) {
		// フェーズの移行の演出を始める
	}
}
```

### 時間との関係

| したいこと | 書き方 | AI はどうなるか |
|---|---|---|
| 全体を止める | `Time::SetGamePaused(true)`・`Time::SetGameTimeScale(0.0f)` | 止まる（部品も呼ばれない） |
| 一瞬止める | `Time::HitStop(0.2f)` | 止まる |
| スロー | `Time::SetGameTimeScale(0.3f)` | タイマー・部品の `deltaTime` も遅くなる |
| ボスだけ止める | `AttackTableSystem::SetEnabled(boss.handle, false)` | 止まる。プレイヤーは動ける |

### Timeline から（Timeline を作るときに呼ぶ入口）

Timeline の本体はこの md では作らない。必殺の演出で必要になる入口は、次の4つで足りる。

| Timeline でしたいこと | 呼ぶ物 |
|---|---|
| 演出の間、ボスを止める | `SetEnabled(boss, false)` → 終わったら `SetEnabled(boss, true)` |
| 演出の途中でボスを怯ませる | `Stagger(boss, 秒)` |
| 演出のあとに大技を出す | `RequestAttack(boss, "攻撃の名前")`（条件を見ずに、次に必ずこの攻撃） |
| 演出を始める条件を読む | `GetStatus(boss)` |

**フェーズの移行を Timeline に任せるとき：** 状態が `PhaseTransition` になったら `SetEnabled(false)` で止めて演出を流し、終わったら `SetEnabled(true)` にする。止めている間は移行のタイマーも止まる。表の Transition Seconds は、演出の後の間として使える。

`SetEnabled` の中身は bool 1つ。2か所から別々に止める場合は、呼ぶ側で数えること。

---

## 知っておくこと

- **`Stagger`・`RequestAttack` は、再生が始まって最初の `AttackTableSystem::Update` より前は `false`。** Script の `Start` で呼ぶと効かないことがある。
- **同じ名前の攻撃が2行あると、`RequestAttack` は上の行を選ぶ。** 名前は重ねない。
- **`RequestAttack` はすぐには始まらない。** 次に選ぶときに使う。今の攻撃を止めたいときは、先に `Stagger` する。
- **怯みの間にも `Stagger` を受け付ける。** 残りの長い方が残る。何回も怯ませたくないなら、呼ぶ側で状態を見る。
- **`OnEnd` はアプリの終了のときには呼ばれない。** Stop・シーンの切り替え・敵の Destroy では呼ばれる。
- **Target が居ないと距離は無限大になる。** Max Distance のある攻撃は使えなくなる。
- **使える攻撃が1つも無いと、Selecting で待ち続ける。** 敵を動かしたいときは、Script で `status.state == AttackTableState::Selecting` の間に近づける。
- **表のファイルは `resources/` の下に置く。** 製品版でもそこから読む。
