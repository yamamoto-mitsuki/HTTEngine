#include "ParticleSystem.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Editor/Viewport/SceneVisibility.h"
#include "HTTEngine/Graphics/Renderer/Renderer.h"
#include "HTTEngine/Math/Color.h"
#include "HTTEngine/Math/RandomEngine.h"
#include "HTTEngine/Time/EngineTime.h"
#ifdef USE_IMGUI
#include "HTTEngine/Editor/Windows/HierarchyWindow.h"
#include "HTTEngine/Math/MathUtility.h"
#endif

namespace {
constexpr float kMinDuration = 0.01f;          // Duration の下限（秒。0 だとくり返しが止まらないので）
constexpr float kMinLifetime = 0.01f;          // 寿命の下限（秒。0 で割らないように）
constexpr float kMinDirectionLengthSq = 1e-4f; // これより短い向きは、向きが決まらないとみなす

/// <summary>
/// 生きている粒1つ（実行中だけ。保存しない）
/// </summary>
struct LiveParticle {
	Vector3 position;       // 位置（World ならワールド座標、Local なら Entity から見た座標）
	Vector3 velocity;       // 速さ（m/s。position と同じ座標で）
	Vector3 startPosition;  // 出た所（Converge のとき、ここから center へ動かす）
	Vector3 center;         // 集まる先（Converge。World なら出た瞬間の Entity の位置、Local なら Entity から見た原点）
	Vector4 startColor;     // 初めの色（sRGB）
	float startSize = 0.0f; // 初めの大きさ（m）
	float age = 0.0f;       // 出てからの秒
	float lifetime = 1.0f;  // 消えるまでの秒
	float rotation = 0.0f;  // 今の角度（ラジアン。画面の上で、正で左回り）
	float angularVelocity = 0.0f; // 回る速さ（ラジアン/秒）
};

/// <summary>
/// Particle System 1つ分の、実行中だけの状態（保存しない。Play / Stop で作り直すと消える）
/// </summary>
struct EmitterState {
	Handle<Entity> entity;               // 持ち主
	std::vector<LiveParticle> particles; // 生きている粒（古い方 → 新しい方。Normal で重ねるとき、新しい粒が手前に描かれる）
	bool isStartChecked = false;         // Play On Start を見たか（最初に見つけたときに1回だけ。先に Play・Stop を呼ばれたら見ない）
	bool hasPlayed = false;              // 一度でも出し始めたか（Play On Start・Play。Stop Action は、出し始めた物が出し終わったときだけ）
	bool isStopActionDone = false;       // Stop Action をもうしたか（1回の出し終わりに1回だけ。もう一度 Play したら戻す）
	bool isPlaying = false;              // 出している途中か
	bool isBurstPending = false;         // 次に出すとき、Burst をまとめて出す（Play したとき・くり返しの頭）
	float time = 0.0f;                   // 今の1回（Duration）の中の秒
	float rateCarry = 0.0f;              // Rate Over Time の端数（整数にならなかった分を、次のフレームへ持ち越す）
};

/// <summary>
/// ParticleSystem が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct ParticleSystemState {
	std::vector<EmitterState> emitters;  // 見つけた Particle System の一覧（数しか無いので、順に見れば足りる）
	std::vector<ParticleData> instances; // 描くときの粒の置き場（Draw の中だけで使う。配列のメモリを使い回す）
	Handle<Entity> previewEntity;        // 停止中にも流す1つ（Inspector の Preview。無効なら無し）
	bool wasGameRunning = false;         // ゲームが動いているとして粒を持っているか（Play / Stop の切り替わりで、粒を全部捨てる。SwitchGameRunning）
};
std::unique_ptr<ParticleSystemState> state;

// ゲームが動き出した・止まったら、粒を全部捨てる（Preview の粒をゲームに持ち込まない・ゲームの粒を停止中に残さない。Play On Start も、また最初から見る）
// 切り替わったときだけ捨てる（同じ値で何回呼んでもよい）
void SwitchGameRunning(bool isGameRunning) {
	if (isGameRunning == state->wasGameRunning) {
		return;
	}
	state->emitters.clear();
	state->previewEntity = {};
	state->wasGameRunning = isGameRunning;
}

// Entityの状態を探す（無ければ nullptr）
EmitterState* FindEmitter(Handle<Entity> entity) {
	const auto it = std::ranges::find(state->emitters, entity, &EmitterState::entity);
	return (it != state->emitters.end()) ? &*it : nullptr;
}

