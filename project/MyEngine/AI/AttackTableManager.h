#pragma once
#include <array>
#include <vector>
#include <cstdint>
#include <string>
#include <string_view>
#include <filesystem>

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
/// 選んだ攻撃部品1つ（部品の名前と、その部品の Fields に書いた値）。攻撃の行と、フェーズの待ちの部品が使う
/// </summary>
struct AttackPartSlot {
	std::string name;          // ATTACK_PART(...) の名前（空なら部品を使わない）
	std::string fields = "{}"; // 部品の Fields に書いた値（JSON の文字）

	bool operator==(const AttackPartSlot&) const = default;
};

/// <summary>
/// フェーズ1つ
/// </summary>
struct AttackPhaseData {
	std::string name = "Phase";     // 名前（確認の画面に出す）
	float healthThreshold = 0.5f;   // HP の割合がこの値以下になったら、このフェーズへ移る（一番上のフェーズでは使わない）
	float transitionSeconds = 2.0f; // このフェーズへ移るときの長さ（0 なら移行の状態を飛ばす）
	float minIdleSeconds = 0.0f;    // 次の攻撃を選ぶまでの最短の秒（Selecting に入るたびに 0 から数える。0 ならすぐ選ぶ）
	AttackPartSlot idlePart;        // 待ちの部品（Selecting の間だけ動かす。空なら何もしない）

	bool operator==(const AttackPhaseData&) const = default;
};

/// <summary>
/// 攻撃1行
/// </summary>
struct AttackData {
	std::string name = "Attack";   // 名前（確認の画面・RequestAttack に使う）
	AttackPartSlot part;           // 攻撃部品（空なら時間が進むだけ）
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
	int maxRepeat = 2;            // 同じ攻撃を続けてよい回数（これを超える攻撃は、ほかに使える攻撃がある間は選ばない。0 なら制限なし）
	uint32_t sampleAnimation = 0; // 部品の Clip の候補を出すためだけのアニメーションのファイル（AnimationManager の番号。保存はパス）。実際に流すのは敵の Animator のファイル
	std::vector<AttackPhaseData> phases = {
	    {"Phase 1", 1.0f, 0.0f}
    };
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
	// 表全体の値・フェーズ1つ・攻撃1行。部品（攻撃の Part・フェーズの Idle Part）と部品の値は、形が違うので別に扱う（選択肢が実行中に決まる）
	static void DescribeTable(ComponentUI& ui, AttackTable& value);
	static void DescribePhase(ComponentUI& ui, AttackPhaseData& value, bool isFirst);
	static void DescribeAttack(ComponentUI& ui, AttackData& value, const std::vector<AttackPhaseData>& phases);
};