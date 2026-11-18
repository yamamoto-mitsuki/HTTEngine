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
#include "HTTEngine/AI/BuiltInAttackParts.h"
#include "HTTEngine/Components/Combat/HealthComponent.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/Math/RandomEngine.h"

namespace {
constexpr int kNoAttack = -1; // 攻撃を指していない番号
constexpr int kNoStep = -1;   // ステップを指していない番号
constexpr int kWholeAttack = -1; // 部品の動く所：攻撃の間ずっと（部品の step が空）
constexpr int kNowhere = -2;     // 部品の動く所：無い（部品の step の名前のステップが無い）
using PartList = std::vector<std::unique_ptr<AttackPart>>; // 部品の並び（名前が空・知らない名前の所はnullptr）

//=============================================================================
// 敵1体分の実行中の値
//=============================================================================
// ===== 攻撃1行の部品の実体 =====
struct AttackPartSet {
	PartList parts;          // 部品（表の attack.parts と同じ並び）
	std::vector<int> stepOf; // [部品]動くステップの番号（kWholeAttack・kNowhere もある）
	size_t stepCount = 0;    // 作ったときのステップの数
};

struct AttackTableRuntime {
	Handle<Entity> entity;              // どの敵か
	uint32_t table = 0;                 // 作ったときの攻撃表（Component の表が替わったら作り直す）
	uint32_t revision = 0;              // 読んだ攻撃表の版（変わったら部品の値を読み直す）
	std::vector<AttackPartSet> attacks; // [攻撃]部品の実体
	std::vector<PartList> idleParts;    // [フェーズ]待ちの部品
	std::vector<float> cooldowns;       // [攻撃]クールタイムの残りの行

	AttackTableState state = AttackTableState::Selecting; // 状態
	int phase = 0;                                        // 今のフェーズ
	int attack = kNoAttack;                               // 今の攻撃の行
	int step = kNoStep;                                   // 今のステップ（ステップの部品のOnBeginを呼んで、まだOnEndを呼んでいない間だけkNoStep以外）
	int lastAttack = kNoAttack;                           // 最後に選んだ攻撃の行（同じ攻撃が続いた回数を数える）
	int repeatCount = 0;                                  // lastAttackを続けて選んだ回数
	int requestedAttack = kNoAttack;                      // RequestAttack で決められた次の攻撃の行
	bool isIdleBegun = false;                             // 待ちの部品の OnBegin を呼んで、まだ OnEnd を呼んでいない（Selecting の間だけtrueになる）
	float time = 0.0f;                                    // 今のステップ・状態に入ってからの秒
	float duration = 0.0f;                                // 今のステップ・状態の長さ（ステップは Seconds、Selecting はフェーズの Min Idle Seconds）
	float attackTime = 0.0f;                              // 今の攻撃が始まってからの秒