// Entityの状態を探し、無ければ足す
EmitterState& FindOrAddEmitter(Handle<Entity> entity) {
	if (EmitterState* emitter = FindEmitter(entity)) {
		return *emitter;
	}
	state->emitters.push_back({});
	state->emitters.back().entity = entity;
	return state->emitters.back();
}

// Entity のワールド行列（Transform が無ければ単位行列）
Matrix4x4 WorldMatrixOf(Handle<Entity> entity) {
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	return (transform != nullptr) ? transform->worldMatrix : MakeIdentity4x4();
}

// 最初から出し始める（Burst は次に出すときにまとめて）
void Restart(EmitterState& emitter) {
	emitter.isStartChecked = true;
	emitter.hasPlayed = true;
	emitter.isStopActionDone = false; // 出し終わった後にもう一度 Play したら、また出し終わりを待つ（None で残していた物）
	emitter.isPlaying = true;
	emitter.isBurstPending = true;
	emitter.time = 0.0f;
	emitter.rateCarry = 0.0f;
}

//=============================================================================
// 乱数
//=============================================================================
// Min〜Max の乱数（x と y のどちらが大きくてもよい。同じなら、その値）
float RandomRange(const Vector2& range) {
	const float low = (std::min)(range.x, range.y);
	const float high = (std::max)(range.x, range.y);
	return (low < high) ? RandomEngine::GetFloat(low, high) : low;
}

// 長さ1のランダムな向き（球の中の点を長さ1に伸ばす。真ん中に近すぎて向きが決まらない点は選び直す）
Vector3 RandomDirection() {
	while (true) {
		const Vector3 point = RandomEngine::GetInsideUnitSphere();
		const float lengthSq = LengthSq(point);
		if (lengthSq > kMinDirectionLengthSq) {
			return point * (1.0f / std::sqrt(lengthSq));
		}
	}
}

//=============================================================================
// 出る場所と向き（Shape）
//=============================================================================
/// <summary>
/// 粒が出る場所と、飛ぶ向き（どちらも Entity から見た座標）
/// </summary>
struct SpawnPoint {
	Vector3 position;  // 出る場所
	Vector3 direction; // 飛ぶ向き（長さ1）
};

// 形の中（From Surface なら表面）から、出る場所と向きを1つ選ぶ
// 中に均等に散らすため、真ん中からの距離は「0〜1 の乱数」をそのまま使わず、球なら3乗根・円なら平方根にする
// （球の中の体積は半径の3乗、円の面積は半径の2乗に比例するので、そのままだと真ん中に集まって見える）
SpawnPoint SampleShape(const ParticleSystemComponent& component) {
	switch (component.shape) {
	case ParticleShape::Point:
		return {
		    {0.0f, 0.0f, 0.0f},
            RandomDirection()
        };

	case ParticleShape::Sphere: {
		const Vector3 direction = RandomDirection();
		const float distance = component.fromSurface ? 1.0f : std::cbrt(RandomEngine::GetFloat(0.0f, 1.0f));
		return {direction * (component.radius * distance), direction};
	}

	case ParticleShape::Circle: {
		// XY の面（横から見るゲームで、画面と同じ向きの円）
		const float angle = RandomEngine::GetFloat(0.0f, MathUtility::kTwoPi);
		const Vector3 direction = {std::cos(angle), std::sin(angle), 0.0f};
		const float distance = component.fromSurface ? 1.0f : std::sqrt(RandomEngine::GetFloat(0.0f, 1.0f));
		return {direction * (component.radius * distance), direction};
	}

	case ParticleShape::Cone: {
		// XZ の面の円から出て、上（+Y）へ。縁から出た粒ほど外へ傾ける（縁で Cone Angle。真ん中はまっすぐ上）
		// → 粒どうしが交差せず、ラッパの形に広がる。Radius 0 でも、傾きは乱数で決まるので広がる
		const float angle = RandomEngine::GetFloat(0.0f, MathUtility::kTwoPi);
		const Vector3 outward = {std::cos(angle), 0.0f, std::sin(angle)};
		const float distance = component.fromSurface ? 1.0f : std::sqrt(RandomEngine::GetFloat(0.0f, 1.0f));
		const float tilt = component.coneAngle * MathUtility::kDegToRad * distance;
		const Vector3 direction = outward * std::sin(tilt) + Vector3{0.0f, std::cos(tilt), 0.0f};
		return {outward * (component.radius * distance), direction};
	}

	case ParticleShape::Box: {
		const Vector3 half = {std::abs(component.boxSize.x) * 0.5f, std::abs(component.boxSize.y) * 0.5f, std::abs(component.boxSize.z) * 0.5f};
		Vector3 position = RandomEngine::GetVector3({-half.x, -half.y, -half.z}, half);
		if (component.fromSurface) {
			// 面を1つ選んで（広い面ほど選ばれやすく）、その軸だけ端へ寄せる
			const float areaX = half.y * half.z; // 左右の面の広さ（の 1/4。比べるだけなので、そろっていればよい）
			const float areaY = half.x * half.z; // 上下の面
			const float areaZ = half.x * half.y; // 前後の面
			const float pick = RandomEngine::GetFloat(0.0f, areaX + areaY + areaZ);
			const float side = (RandomEngine::GetInt(0, 1) == 0) ? -1.0f : 1.0f;
			if (pick < areaX) {
				position.x = half.x * side;
			} else if (pick < areaX + areaY) {
				position.y = half.y * side;
			} else {
				position.z = half.z * side;
			}
		}
		// 向きは箱の真ん中から外へ（ちょうど真ん中に出た粒は、どの向きでも）
		const Vector3 direction = (LengthSq(position) > kMinDirectionLengthSq) ? Normalize(position) : RandomDirection();
		return {position, direction};
	}
	}
	return {
	    {0.0f, 0.0f, 0.0f},
        RandomDirection()
    };
}

