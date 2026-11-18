#pragma once
#include <array>
#include <vector>
#include <cstdint>
#include <string>
#include <string_view>
#include <filesystem>

#include "HTTEngine/Components/Gameplay/AttackTableComponent.h"

// 前方宣言
class ComponentUI;

constexpr size_t kMaxAttackPhases = 8; // フェーズの数の上限（攻撃ごとの使えるフェーズを決まった大きさの配列で持つため）


// フェーズの数分boolをtrueに初期化
constexpr std::array<bool, kMaxAttackPhases> MakeAllPhases() { 
	std::array<bool, kMaxAttackPhases> phases{};
	phases.fill(true);
	return phases;
}

// ===== 選んだ攻撃部品の1つ。攻撃の部品・フェーズの待ちの部品が使用 =====
struct AttackPartSlot {
	std::string name;          // ATTACK_PART(...)の名前
	std::string fields = "{}"; // 部品のFieldsに書いた値
	std::string step;          // 攻撃の部品だけ：動くステップの名前（空なら攻撃の間ずっと動く。待ちの部品では使わない）

	bool operator==(const AttackPartSlot&) const = default;
};

// ===== ステップが終わった後の行き先の決め方 =====
enum class AttackStepJump {
	Next, // 1つ下のステップ（一番下のステップなら攻撃を終える）
	End,  // 攻撃を終える
	Step, // 名前で選んだステップ
};

// ===== ステップが終わった後の行き先 =====
struct AttackStepLink {
	AttackStepJump jump = AttackStepJump::Next; // 決め方
	std::string step;                           // jumpがStepのときの名前

	bool operator==(const AttackStepLink&) const = default;
};

// ===== 攻撃のステップ1つ。攻撃はステップを上から順に進める =====
struct AttackStepData {
	std::string name = "Step";                       // 名前（確認の画面・行き先・context・stepNameに使う）
	AttackStage stage = AttackStage::Active;         // 種類の目印（確認の画面・GetStatus・SyncHitboxが見る）
	float seconds = 0.5f;                            // 長さ。部品がFinishStep・FailStepを呼べば、これより速く終わる（どのステップも必ずこの秒で終わる）
	AttackStepLink next;                             // 時間が来た・部品がFinishStepを呼んだときの行き先
	AttackStepLink fail = {AttackStepJump::End, ""}; // 部品がFailStepを呼んだときの行き先

	bool operator==(const AttackStepData&) const = default;
};

// 新しい攻撃の行のステップ
std::vector<AttackStepData> MakeDefaultAttackSteps();


// ===== フェーズ1つ =====
struct AttackPhaseData {
	std::string name = "Phase";            // 名前
	float healthThreshold = 0.5f;          // HPの割合がこの値以下になったら次のフェーズに移る
	float transitionSeconds = 2.0f;        // このフェーズに移るときの長さ
	float minIdleSeconds = 0.0f;           // 次の攻撃を選ぶまでの最短の秒
	std::vector<AttackPartSlot> idleParts; // 待ちの部品。Selectingの間だけ動かす

	bool operator==(const AttackPhaseData&) const = default;
};

// ===== 攻撃1行 =====
struct AttackData {
	std::string name = "Attack";                                       // 名前
	float weight = 1.0f;                                               // 抽選の重み
	float minDistance = 0.0f;                                          // 相手との距離がこれより近いと使わない
	float maxDistance = 0.0f;                                          // 相手との距離がこれより遠いと使わない（0 なら上限なし）
	float cooldown = 0.0f;                                             // 終わってから、次に使えるようになるまでの秒
	std::array<bool, kMaxAttackPhases> usablePhases = MakeAllPhases(); // [フェーズの番号]使えるか
	std::vector<AttackPartSlot> parts;                                 // 部品（それぞれの step のステップの間だけ動く。step が空なら攻撃の間ずっと。並びの順に呼ぶ）
	std::vector<AttackStepData> steps = MakeDefaultAttackSteps();      // ステップ（上から順に進める）

	bool operator==(const AttackData&) const = default;
};

// ===== 攻撃表（ファイルの1つの中身）。フェーズは必ず1つ以上ある =====
struct AttackTable {
	int maxRepeat = 2; // 同じ攻撃を続けてよい回数。0なら制限なし
	std::vector<AttackPhaseData> phases = {
	    {"Phase 1", 1.0f, 0.0f}
    };
	std::vector<AttackData> attacks;

	bool operator==(const AttackTable&) const = default;
};



/// <summary>
/// 攻撃表のファイル（.attacktable）を読む・持つ・保存する
/// </summary>
class AttackTableManager {
public:
	static constexpr std::string_view kFileExtension = ".attacktable"; // 攻撃表のファイルの拡張子

	static void Initialize();
	static void Release();

	/// <summary>
	/// ファイルを読む。同じファイルなら、前に呼んだ番号を返す
	/// </summary>
	static uint32_t Load(const std::filesystem::path& filePath);

	// 番号からパスを引く（知らない番号なら空）
	static const std::string& GetPath(uint32_t handle);

	// 中身。ポインタは使い捨て
	static const AttackTable* Get(uint32_t handle);

	// 中身を書き換える（Inspectorから）
	static void Set(uint32_t handle, const AttackTable& table);

	// 中身の版（AttackTableSystemが部品の値を読み直すか決めるのに使う）
	static uint32_t GetRevision(uint32_t handle);

	// ファイルに書く
	static bool Save(uint32_t handle);

	// 新しいファイルを既定の中身で作る
	static bool CreateNewFile(const std::filesystem::path& filePath);

	// ===== 見せ方（Inspectorとファイルの両方がこの並びを使う）=====
	static void DescribeTable(ComponentUI& ui, AttackTable& value);                   // 表全体の値
	static void DescribePhase(ComponentUI& ui, AttackPhaseData& value, bool isFirst); // フェーズ1つ
	static void DescribeAttack(ComponentUI& ui, AttackData& value, const std::vector<AttackPhaseData>& phases); // 攻撃1行
	static void DescribeStep(ComponentUI& ui, AttackStepData& value);                 // ステップ1つ
};