#include "AttackTableSystem.h"

#include <set>
#include <cmath>
#include <format>
#include <limits>
#include <memory>
#include <algorithm>

#include <externals/nlohmann/json.hpp>

#include "HTTEngine/AI/AttackTableManager.h"
#include "HTTEngine/AI/AttackPart.h"
#include "HTTEngine/Components/Combat/HealthComponent.h"
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
	std::vector<std::unique_ptr<AttackPart>> parts;     // [攻撃の行] 攻撃部品（Part が空・知らない名前なら nullptr）
	std::vector<std::unique_ptr<AttackPart>> idleParts; // [フェーズ] 待ちの部品（Idle Part が空・知らない名前なら nullptr）
	std::vector<float> cooldowns;                       // [攻撃の行] クールタイムの残りの秒

	AttackTableState state = AttackTableState::Selecting; // 状態
	AttackStage stage = AttackStage::None;                // 攻撃の段階
	int phase = 0;                                        // 今のフェーズ
	int attack = kNoAttack;                               // 今の攻撃の行
	int lastAttack = kNoAttack;                           // 最後に選んだ攻撃の行（同じ攻撃が続いた回数を数える）
	int repeatCount = 0;                                  // lastAttack を続けて選んだ回数
	int requestedAttack = kNoAttack;                      // RequestAttack で決められた次の攻撃の行
	bool isIdleBegun = false;                             // 待ちの部品の OnBegin を呼んで、まだ OnEnd を呼んでいない（Selecting の間だけ true になる）
	float time = 0.0f;                                    // 今の段階・状態に入ってからの秒
	float duration = 0.0f;                                // 今の段階・状態の長さ（Selecting はフェーズの Min Idle Seconds）
};

std::vector<AttackTableRuntime> runtimes; // 動いている敵（数が少ないので、一列に並べて順に探す）

// ===== その敵の実行中の値（まだ動いていなければ nullptr）=====
AttackTableRuntime* FindRuntime(Handle<Entity> entity) {
	const auto found = std::ranges::find(runtimes, entity, &AttackTableRuntime::entity);
	return (found != runtimes.end()) ? &*found : nullptr;
}

//=============================================================================
// 攻撃部品を作る・値を読む
//=============================================================================
// ===== 部品を作る（Part が空・知らない名前なら nullptr。知らない名前は警告を出す）=====
// owner は警告に出す持ち主（「攻撃 "Dash"」「フェーズ "Phase 1" の待ち」）
std::unique_ptr<AttackPart> CreatePart(const AttackPartSlot& slot, std::string_view owner) {
	std::unique_ptr<AttackPart> part = AttackPartRegistry::Create(slot.name);
	if (part == nullptr && !slot.name.empty()) {
		LogManager::Warning(std::format("{}の部品 \"{}\" がありません（ATTACK_PART(...) を確かめてください）。部品なしで動かします", owner, slot.name));
	}
	return part;
}

// ===== 部品の値を読む（label は警告に出す名前）=====
void ReadPartFields(AttackPart& part, const AttackPartSlot& slot, std::string_view label, std::string_view tablePath) {
	const SceneJson json = SceneJson::parse(slot.fields, nullptr, false);
	std::set<std::string> warnings;
	ComponentSerializer::ReadFields(label, [&part](ComponentUI& ui) { part.Fields(ui); }, json, warnings);
	for (const std::string& warning : warnings) {
		LogManager::Warning(std::format("{}: {}", tablePath, warning));
	}
}

// ===== 攻撃の行と、フェーズの待ちの部品の値を、全部読む =====
void ReadAllPartFields(AttackTableRuntime& runtime, const AttackTable& table) {
	const std::string& tablePath = AttackTableManager::GetPath(runtime.table);
	for (size_t i = 0; i < runtime.parts.size(); ++i) {
		if (runtime.parts[i] != nullptr) {
			ReadPartFields(*runtime.parts[i], table.attacks[i].part, table.attacks[i].name, tablePath);
		}
	}
	for (size_t i = 0; i < runtime.idleParts.size(); ++i) {
		if (runtime.idleParts[i] != nullptr) {
			ReadPartFields(*runtime.idleParts[i], table.phases[i].idlePart, table.phases[i].name, tablePath);
		}
	}
}