//=============================================================================
// 出す・動かす
//=============================================================================
// ===== 粒を count 個出す（Max Particles を超える分は出さない）=====
void EmitParticles(EmitterState& emitter, const ParticleSystemComponent& component, const Matrix4x4& worldMatrix, int count) {
	const int limit = std::clamp(component.maxParticles, 1, ParticleSystem::kMaxParticles);
	const int room = limit - static_cast<int>(emitter.particles.size());
	const int spawnCount = (std::min)(count, room);
	for (int i = 0; i < spawnCount; ++i) {
		SpawnPoint point = SampleShape(component);
		// World：出た瞬間のワールド座標にする（この後 Entity が動いても付いていかない）
		if (component.space == ParticleSpace::World) {
			point.position = TransformPoint(point.position, worldMatrix);
			point.direction = Normalize(TransformNormal(point.direction, worldMatrix)); // 回転だけ効かせる（Scale で速さは変えない）
		}
		LiveParticle particle;
		particle.position = point.position;
		particle.startPosition = point.position;
		particle.center = (component.space == ParticleSpace::World) ? TransformPoint({0.0f, 0.0f, 0.0f}, worldMatrix) : Vector3{0.0f, 0.0f, 0.0f};
		particle.velocity = point.direction * RandomRange(component.speed);
		particle.startColor = Lerp(component.colorA, component.colorB, RandomEngine::GetFloat(0.0f, 1.0f));
		particle.startSize = RandomRange(component.size);
		particle.lifetime = (std::max)(RandomRange(component.lifetime), kMinLifetime);
		// 回転
		const float spinSign = (component.randomSpinDirection && RandomEngine::GetInt(0, 1) == 0) ? -1.0f : 1.0f;
		particle.rotation = RandomRange(component.startRotation) * MathUtility::kDegToRad * spinSign;
		particle.angularVelocity = RandomRange(component.angularVelocity) * MathUtility::kDegToRad * spinSign;
		emitter.particles.push_back(particle);
	}
}