	int partCallDepth = 0;       // 部品を呼んでいる途中なら1以上（PartCallScope が数える）
	float pendingStagger = 0.0f; // 部品の中から呼ばれたStaggeの秒（部品の呼び出しが終わってから効かせる。0 なら無し）
};
std::vector<AttackTableRuntime> runtimes; // 動いている敵

// ===== その敵の実行中の値（まだ動いていなければnullptr）=====
AttackTableRuntime* FindRuntime(Handle<Entity> entity) {
	const auto found = std::ranges::find(runtimes, entity, &AttackTableRuntime::entity);
	return (found != runtimes.end()) ? &*found : nullptr;
}

/// <summary>
/// 部品を呼んでいる間の印。この間に部品から Stagger を呼ばれたら、印が外れてから怯ませる
/// <para>呼び出しの途中で怯むと、呼んでいる途中の部品の OnEnd が先に来たり、OnEnd が2回来たりするため</para>
/// </summary>
class PartCallScope {
public:
	explicit PartCallScope(AttackTableRuntime& runtime) : runtime_(runtime) { ++runtime_.partCallDepth; }
	~PartCallScope() { --runtime_.partCallDepth; }
	PartCallScope(const PartCallScope&) = delete;
	PartCallScope& operator=(const PartCallScope&) = delete;

private:
	AttackTableRuntime& runtime_;
};

// ===== 並びの部品に、順に同じことをする（nullptr は飛ばす）=====
template<class Function> 
void ForEachPart(const PartList& parts, Function function) {
	for (const std::unique_ptr<AttackPart>& part : parts) {
		if (part != nullptr) {
			function(*part);
		}
	}
}

// ===== 部品のうち、動く所が where の物に、順に同じことをする（where はステップの番号か kWholeAttack）=====
template<class Function>
void ForEachPartIn(const AttackPartSet& set, int where, Function function) {
	for (size_t i = 0; i < set.parts.size(); ++i) {
		if (set.parts[i] != nullptr && set.stepOf[i] == where) {
			function(*set.parts[i]);
		}
	}
}

// ===== 部品ごとの動く所（部品の step の名前を、ステップの番号にする）=====
std::vector<int> PlaceParts(const AttackData& attack) {
	std::vector<int> stepOf;
	stepOf.reserve(attack.parts.size());
	for (const AttackPartSlot& slot : attack.parts) {
		if (slot.step.empty()) {
			stepOf.push_back(kWholeAttack);
			continue;
		}
		const auto found = std::ranges::find(attack.steps, slot.step, &AttackStepData::name);
		stepOf.push_back((found != attack.steps.end()) ? static_cast<int>(found - attack.steps.begin()) : kNowhere);
	}
	return stepOf;
}

// ===== ステップのデータ =====
const AttackStepData* FindStepData(const AttackTable& table, int attack, int step) {
	if (attack < 0 || static_cast<size_t>(attack) >= table.attacks.size()) {
		return nullptr;
	}
	const std::vector<AttackStepData>& steps = table.attacks[attack].steps;
	return (step >= 0 && static_cast<size_t>(step) < steps.size()) ? &steps[step] : nullptr;
}

//=============================================================================
// 攻撃部品を作る・値を読む
//=============================================================================
// ===== 部品の並びを作る =====
PartList CreateParts(const std::vector<AttackPartSlot>& slots, std::string_view owner) {
	PartList parts;
	for (const AttackPartSlot& slot : slots) {
		std::unique_ptr<AttackPart> part = AttackPartRegistry::Create(slot.name);
		if (part == nullptr && !slot.name.empty()) {
			LogManager::Warning(std::format("{}の部品 \"{}\" がありません（ATTACK_PART(...) を確かめてください）。部品なしで動かします", owner, slot.name));
		}
		parts.push_back(std::move(part));
	}
	return parts;
}

// ===== 部品の並びの値を読む（label は警告に出す名前）=====
void ReadPartFields(PartList& parts, const std::vector<AttackPartSlot>& slots, std::string_view label, std::string_view tablePath) {
	for (size_t i = 0; i < parts.size() && i < slots.size(); ++i) {
		if (parts[i] == nullptr) {
			continue;
		}
		const SceneJson json = SceneJson::parse(slots[i].fields, nullptr, false);
		std::set<std::string> warnings;
		ComponentSerializer::ReadFields(label, [&part = *parts[i]](ComponentUI& ui) { part.Fields(ui); }, json, warnings);
		for (const std::string& warning : warnings) {
			LogManager::Warning(std::format("{}: {}", tablePath, warning));
		}
	}
}

// ===== 全部の部品の値を読む =====
void ReadAllPartFields(AttackTableRuntime& runtime, const AttackTable& table) {
	const std::string& tablePath = AttackTableManager::GetPath(runtime.table);
	for (size_t i = 0; i < runtime.attacks.size(); ++i) {
		const AttackData& attack = table.attacks[i];
		AttackPartSet& set = runtime.attacks[i];
		ReadPartFields(set.parts, attack.parts, attack.name, tablePath);
	}
	for (size_t i = 0; i < runtime.idleParts.size(); ++i) {
		ReadPartFields(runtime.idleParts[i], table.phases[i].idleParts, table.phases[i].name, tablePath);
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
// 攻撃部品に渡す様子
//=============================================================================
AttackPartContext MakeContext(const AttackTableRuntime& runtime, const AttackTableComponent& component, const AttackTable& table, float deltaTime) {
	AttackPartContext context;
	context.self = GameObject{runtime.entity};
	context.target = GameObject{EntityManager::Find(component.target)};
	context.state = runtime.state;
	context.stepTime = runtime.time;
	context.stepDuration = runtime.duration;
	context.deltaTime = deltaTime;
	context.phase = runtime.phase;
	if (runtime.attack != kNoAttack) {
		context.attackTime = runtime.attackTime;
	}
	if (const AttackStepData* step = FindStepData(table, runtime.attack, runtime.step)) {
		context.step = runtime.step;
		context.stepName = step->name;
		context.stage = step->stage;
	}
	return context;
}

//=============================================================================
// 状態
//=============================================================================
// ===== 状態を変える（タイマーも最初から）=====
void EnterState(AttackTableRuntime& runtime, AttackTableState state, float duration) {
	runtime.state = state;
	runtime.time = 0.0f;
	runtime.duration = duration;
}

// ===== 選ぶ状態へ（長さはフェーズの Min Idle Seconds）=====
// 待ちの部品の OnBegin はここでは呼ばず、次に時間が進むフレームの UpdateSelecting で呼ぶ（AI が止まっている・時間が 0 のフレームに呼ばないため）
void EnterSelecting(AttackTableRuntime& runtime, const AttackTable& table) { EnterState(runtime, AttackTableState::Selecting, table.phases[runtime.phase].minIdleSeconds); }

//=============================================================================
// 待ちの部品
//=============================================================================
// ===== 今のフェーズの待ちの部品（無ければ nullptr）=====
PartList* GetIdleParts(AttackTableRuntime& runtime) {
	const size_t phase = static_cast<size_t>(runtime.phase);
	return (phase < runtime.idleParts.size()) ? &runtime.idleParts[phase] : nullptr;
}

// ===== 待ちの部品へ、終わったことを知らせる（OnBegin を呼んでいなければ何もしない）=====
void EndIdle(AttackTableRuntime& runtime, const AttackTableComponent& component, const AttackTable& table, AttackPartEnd reason, float deltaTime) {
	if (!runtime.isIdleBegun) {
		return;
	}
	runtime.isIdleBegun = false; // 先に下ろす（OnEnd を2回送らないように）
	if (PartList* idle = GetIdleParts(runtime)) {
		const AttackPartContext context = MakeContext(runtime, component, table, deltaTime);
		ForEachPart(*idle, [&](AttackPart& part) { part.OnEnd(context, reason); });
	}
}

//=============================================================================
// ステップ・攻撃の開始と終了
//=============================================================================
// ===== ステップの行き先を、ステップの番号にする（攻撃を終えるなら kNoStep）=====
int ResolveLink(const AttackTableRuntime& runtime, const AttackData& attack, const AttackStepLink& link, int from) {
	switch (link.jump) {
	case AttackStepJump::Next:
		return (static_cast<size_t>(from + 1) < attack.steps.size()) ? from + 1 : kNoStep;
	case AttackStepJump::End:
		return kNoStep;
	case AttackStepJump::Step: {
		const auto found = std::ranges::find(attack.steps, link.step, &AttackStepData::name);
		if (found == attack.steps.end()) {
			LogManager::Warning(std::format("{}: 攻撃 \"{}\" に行き先のステップ \"{}\" がありません。攻撃を終えます", AttackTableManager::GetPath(runtime.table), attack.name, link.step));
			return kNoStep;
		}
		return static_cast<int>(found - attack.steps.begin());
	}
	}
	return kNoStep;
}

// ===== ステップに入る（通しの部品の OnStep → ステップの部品の OnBegin）=====
void EnterStep(AttackTableRuntime& runtime, const AttackTableComponent& component, const AttackTable& table, int step, float deltaTime) {
	runtime.step = step;
	runtime.time = 0.0f;
	runtime.duration = table.attacks[runtime.attack].steps[step].seconds;
	const AttackPartContext context = MakeContext(runtime, component, table, deltaTime);
	AttackPartSet& set = runtime.attacks[runtime.attack];
	ForEachPartIn(set, kWholeAttack, [&](AttackPart& part) { part.OnStep(context); });
	ForEachPartIn(set, step, [&](AttackPart& part) {
		part.TakeStepRequest(); // 前に使ったときの頼みが残っていたら捨てる
		part.OnBegin(context);
	});
}

// ===== ステップの部品へ、ステップが終わったことを知らせる（ステップに居なければ何もしない）=====
void EndStep(AttackTableRuntime& runtime, const AttackTableComponent& component, const AttackTable& table, AttackPartEnd reason, float deltaTime) {
	if (runtime.step == kNoStep) {
		return;
	}
	const AttackPartContext context = MakeContext(runtime, component, table, deltaTime); // ステップの様子が入っているうちに作る
	const int step = runtime.step;
	runtime.step = kNoStep; // 先に下ろす（OnEnd を2回送らないように）
	ForEachPartIn(runtime.attacks[runtime.attack], step, [&](AttackPart& part) { part.OnEnd(context, reason); });
}

// ===== 攻撃を終える（ステップの部品 → 通しの部品の順に OnEnd。クールタイムは、終わったときから数える。攻撃していなければ何もしない）=====
void EndAttack(AttackTableRuntime& runtime, const AttackTableComponent& component, const AttackTable& table, AttackPartEnd reason, float deltaTime) {
	if (runtime.attack == kNoAttack) {
		return;
	}
	EndStep(runtime, component, table, reason, deltaTime);
	const AttackPartContext context = MakeContext(runtime, component, table, deltaTime); // ステップは終わっているので、step は -1
	const int attack = runtime.attack;
	runtime.attack = kNoAttack; // 先に下ろす（OnEnd を2回送らないように）
	if (static_cast<size_t>(attack) < table.attacks.size()) {
		runtime.cooldowns[attack] = table.attacks[attack].cooldown; // 表を作り直す前の古い番号なら数えない（この後で全部 0 に戻る）
	}
	ForEachPartIn(runtime.attacks[attack], kWholeAttack, [&](AttackPart& part) { part.OnEnd(context, reason); });
}

// ===== ステップへ進む（step が kNoStep なら、攻撃を終えて選ぶ状態へ）=====
void GoToStep(AttackTableRuntime& runtime, const AttackTableComponent& component, const AttackTable& table, int step, float deltaTime) {
	if (step != kNoStep) {
		EnterStep(runtime, component, table, step, deltaTime);
		return;
	}
	EndAttack(runtime, component, table, AttackPartEnd::Finished, deltaTime);
	EnterSelecting(runtime, table);
}

// ===== 攻撃を始める（同じ攻撃が続いた回数も、ここで数える）=====
void BeginAttack(AttackTableRuntime& runtime, const AttackTableComponent& component, const AttackTable& table, int attack, float deltaTime) {
	runtime.repeatCount = (attack == runtime.lastAttack) ? runtime.repeatCount + 1 : 1;
	runtime.lastAttack = attack;
	runtime.attack = attack;
	runtime.step = kNoStep;
	runtime.attackTime = 0.0f;
	EnterState(runtime, AttackTableState::Attacking, 0.0f);
	const AttackPartContext context = MakeContext(runtime, component, table, deltaTime);
	ForEachPartIn(runtime.attacks[attack], kWholeAttack, [&](AttackPart& part) {
		part.TakeStepRequest(); // 前に使ったときの頼みが残っていたら捨てる
		part.OnBegin(context);
	});
	if (runtime.pendingStagger > 0.0f) {
		return; // OnBegin の中で怯まされた（ステップに入らずに、この後すぐ止める）
	}
	GoToStep(runtime, component, table, table.attacks[attack].steps.empty() ? kNoStep : 0, deltaTime);
}

// ===== 今動いている部品（待ちか攻撃）を止める（怯み・フェーズの移行・撃破・表の作り直し・敵が消える）=====
void InterruptCurrent(AttackTableRuntime& runtime, const AttackTableComponent& component, const AttackTable& table, float deltaTime) {
	EndIdle(runtime, component, table, AttackPartEnd::Interrupted, deltaTime);
	EndAttack(runtime, component, table, AttackPartEnd::Interrupted, deltaTime);
}

//=============================================================================
// 怯み
//=============================================================================
// ===== 怯めるか =====
bool CanStagger(AttackTableState state) {
	switch (state) {
	case AttackTableState::Defeated:
	case AttackTableState::PhaseTransition:
		return false; // 撃破とフェーズの移行は、怯みで止めない
	case AttackTableState::Selecting:
	case AttackTableState::Attacking:
	case AttackTableState::Staggered:
		break;
	}
	return true;
}

// ===== 今すぐ怯ませる =====
void StaggerNow(AttackTableRuntime& runtime, const AttackTableComponent& component, const AttackTable& table, float seconds) {
	switch (runtime.state) {
	case AttackTableState::Defeated:
	case AttackTableState::PhaseTransition:
		return;
	case AttackTableState::Staggered:
		runtime.duration = (std::max)(runtime.duration, runtime.time + seconds); // 残りの長い方
		return;
	case AttackTableState::Selecting:
	case AttackTableState::Attacking:
		break;
	}
	{
		const PartCallScope scope(runtime); // OnEnd の中から呼ばれた Stagger は、止め終わってから
		InterruptCurrent(runtime, component, table, 0.0f);
	}
	EnterState(runtime, AttackTableState::Staggered, seconds);
}

// ===== 部品の中から呼ばれた Stagger を効かせる（部品の呼び出しが全部終わった所で呼ぶ）=====
void ApplyPendingStagger(AttackTableRuntime& runtime, const AttackTableComponent& component, const AttackTable& table) {
	// 怯ませる途中の OnEnd で、また Stagger を呼ばれることがあるので、無くなるまで繰り返す（2回目からは怯んでいる状態なので、残りを延ばすだけ）
	while (runtime.pendingStagger > 0.0f) {
		const float seconds = runtime.pendingStagger;
		runtime.pendingStagger = 0.0f;
		StaggerNow(runtime, component, table, seconds);
	}
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
// ===== 部品の数が表と同じか（違えば作り直す）=====
bool IsSameShape(const AttackTableRuntime& runtime, const AttackTable& table) {
	if (runtime.attacks.size() != table.attacks.size() || runtime.idleParts.size() != table.phases.size()) {
		return false;
	}
	for (size_t i = 0; i < table.attacks.size(); ++i) {
		const AttackPartSet& set = runtime.attacks[i];
		const AttackData& attack = table.attacks[i];
		// 部品の数・ステップの数・部品の動く所（再生中にステップの名前を消した など）が同じか
		if (set.parts.size() != attack.parts.size() || set.stepCount != attack.steps.size() || set.stepOf != PlaceParts(attack)) {
			return false;
		}
	}
	for (size_t i = 0; i < table.phases.size(); ++i) {
		if (runtime.idleParts[i].size() != table.phases[i].idleParts.size()) {
			return false;
		}
	}
	return true;
}

// ===== 表に合わせて部品を作り、最初から =====
void Build(AttackTableRuntime& runtime, const AttackTableComponent& component, const AttackTable& table) {
	runtime.table = component.table;
	runtime.revision = AttackTableManager::GetRevision(component.table);
	runtime.attacks.clear();
	for (const AttackData& attack : table.attacks) {
		AttackPartSet set;
		set.parts = CreateParts(attack.parts, std::format("攻撃 \"{}\" ", attack.name));
		set.stepOf = PlaceParts(attack);
		set.stepCount = attack.steps.size();
		for (size_t i = 0; i < attack.parts.size(); ++i) {
			if (set.stepOf[i] == kNowhere) {
				LogManager::Warning(std::format("攻撃 \"{}\" の部品 \"{}\" の動くステップ \"{}\" がありません。この部品は動きません", attack.name, attack.parts[i].name, attack.parts[i].step));
			}
		}
		runtime.attacks.push_back(std::move(set));
	}
	runtime.idleParts.clear();
	for (const AttackPhaseData& phase : table.phases) {
		runtime.idleParts.push_back(CreateParts(phase.idleParts, std::format("フェーズ \"{}\" の待ち", phase.name)));
	}
	ReadAllPartFields(runtime, table);
	runtime.cooldowns.assign(table.attacks.size(), 0.0f);
	runtime.attack = kNoAttack;
	runtime.step = kNoStep;
	runtime.lastAttack = kNoAttack;
	runtime.repeatCount = 0;
	runtime.requestedAttack = kNoAttack;
	runtime.isIdleBegun = false;
	runtime.phase = CalcPhase(table, 0, component.healthRatio); // 始めから HP が減っていれば、移行の状態を飛ばしてそのフェーズから
	EnterSelecting(runtime, table);
}

// ===== その敵の実行中の値（無ければ作る）=====
AttackTableRuntime& FindOrAddRuntime(Handle<Entity> entity, const AttackTableComponent& component, const AttackTable& table) {
	if (AttackTableRuntime* runtime = FindRuntime(entity)) {
		return *runtime;
	}
	AttackTableRuntime& runtime = runtimes.emplace_back();
	runtime.entity = entity;
	Build(runtime, component, table);
	return runtime;
}

// ===== 実行中の値を、今の Component と表に合わせる =====
void SyncWithTable(AttackTableRuntime& runtime, const AttackTableComponent& component, const AttackTable& table) {
	// --- 表が替わった・行・ステップ・部品の数が変わった：今の部品を止めて作り直す ---
	if (runtime.table != component.table || !IsSameShape(runtime, table)) {
		InterruptCurrent(runtime, component, table, 0.0f); // 表はもう新しい物なので、古い番号の所は読まない（Build でクールタイムも全部 0 に戻る）
		Build(runtime, component, table);
		return;
	}
	// --- 値が変わった（再生中に表を直した）：部品の値だけ読み直す。今の攻撃は続ける ---
	const uint32_t revision = AttackTableManager::GetRevision(component.table);
	if (runtime.revision != revision) {
		runtime.revision = revision;
		ReadAllPartFields(runtime, table);
	}
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
	PartList* idle = GetIdleParts(runtime);
	// --- Selecting の最初のフレーム：待ちの部品を始める ---
	if (!runtime.isIdleBegun) {
		runtime.isIdleBegun = true;
		if (idle != nullptr) {
			const AttackPartContext context = MakeContext(runtime, component, table, deltaTime);
			ForEachPart(*idle, [&](AttackPart& part) { part.OnBegin(context); });
		}
	}
	// --- 待ちの部品を進める ---
	runtime.time += deltaTime;
	if (idle != nullptr) {
		const AttackPartContext context = MakeContext(runtime, component, table, deltaTime);
		ForEachPart(*idle, [&](AttackPart& part) {
			part.OnUpdate(context);
			part.TakeStepRequest(); // 待ちにステップは無いので、頼みは捨てる
		});
	}
	if (runtime.pendingStagger > 0.0f) {
		return; // 待ちの部品に怯まされた（攻撃を選ばずに、この後すぐ止める。RequestAttack の分は残す）
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
	EndIdle(runtime, component, table, AttackPartEnd::Finished, deltaTime);
	if (runtime.pendingStagger > 0.0f) {
		return; // 待ちの部品の OnEnd で怯まされた（RequestAttack の分は、次に選ぶときへ残す）
	}
	runtime.requestedAttack = kNoAttack;
	BeginAttack(runtime, component, table, attack, deltaTime);
}

// ===== 攻撃を進める =====
// 部品を全部呼んでから、頼み（FinishStep・FailStep）とステップの長さを見て、次のステップへ
void UpdateAttacking(AttackTableRuntime& runtime, const AttackTableComponent& component, const AttackTable& table, float deltaTime) {
	const AttackData& attack = table.attacks[runtime.attack];
	const AttackStepData& step = attack.steps[runtime.step];
	runtime.time += deltaTime;
	runtime.attackTime += deltaTime;
	runtime.duration = step.seconds; // 再生中に秒を直しても、すぐ効くように
	// --- 通しの部品 → ステップの部品 の順に進める ---
	const AttackPartContext context = MakeContext(runtime, component, table, deltaTime);
	AttackStepRequest request = AttackStepRequest::None;
	const auto update = [&](AttackPart& part) {
		part.OnUpdate(context);
		const AttackStepRequest partRequest = part.TakeStepRequest();
		// 失敗 ＞ 終える ＞ 無し の強い方を残す（1つでも失敗なら、失敗の行き先へ）
		if (partRequest == AttackStepRequest::Fail || (partRequest == AttackStepRequest::Finish && request == AttackStepRequest::None)) {
			request = partRequest;
		}
	};
	AttackPartSet& set = runtime.attacks[runtime.attack];
	ForEachPartIn(set, kWholeAttack, update);
	ForEachPartIn(set, runtime.step, update);
	if (runtime.pendingStagger > 0.0f) {
		return; // 部品に怯まされた（ステップを進めずに、この後すぐ止める）
	}
	if (request == AttackStepRequest::None && runtime.time < runtime.duration) {
		return; // まだこのステップ
	}
	// --- ステップが終わった：行き先を決めてから、ステップの部品を終える ---
	const AttackStepLink& link = (request == AttackStepRequest::Fail) ? step.fail : step.next;
	const int next = ResolveLink(runtime, attack, link, runtime.step);
	EndStep(runtime, component, table, AttackPartEnd::Finished, deltaTime);
	if (runtime.pendingStagger > 0.0f) {
		return; // ステップの部品の OnEnd で怯まされた（次のステップに入らずに、この後すぐ止める）
	}
	GoToStep(runtime, component, table, next, deltaTime);
}

// ===== 怯み・フェーズの移行：時間が来たら選ぶ状態へ =====
void UpdateTimedState(AttackTableRuntime& runtime, const AttackTable& table, float deltaTime) {
	runtime.time += deltaTime;
	if (runtime.time >= runtime.duration) {
		EnterSelecting(runtime, table);
	}
}

// ===== 敵1体（部品を呼ぶ所。呼び終わってから、部品の中で呼ばれた Stagger を効かせる）=====
void UpdateRuntime(AttackTableRuntime& runtime, const AttackTableComponent& component, const AttackTable& table, float deltaTime) {
	SyncWithTable(runtime, component, table);
	// 止まっている間は何も進めない（クールタイムも、攻撃部品の呼び出しも）。時間が 0 なのに部品を呼ぶと、止まった時間の中で攻撃が動いてしまう
	if (!component.enabled || deltaTime <= 0.0f || !EntityManager::IsActiveInHierarchy(runtime.entity)) {
		return;
	}
	for (float& cooldown : runtime.cooldowns) {
		cooldown = (std::max)(cooldown - deltaTime, 0.0f);
	}
	if (UpdateHealth(runtime, component, table, deltaTime)) {
		return;
	}

	// --- 状態ごとに行動 ---
	switch (runtime.state) {
	case AttackTableState::Selecting:
		UpdateSelecting(runtime, component, table, deltaTime);
		break;
	case AttackTableState::Attacking:
		UpdateAttacking(runtime, component, table, deltaTime);
		break;
	case AttackTableState::Staggered:
	case AttackTableState::PhaseTransition:
		UpdateTimedState(runtime, table, deltaTime);
		break;
	case AttackTableState::Defeated:
		break;
	}
}

void UpdateEntity(Handle<Entity> entity, const AttackTableComponent& component, float deltaTime) {
	const AttackTable* table = AttackTableManager::Get(component.table);
	if (table == nullptr) {
		return; // 表が選ばれていない
	}
	AttackTableRuntime& runtime = FindOrAddRuntime(entity, component, *table);
	{
		const PartCallScope scope(runtime);
		UpdateRuntime(runtime, component, *table, deltaTime);
	}
	ApplyPendingStagger(runtime, component, *table);
}


//=============================================================================
// Inspector・保存
//=============================================================================
void DescribeAttackTableComponent(ComponentUI& ui, AttackTableComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すとAIが止まる（タイマーも止まる）"));
	ui.AssetField("Table###table", value.table, AssetType::AttackTable, Tip("攻撃表（.attacktable）。Projectウィンドウの Create → Attack Table で作る"));
	ui.Field("Target###target", value.target, Tip("狙う相手。距離の条件に使う"));
	ui.Field("Health Ratio###healthRatio", value.healthRatio, Range(0.0f, 1.0f).Tip(
		"残りのHPの割合（0〜1）。同じEntityにHealthがあれば、再生中は毎フレームそこから書かれる。無ければゲームが書く。再生中につまむと、フェーズの移行と撃破を試せる（Health "
	            "があるときは Health の値をつまむ）"));
}
}


//=============================================================================
// 初期化・解放
//=============================================================================
// ===== 初期化 =====
void AttackTableSystem::Initialize() {
	// AttackTableの置き場所・保存・Inspectorを登録する
	EntityManager::RegisterComponent<AttackTableComponent>();
	ComponentSerializer::Register<AttackTableComponent>("Attack Table", &DescribeAttackTableComponent);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<AttackTableComponent>>("Attack Table", "Gameplay", &DescribeAttackTableComponent, 
		"攻撃表のAI。攻撃表のファイル（.attacktable）に書いた攻撃から、条件に合う物を選んで順に出す（ボス・敵）"));
	// HitboxWindow・PlayAnimationの攻撃部品。ゲームの部品
	BuiltInAttackParts::Register();
	// 敵が消えるときに攻撃の途中ならOnEndを呼べるようにする
	EntityManager::AddDestroyListener(&AttackTableSystem::OnEntityDestroyed);
}

// ===== 解放 =====
void AttackTableSystem::Release() { runtimes.clear(); }


//=============================================================================
// 毎フレーム
//=============================================================================
void AttackTableSystem::Update(bool isGameRunning, float deltaTime) {
	if (!isGameRunning) {
		return;
	}
	// --- Componentを外された敵を片づける ---
	std::erase_if(runtimes, [](const AttackTableRuntime& runtime) { return EntityManager::Get<AttackTableComponent>(runtime.entity) == nullptr; });
	EntityManager::ForEach<AttackTableComponent>([deltaTime](Handle<Entity> entity, AttackTableComponent& component) {
		// 同じEntityにHealthがあれば、HPの割合はそこから読む
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
	if (runtime == nullptr || table == nullptr || seconds <= 0.0f || !CanStagger(runtime->state)) {
		return false;
	}
	// --- その敵の部品の中から呼ばれた：呼び出しが全部終わってから怯ませる ---
	if (runtime->partCallDepth > 0) {
		runtime->pendingStagger = (std::max)(runtime->pendingStagger, seconds);
		return true;
	}
	StaggerNow(*runtime, *component, *table, seconds);
	ApplyPendingStagger(*runtime, *component, *table); // 止めるときの OnEnd で呼ばれた分
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
	status.phase = runtime->phase;
	status.phaseName = table->phases[runtime->phase].name;
	status.attackName = attackName(runtime->attack);
	status.lastAttackName = attackName(runtime->lastAttack);
	if (const AttackStepData* step = FindStepData(*table, runtime->attack, runtime->step)) {
		status.step = runtime->step;
		status.stepName = step->name;
		status.stage = step->stage;
	}
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
	const AttackTable* table = AttackTableManager::Get(runtime->table);
	if (component != nullptr && table != nullptr) {
		const PartCallScope scope(*runtime);                  // 消える敵なので、OnEnd の中で呼ばれた Stagger は捨てる
		InterruptCurrent(*runtime, *component, *table, 0.0f); // Component はまだ読める。部品が出した物を片付けられるように
	}
	std::erase_if(runtimes, [entity](const AttackTableRuntime& other) { return other.entity == entity; });
}