//=============================================================================
// フェーズ
//=============================================================================
// ===== HP の割合から、今いるべきフェーズを決める（今より前には戻らない。一度に2つ越えたら、先の方）=====
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
// ===== 攻撃部品に渡す今の様子を作る =====
AttackPartContext MakeContext(const AttackTableRuntime& runtime, const AttackTableComponent& component, float deltaTime) {
	AttackPartContext context;
	context.self = GameObject{runtime.entity};
	context.target = GameObject{EntityManager::Find(component.target)};
	context.state = runtime.state;
	context.stage = runtime.stage;
	context.stageTime = runtime.time;
	context.stageDuration = runtime.duration;
	context.deltaTime = deltaTime;
	context.phase = runtime.phase;
	return context;
}

// ===== 状態を変える（タイマーも最初から）=====
void EnterState(AttackTableRuntime& runtime, AttackTableState state, float duration) {
	runtime.state = state;
	runtime.stage = AttackStage::None;
	runtime.time = 0.0f;
	runtime.duration = duration;
}

// ===== 選ぶ状態へ（長さはフェーズの Min Idle Seconds）=====
// 待ちの部品の OnBegin はここでは呼ばず、次に時間が進むフレームの UpdateSelecting で呼ぶ（AI が止まっている・時間が 0 のフレームに呼ばないため）
void EnterSelecting(AttackTableRuntime& runtime, const AttackTable& table) { EnterState(runtime, AttackTableState::Selecting, table.phases[runtime.phase].minIdleSeconds); }

// ===== 段階の長さ =====
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

// ===== 段階に入る =====
void EnterStage(AttackTableRuntime& runtime, const AttackTableComponent& component, const AttackData& attack, AttackStage stage, float deltaTime) {
	runtime.stage = stage;
	runtime.time = 0.0f;
	runtime.duration = StageSeconds(attack, stage);
	if (AttackPart* part = runtime.parts[runtime.attack].get()) {
		part->OnStage(MakeContext(runtime, component, deltaTime));
	}
}

// ===== 攻撃を始める（同じ攻撃が続いた回数も、ここで数える）=====
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

// ===== 今の攻撃の部品へ、終わったことを知らせる（攻撃していなければ何もしない）=====
void NotifyAttackEnd(AttackTableRuntime& runtime, const AttackTableComponent& component, AttackPartEnd reason, float deltaTime) {
	if (runtime.attack == kNoAttack) {
		return;
	}
	if (AttackPart* part = runtime.parts[runtime.attack].get()) {
		part->OnEnd(MakeContext(runtime, component, deltaTime), reason);
	}
}

// ===== 攻撃を終える（クールタイムは、終わったときから数える）=====
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
// 待ちの部品・止める
//=============================================================================
// ===== 今のフェーズの待ちの部品（Idle Part が空・知らない名前なら nullptr）=====
AttackPart* GetIdlePart(const AttackTableRuntime& runtime) {
	const size_t phase = static_cast<size_t>(runtime.phase);
	return (phase < runtime.idleParts.size()) ? runtime.idleParts[phase].get() : nullptr;
}

// ===== 待ちの部品へ、終わったことを知らせる（OnBegin を呼んでいなければ何もしない）=====
void EndIdle(AttackTableRuntime& runtime, const AttackTableComponent& component, AttackPartEnd reason, float deltaTime) {
	if (!runtime.isIdleBegun) {
		return;
	}
	runtime.isIdleBegun = false; // OnEnd より先に下ろす（OnEnd の中で Stagger を呼ばれても、2回知らせないように）
	if (AttackPart* part = GetIdlePart(runtime)) {
		part->OnEnd(MakeContext(runtime, component, deltaTime), reason);
	}
}

// ===== 今動いている部品（待ちか攻撃）を止める（怯み・フェーズの移行・撃破。攻撃のクールタイムも数える）=====
void InterruptCurrent(AttackTableRuntime& runtime, const AttackTableComponent& component, const AttackTable& table, float deltaTime) {
	EndIdle(runtime, component, AttackPartEnd::Interrupted, deltaTime);
	EndAttack(runtime, component, table, AttackPartEnd::Interrupted, deltaTime);
}