// ===== 1フレーム進める（粒を動かして寿命の物を消し、再生中なら出す）=====
void UpdateEmitter(EmitterState& emitter, const ParticleSystemComponent& component, const Matrix4x4& worldMatrix, float deltaTime) {
	// ===== 1. 動かす =====
	// drag：1秒あたり drag の割合で遅くする（1フレームで 1 - drag×dt 倍。負にならないように 0 で止める）
	const float dragScale = (std::max)(0.0f, 1.0f - component.drag * deltaTime);
	for (LiveParticle& particle : emitter.particles) {
		particle.age += deltaTime;
		particle.rotation += particle.angularVelocity * deltaTime; // 回す（Converge でも回る。Drag では遅くならない）
		// --- Converge：速さは使わず、年の割合で「出た所 → 中心」の間に置く（寿命の終わりにちょうど中心。突き抜けない）---
		if (component.converge) {
			const float t = std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f);
			particle.position = Lerp(particle.startPosition, particle.center, Ease::Apply(t, component.convergeEase));
			continue;
		}
		particle.velocity += component.gravity * deltaTime;
		particle.velocity *= dragScale;
		particle.position += particle.velocity * deltaTime;
	}
	// ===== 2. 寿命の粒を消す（並びはそのまま。Normal で重ねたとき、前後が入れ替わってちらつかないように）=====
	std::erase_if(emitter.particles, [](const LiveParticle& particle) { return particle.age >= particle.lifetime; });

	// ===== 3. 出す =====
	if (!emitter.isPlaying) {
		return;
	}
	// --- Burst（1回の始めにまとめて）---
	if (emitter.isBurstPending) {
		emitter.isBurstPending = false;
		EmitParticles(emitter, component, worldMatrix, component.burstCount);
	}
	// --- Rate Over Time（端数を持ち越して、整数の分だけ。フレームの速さで出る数が変わらない）---
	emitter.rateCarry += (std::max)(0.0f, component.rateOverTime) * deltaTime;
	const float whole = std::floor(emitter.rateCarry);
	emitter.rateCarry -= whole;
	EmitParticles(emitter, component, worldMatrix, static_cast<int>((std::min)(whole, static_cast<float>(ParticleSystem::kMaxParticles))));

	// ===== 4. 1回の終わり =====
	emitter.time += deltaTime;
	const float duration = (std::max)(component.duration, kMinDuration);
	if (emitter.time >= duration) {
		if (component.looping) {
			emitter.time = std::fmod(emitter.time, duration); // 次の1回へ（Burst もまた出る）
			emitter.isBurstPending = true;
		} else {
			emitter.isPlaying = false; // 出すのをやめる（出ている粒は寿命まで残る）
		}
	}
}

// 描かない・動かさない物か（無効・親まで含めて無効）
bool IsSleeping(Handle<Entity> entity, const ParticleSystemComponent& component) { return !component.enabled || !EntityManager::IsActiveInHierarchy(entity); }

// 出し終わったか（出し始めた物が、出すのをやめて＝Duration が過ぎた・Stop を呼んだ、粒が全部消えた）
bool IsFinished(const EmitterState& emitter) { return emitter.hasPlayed && !emitter.isPlaying && emitter.particles.empty(); }

// 出し終わったら、Stop Action を1回だけする（ゲームが動いている間だけ呼ぶ。停止中の Preview では Entity を消さない）
void ApplyStopAction(EmitterState& emitter, const ParticleSystemComponent& component) {
	if (emitter.isStopActionDone || !IsFinished(emitter)) {
		return;
	}
	emitter.isStopActionDone = true;
	switch (component.stopAction) {
	// --- Entity を消す（ParticleSystem::Update のすぐ後の EntityManager::FlushDestroy で、このフレームのうちに消える。子も一緒。粒はもう無いので、見た目は変わらない）---
	case ParticleStopAction::Destroy:
		EntityManager::Destroy(emitter.entity);
		break;
	// --- 何もしない ---
	case ParticleStopAction::None:
	default:
		break;
	}
}

//=============================================================================
// 停止中の Preview
//=============================================================================
// Preview の1つだけを、止めない時間で動かす（ほかの粒は持たない）
void UpdatePreview() {
	Handle<Entity> preview = state->previewEntity;
#ifdef USE_IMGUI
	// 選んでいる物を変えたら止める（Inspector から Preview が見えなくなるので）
	if (HierarchyWindow::GetSelected() != preview) {
		preview = {};
	}
#endif
	std::erase_if(state->emitters, [preview](const EmitterState& emitter) { return emitter.entity != preview; });
	const ParticleSystemComponent* component = EntityManager::Get<ParticleSystemComponent>(preview);
	EmitterState* emitter = FindEmitter(preview);
	if (component == nullptr || emitter == nullptr) {
		ParticleSystem::StopPreview();
		return;
	}
	if (!IsSleeping(preview, *component)) {
		UpdateEmitter(*emitter, *component, WorldMatrixOf(preview), Time::GetUnscaleDeltaTime());
	}
	// 1回で終わる物は、出し終わって粒が全部消えたら Preview も終わる（ボタンが ▶ Preview に戻る）
	if (IsFinished(*emitter)) {
		ParticleSystem::StopPreview();
	}
}