// ===== 今動いている部品（待ちか攻撃）へ、止められたことだけを知らせる（表の作り直し・敵が消えるとき。この後で実行中の値ごと捨てる）=====
void NotifyInterrupted(AttackTableRuntime& runtime, const AttackTableComponent& component) {
	EndIdle(runtime, component, AttackPartEnd::Interrupted, 0.0f);
	NotifyAttackEnd(runtime, component, AttackPartEnd::Interrupted, 0.0f);
}

//=============================================================================
// 攻撃の抽選
//=============================================================================
// ===== 行ごとに条件を調べて、選ばれる確率まで入れる（抽選と確認の画面がこれ1つを使うので、画面の確率と本当の抽選がずれない）=====
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

// ===== 確率に合わせて1つ選ぶ（選べなければ kNoAttack）=====
int PickAttack(const std::vector<AttackCheck>& checks) {
	// 0〜1 の乱数を、確率の幅で並べた帯のどこに落ちたかで選ぶ
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
// ===== 表に合わせて攻撃部品・待ちの部品を作る =====
void Build(AttackTableRuntime& runtime, const AttackTableComponent& component, const AttackTable& table) {
	runtime.table = component.table;
	runtime.revision = AttackTableManager::GetRevision(component.table);
	runtime.parts.clear();
	for (const AttackData& attack : table.attacks) {
		runtime.parts.push_back(CreatePart(attack.part, std::format("攻撃 \"{}\" ", attack.name)));
	}
	runtime.idleParts.clear();
	for (const AttackPhaseData& phase : table.phases) {
		runtime.idleParts.push_back(CreatePart(phase.idlePart, std::format("フェーズ \"{}\" の待ち", phase.name)));
	}
	ReadAllPartFields(runtime, table);
	runtime.cooldowns.assign(table.attacks.size(), 0.0f);
	runtime.attack = kNoAttack;
	runtime.lastAttack = kNoAttack;
	runtime.repeatCount = 0;
	runtime.requestedAttack = kNoAttack;
	runtime.isIdleBegun = false;
	runtime.phase = CalcPhase(table, 0, component.healthRatio); // 始めから HP が減っていれば、移行の状態を飛ばしてそのフェーズから
	EnterSelecting(runtime, table);
}

// ===== その敵の実行中の値を、今の Component に合わせて返す（表が選ばれていなければ nullptr）=====
AttackTableRuntime* PrepareRuntime(Handle<Entity> entity, const AttackTableComponent& component, const AttackTable& table) {
	AttackTableRuntime* runtime = FindRuntime(entity);
	if (runtime == nullptr) {
		runtime = &runtimes.emplace_back();
		runtime->entity = entity;
		Build(*runtime, component, table);
		return runtime;
	}
	// --- 表が替わった・攻撃の行やフェーズの数が変わった：今の部品を止めて作り直す ---
	if (runtime->table != component.table || runtime->parts.size() != table.attacks.size() || runtime->idleParts.size() != table.phases.size()) {
		NotifyInterrupted(*runtime, component); // 表はもう新しい物なので、クールタイムは数えない（Build で全部 0 に戻る）
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
		InterruptCurrent(runtime, component, table, deltaTime);
		EnterState(runtime, AttackTableState::Defeated, 0.0f);
		return true;
	}
	const int phase = CalcPhase(table, runtime.phase, component.healthRatio);
	if (phase == runtime.phase) {
		return false;
	}
	InterruptCurrent(runtime, component, table, deltaTime); // 待ちの部品は、移る前のフェーズの物を止める
	runtime.phase = phase;
	EnterState(runtime, AttackTableState::PhaseTransition, table.phases[phase].transitionSeconds);
	return true;
}

// ===== 選ぶ =====
// Min Idle Seconds が過ぎるまでと、使える攻撃が無い間は、待ちの部品を動かして待つ
void UpdateSelecting(AttackTableRuntime& runtime, const AttackTableComponent& component, const AttackTable& table, float deltaTime) {
	runtime.duration = table.phases[runtime.phase].minIdleSeconds; // 再生中に秒を直しても、すぐ効くように
	AttackPart* idle = GetIdlePart(runtime);
	// --- Selecting の最初のフレーム：待ちの部品を始める ---
	if (!runtime.isIdleBegun) {
		runtime.isIdleBegun = true;
		if (idle != nullptr) {
			idle->OnBegin(MakeContext(runtime, component, deltaTime));
		}
		// OnBegin の中で Stagger を呼ばれたら、待ちはもう終わっている
		if (runtime.state != AttackTableState::Selecting) {
			return;
		}
	}
	// --- 待ちの部品を進める ---
	runtime.time += deltaTime;
	if (idle != nullptr) {
		idle->OnUpdate(MakeContext(runtime, component, deltaTime));
	}
	if (runtime.state != AttackTableState::Selecting) {
		return; // OnUpdate の中で Stagger を呼ばれた
	}
	// --- 選ぶ（RequestAttack で決められた攻撃は、Min Idle Seconds を待たない）---
	int attack = runtime.requestedAttack;
	if (attack == kNoAttack && runtime.time >= runtime.duration) {
		static std::vector<AttackCheck> checks; // 毎フレーム配列を作り直さないように使い回す
		CheckAttacks(runtime, table, CalcDistance(runtime.entity, component), checks);
		attack = PickAttack(checks);
	}
	if (attack == kNoAttack) {
		return; // まだ待つ
	}
	// --- 待ちを終えてから、攻撃を始める（待ちの部品が書いた速さなどを、攻撃の OnBegin より先に片付けられるように）---
	EndIdle(runtime, component, AttackPartEnd::Finished, deltaTime);
	if (runtime.state != AttackTableState::Selecting) {
		return; // OnEnd の中で Stagger を呼ばれた（RequestAttack の分は、次に選ぶときへ残す）
	}
	runtime.requestedAttack = kNoAttack;
	BeginAttack(runtime, component, table, attack, deltaTime);
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
		EnterSelecting(runtime, table);
		break;
	}
}