// Inspector が描いている Component から、持ち主の Entity を探す（無ければ無効な Handle）
// Inspector は Component の置き場所そのものを渡してくるので、アドレスで比べる
Handle<Entity> FindOwner(const ParticleSystemComponent& component) {
	Handle<Entity> owner;
	EntityManager::ForEach<ParticleSystemComponent>([&component, &owner](Handle<Entity> entity, const ParticleSystemComponent& value) {
		if (&value == &component) {
			owner = entity;
		}
	});
	return owner;
}

//=============================================================================
// Inspector と 保存
//=============================================================================
// Particle System の見せ方。Inspector とシーンファイルの両方がこの並びを使う（Trail と同じ書き方）
// 保存されるのは Field に書いた物だけなので、Shape で使わない項目も隠さずに全部出す
void DescribeParticleSystem(ComponentUI& ui, ParticleSystemComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと出さない・動かさない・描かない（粒は残る）"));
	// 試しに流す（停止中：選んでいる間だけ流す／再生中：最初からもう一度。保存・読み込みのときは Button が false を返すので何も起きない）
	if (ParticleSystem::IsPreviewing(value)) {
		if (ui.Button("■ Stop Preview")) {
			ParticleSystem::StopPreview();
		}
	} else if (ui.Button("▶ Preview")) {
		ParticleSystem::StartPreview(value);
	}

	ui.Separator("Playback");
	ui.Field("Play On Start###playOnStart", value.playOnStart, Tip("Play で流し始める。外すと ParticleSystem::Play を呼ぶまで出さない（使ったら飛び散る物）"));
	ui.Field("Looping###looping", value.looping, Tip("Duration 秒ごとにくり返す（Burst もまた出る）。外すと1回で出すのをやめる"));
	ui.Field("Duration###duration", value.duration, AtLeast(kMinDuration).Tip("1回の長さ（秒）"));
	ui.Field("Stop Action###stopAction", value.stopAction, Tip("出し終わったら（出すのをやめて＝Looping でなく Duration が過ぎた・Stop を呼んだ、粒が全部消えたら）すること。Destroy：この Entity を消す（子も一緒。飛び散る物の Prefab に）"));

	ui.Separator("Emission");
	ui.Field("Rate Over Time###rateOverTime", value.rateOverTime, AtLeast(0.0f).Tip("1秒に出す数。0 で Burst だけ"));
	ui.Field("Burst Count###burstCount", value.burstCount, AtLeast(0.0f).Tip("1回の始めに、まとめて出す数"));
	ui.Field("Max Particles###maxParticles", value.maxParticles, Range(1.0f, static_cast<float>(ParticleSystem::kMaxParticles)).Tip("同時に出ている数の上限。いっぱいの間は出さない"));

	ui.Separator("Shape");
	ui.Field("Shape###shape", value.shape, Tip("Point：点から全部の向き／Sphere：球から外へ／Circle：XY の円から外へ／Cone：上（+Y）へ開く／Box：箱から外へ"));
	ui.Field("Radius###radius", value.radius, AtLeast(0.0f).Tip("Sphere・Circle・Cone の半径（m）"));
	ui.Field("Cone Angle###coneAngle", value.coneAngle, Range(0.0f, 90.0f).Tip("Cone の開き（度）。縁から出た粒ほど外へ傾く"));
	ui.Field("Box Size###boxSize", value.boxSize, AtLeast(0.0f).Tip("Box の大きさ（m）"));
	ui.Field("From Surface###fromSurface", value.fromSurface, Tip("形の表面（縁）から出す。Circle なら輪から飛び散る"));

	ui.Separator("Start (Min, Max)");
	ui.Field("Lifetime###lifetime", value.lifetime, AtLeast(kMinLifetime).Tip("消えるまでの秒。左と右の間でランダム"));
	ui.Field("Speed###speed", value.speed, Tip("Shape の向きへの速さ（m/s）。負なら内へ（吸い込む）"));
	ui.Field("Size###size", value.size, AtLeast(0.0f).Tip("大きさ（m。板の1辺）"));
	ui.ColorField("Color A###colorA", value.colorA);
	ui.ColorField("Color B###colorB", value.colorB);
	ui.Label("初めの色は Color A と B の間でランダム（同じにすれば1色）");
	ui.Field("Intensity###intensity", value.intensity, AtLeast(0.0f).Tip("明るさの倍率（色はそのまま。全部の粒に掛ける）。1 より大きくすると HDR の明るさになり、ブルーム（Threshold 1）で光る。Add の粒は重なった所が足されて明るくなる"));

	ui.Separator("Over Lifetime");
	ui.Field("Gravity###gravity", value.gravity, Tip("加速（m/s²）。落とすなら Y を負、昇らせるなら正。Local なら Entity の向きで"));
	ui.Field("Drag###drag", value.drag, AtLeast(0.0f).Tip("1秒あたりの減速の割合。大きいほどすぐ止まる（火花）"));
	ui.Field("End Size Rate###endSizeRate", value.endSizeRate, AtLeast(0.0f).Tip("最後の大きさ（初めの大きさに掛ける）。0 で縮んで消える"));
	ui.ColorField("End Color###endColor", value.endColor);
	ui.Label("最後の色は初めの色に掛ける（A 0 でフェードアウト）");
	ui.Field("Ease###ease", value.ease, Tip("大きさと色の変わり方（OutBack などは大きさが少し行き過ぎる）"));
	ui.Field("Converge###converge", value.converge, Tip("中心（Entity の位置）へ集まる。寿命の終わりにちょうど着く（Speed・Gravity・Drag は使わない）。Shape の From Surface と合わせると、輪・球の表面から吸い込まれる"));
	ui.Field("Converge Ease###convergeEase", value.convergeEase, Tip("集まり方。InQuad：だんだん速く吸い込まれる／OutQuad：初め速く、中心でゆっくり止まる"));

	ui.Separator("Rotation");
	ui.Field("Start Rotation###startRotation", value.startRotation, Tip("初めの角度（度）。左と右の間でランダム。画面の上で、正で左回り（-180, 180 で全部の向き）"));
	ui.Field("Angular Velocity###angularVelocity", value.angularVelocity, Tip("回る速さ（度/秒）。左と右の間でランダム。正で左回り、負で右回り"));
	ui.Field("Random Spin Direction###randomSpinDirection", value.randomSpinDirection, Tip("半分の粒を逆にする（初めの角度も回る向きも）。葉が全部同じ向きに回らないように"));
	ui.Field("Align To Velocity###alignToVelocity", value.alignToVelocity, Tip("絵の右（+X）を、画面の上で進む向きに向ける（Start Rotation はそこから足す）。Converge なら中心の方へ"));
	ui.Field("Stretch###stretch", value.stretch, AtLeast(0.0f).Tip("絵の横（X）だけに掛ける長さの倍率。Align To Velocity と合わせると、進む向きに細長い線（風の筋・火花）"));

	ui.Separator("Renderer");
	ui.AssetField("Texture###texture", value.textureHandle, AssetType::Texture, Tip("粒の絵（白で作って、色は Color で付ける）。無ければ丸いぼかし"));
	ui.Field("Blend###blend", value.blend, Tip("Add：光る粒（重なるほど明るい）／Normal：普通（奥から順には並べないので、重なると縁が汚いことがある）"));
	ui.Field("Space###space", value.space, Tip("World：出た所に残る／Local：Entity と一緒に動く・回る"));
	ui.Field("Use Fog###useFog", value.useFog, Tip("霧（Fog）を混ぜる。外すと霧の中でもそのままの色（光る粒をくっきり）"));
	ui.Field(
	    "Use Depth Of Field###useDepthOfField", value.useDepthOfField, Tip("被写界深度でぼかす（奥の層の葉・湯気など）。ぼけ方は粒の後ろにある物の距離で決まるので、ピントの合う所の粒は外しておく"));
}
} // namespace