// ===== 怯み・フェーズの移行：時間が来たら選ぶ状態へ =====
void UpdateTimedState(AttackTableRuntime& runtime, const AttackTable& table, float deltaTime) {
	runtime.time += deltaTime;
	if (runtime.time >= runtime.duration) {
		EnterSelecting(runtime, table);
	}
}

// ===== 敵1体 =====
void UpdateEntity(Handle<Entity> entity, const AttackTableComponent& component, float deltaTime) {
	// --- 早期リターン ---
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

	// --- 状態ごとに行動 ---
	switch (runtime->state) {
	case AttackTableState::Selecting:
		UpdateSelecting(*runtime, component, *table, deltaTime);
		break;
	case AttackTableState::Attacking:
		UpdateAttacking(*runtime, component, *table, deltaTime);
		break;
	case AttackTableState::Staggered:
	case AttackTableState::PhaseTransition:
		UpdateTimedState(*runtime, *table, deltaTime);
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
	ui.Field("Health Ratio###healthRatio", value.healthRatio, Range(0.0f, 1.0f).Tip("残りの HP の割合（0〜1）。同じ Entity に Health があれば、再生中は毎フレームそこから書かれる。無ければゲームが書く。再生中につまむと、フェーズの移行と撃破を試せる（Health があるときは Health の値をつまむ）"));
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
	EntityManager::ForEach<AttackTableComponent>([deltaTime](Handle<Entity> entity, AttackTableComponent& component) {
		// 同じ Entity に Health があれば、HP の割合はそこから読む（ゲームが毎フレーム書かなくてよい。書き忘れ・書く順番のずれが出ない）
		if (const HealthComponent* health = EntityManager::Get<HealthComponent>(entity)) {
			component.healthRatio = health->GetRatio();
		}
		UpdateEntity(entity, component, deltaTime);
	});
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
	InterruptCurrent(*runtime, *component, *table, 0.0f); // 待ちの部品・攻撃の部品のどちらも止める
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
		NotifyInterrupted(*runtime, *component); // Component はまだ読める。部品が出した物を片付けられるように
	}
	std::erase_if(runtimes, [entity](const AttackTableRuntime& other) { return other.entity == entity; });
}