//=============================================================================
// 初期化 / 解放
//=============================================================================
void ParticleSystem::Initialize() {
	HTT_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<ParticleSystemState>();

	// Particle System の置き場所・保存・Inspector を登録する。見せ方は DescribeParticleSystem 1つで、保存と Inspector の両方に使う
	EntityManager::RegisterComponent<ParticleSystemComponent>();
	ComponentSerializer::Register<ParticleSystemComponent>("Particle System", &DescribeParticleSystem);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<ParticleSystemComponent>>("Particle System", "Effects", &DescribeParticleSystem));
}

void ParticleSystem::Release() { state.reset(); }

//=============================================================================
// 毎フレーム
//=============================================================================
void ParticleSystem::Update(bool isGameRunning, float deltaTime) {
	HTT_ASSERT_MSG(state != nullptr, "ParticleSystem::Initialize()を先に呼んでください");
	std::vector<EmitterState>& emitters = state->emitters;

	// ===== Play / Stop が切り替わったら、全部捨てる =====
	// ゲームが動き出した最初のフレームは、ここより先に Script が Play・Emit を呼んでいて、そこで切り替え済み（その分は捨てない）
	SwitchGameRunning(isGameRunning);

	// ===== 停止中は、Preview の1つだけ =====
	if (!isGameRunning) {
		UpdatePreview();
		return;
	}

	// ===== 1. 消えた・外した Particle System の粒を捨てる =====
	// RequestAdd したばかり（次のフレームに付く）の物は残す（同じフレームに Play・Emit した分を捨てないように）
	std::erase_if(emitters, [](const EmitterState& emitter) {
		if (!EntityManager::IsAlive(emitter.entity)) {
			return true;
		}
		return EntityManager::Get<ParticleSystemComponent>(emitter.entity) == nullptr && !EntityManager::IsAddPending<ParticleSystemComponent>(emitter.entity);
	});

	// ===== 2. 出す・動かす =====
	EntityManager::ForEach<ParticleSystemComponent>([deltaTime](Handle<Entity> entity, const ParticleSystemComponent& component) {
		if (IsSleeping(entity, component)) {
			return; // 止めておく（粒は残す。有効に戻したら続きから）
		}
		EmitterState& emitter = FindOrAddEmitter(entity);
		// 初めて見つけたら、Play On Start を見る（先にゲームが Play・Stop を呼んでいたら、そちらに従う）
		if (!emitter.isStartChecked) {
			emitter.isStartChecked = true;
			if (component.playOnStart) {
				Restart(emitter);
			}
		}
		UpdateEmitter(emitter, component, WorldMatrixOf(entity), deltaTime);
		// 出し終わったら Stop Action（Destroy なら、この Entity を消す予約）
		ApplyStopAction(emitter, component);
	});
}

//=============================================================================
// 描画
//=============================================================================
void ParticleSystem::Draw() {
	HTT_ASSERT_MSG(state != nullptr, "ParticleSystem::Initialize()を先に呼んでください");
	std::vector<ParticleData>& instances = state->instances;
	for (const EmitterState& emitter : state->emitters) {
		const ParticleSystemComponent* component = EntityManager::Get<ParticleSystemComponent>(emitter.entity);
		if (emitter.particles.empty() || component == nullptr || IsSleeping(emitter.entity, *component)) {
			continue;
		}

		// --- 粒ごとの行列と色を並べる（今の年の割合 t で、初めの値から最後の値へ）---
		const Matrix4x4 worldMatrix = WorldMatrixOf(emitter.entity);
		const bool isLocal = component->space == ParticleSpace::Local;
		const Vector4 white = {1.0f, 1.0f, 1.0f, 1.0f};
		const float stretch = (std::max)(component->stretch, 0.0f);
		instances.clear();
		for (const LiveParticle& particle : emitter.particles) {
			const float t = std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f);
			const float eased = Ease::Apply(t, component->ease); // OutBack などは 1 を少し越える（大きさの「ポン」）
			const float size = particle.startSize * MathUtility::Lerp(1.0f, component->endSizeRate, eased);
			// 色は 0〜1 に収める（行き過ぎると A が負になり、Add で暗くなるので）
			const Vector4 color = MultiplyColor(particle.startColor, Lerp(white, component->endColor, std::clamp(eased, 0.0f, 1.0f)));
			const Vector3 position = isLocal ? TransformPoint(particle.position, worldMatrix) : particle.position;
			// --- 進む向き（Align To Velocity のときだけ。ワールドの向きで渡し、VS が画面に写して角度にする）---
			// 画面に写すのは VS（Scene ビューと Game ビューでカメラが違うので、ここでは角度にできない）
			Vector3 direction = {0.0f, 0.0f, 0.0f};
			if (component->alignToVelocity) {
				// Converge は速さを使わないので、出た所から中心への向き
				direction = component->converge ? (particle.center - particle.startPosition) : particle.velocity;
				if (isLocal) {
					direction = TransformNormal(direction, worldMatrix); // Entity から見た向き → ワールドの向き
				}
			}
			// VS は world から中心（4行目）と大きさ（1行目の長さ＝横・2行目の長さ＝縦）だけを読み、カメラの方へ向けて四角を組む
			Matrix4x4 world = MakeScaleMatrix({size * stretch, size, size});
			world.m[3][0] = position.x;
			world.m[3][1] = position.y;
			world.m[3][2] = position.z;
			instances.push_back({world, color, direction, particle.rotation});
		}

		// --- 1回のインスタンス描画で描く ---
		Renderer::ParticleInstancesConfig config;
		config.instances = instances;
		config.textureHandle = component->textureHandle;
		config.isSoftCircle = (component->textureHandle == 0); // 絵が無ければ丸いぼかし
		config.color = ApplyIntensity({1.0f, 1.0f, 1.0f, 1.0f}, component->intensity); // 全部の粒に掛ける色＝明るさの倍率だけ（リニアで掛ける。Color.h）
		config.blendMode = (component->blend == ParticleBlend::Add) ? BlendMode::Add : BlendMode::Normal;
		config.useFog = component->useFog;
		config.useDepthOfField = component->useDepthOfField;
		config.isSceneHidden = SceneVisibility::IsHidden(emitter.entity); // Hierarchy の目で隠した物は、Scene ビューだけ描かない
		Renderer::DrawParticleInstances(config);
	}
}

//=============================================================================
// ゲームから使う
//=============================================================================
// Play・Stop・Emit を呼ぶのはゲーム（Script・SYSTEM。どちらもゲームが動いている間だけ回る）なので、呼ばれたらゲームは動いている
// → 頭で SwitchGameRunning(true) をしておく。ゲームが動き出した最初のフレームは、Script が ParticleSystem::Update より先に回るので、
//   ここで切り替えないと、後の Update が「動き出した」として、この Play・Emit の分まで捨ててしまう
void ParticleSystem::Play(Handle<Entity> entity) {
	HTT_ASSERT_MSG(state != nullptr, "ParticleSystem::Initialize()を先に呼んでください");
	SwitchGameRunning(true);
	if (!EntityManager::IsAlive(entity)) {
		return;
	}
	Restart(FindOrAddEmitter(entity));
}

void ParticleSystem::Stop(Handle<Entity> entity, bool clear) {
	HTT_ASSERT_MSG(state != nullptr, "ParticleSystem::Initialize()を先に呼んでください");
	SwitchGameRunning(true);
	if (!EntityManager::IsAlive(entity)) {
		return;
	}
	EmitterState& emitter = FindOrAddEmitter(entity);
	emitter.isStartChecked = true; // 先に Stop したら、Play On Start でも流し始めない
	emitter.isPlaying = false;
	emitter.isBurstPending = false;
	if (clear) {
		emitter.particles.clear();
	}
}

void ParticleSystem::Emit(Handle<Entity> entity, int count) {
	HTT_ASSERT_MSG(state != nullptr, "ParticleSystem::Initialize()を先に呼んでください");
	SwitchGameRunning(true);
	const ParticleSystemComponent* component = EntityManager::Get<ParticleSystemComponent>(entity);
	if (component == nullptr || count <= 0) {
		return;
	}
	EmitParticles(FindOrAddEmitter(entity), *component, WorldMatrixOf(entity), count);
}

bool ParticleSystem::IsAlive(Handle<Entity> entity) {
	HTT_ASSERT_MSG(state != nullptr, "ParticleSystem::Initialize()を先に呼んでください");
	const EmitterState* emitter = FindEmitter(entity);
	return (emitter != nullptr) && (emitter->isPlaying || !emitter->particles.empty());
}

//=============================================================================
// エディタ（Inspector の Preview）
//=============================================================================
void ParticleSystem::StartPreview(const ParticleSystemComponent& component) {
	HTT_ASSERT_MSG(state != nullptr, "ParticleSystem::Initialize()を先に呼んでください");
	const Handle<Entity> owner = FindOwner(component);
	if (!owner.IsValid()) {
		return;
	}
	// 再生中は、最初からもう一度流すだけ（ゲームの粒はそのまま）
	if (state->wasGameRunning) {
		Play(owner);
		return;
	}
	state->emitters.clear();
	state->previewEntity = owner;
	Restart(FindOrAddEmitter(owner));
}

void ParticleSystem::StopPreview() {
	HTT_ASSERT_MSG(state != nullptr, "ParticleSystem::Initialize()を先に呼んでください");
	state->previewEntity = {};
	// 停止中に持っている粒は Preview の物だけなので、全部捨てる
	if (!state->wasGameRunning) {
		state->emitters.clear();
	}
}

bool ParticleSystem::IsPreviewing(const ParticleSystemComponent& component) {
	HTT_ASSERT_MSG(state != nullptr, "ParticleSystem::Initialize()を先に呼んでください");
	return state->previewEntity.IsValid() && EntityManager::Get<ParticleSystemComponent>(state->previewEntity) == &component;
}