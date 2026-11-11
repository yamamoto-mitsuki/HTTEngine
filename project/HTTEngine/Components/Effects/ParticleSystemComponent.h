#pragma once
#include <cstdint>
#include <type_traits>

#include "HTTEngine/Core/EnumTips.h"
#include "HTTEngine/Math/Easing.h"
#include "HTTEngine/Math/Vector2.h"
#include "HTTEngine/Math/Vector3.h"
#include "HTTEngine/Math/Vector4.h"

// パーティクル（小さな板をたくさん出して、動かして消す）。Entityに付けるComponent（カテゴリ：Effects）
// 粒の列は ParticleSystem が持つ。Component に可変長の配列は入れられない
// ここは「どう出すか」の設定だけ（Trail と点の列の関係と同じ）。Unity の Particle System の Main・Emission・Shape・Over Lifetime の小さい版

/// <summary>
/// 粒が出る場所と、飛ぶ向き（向きは Speed の向き）
/// </summary>
enum class ParticleShape {
	Point,  // Entity の位置から、全部の向きへ
	Sphere, // 球の中（From Surface なら表面）から、外へ
	Circle, // XY の面の円の中（From Surface なら輪）から、外へ（横から見るゲームで、輪から飛び散る）
	Cone,   // XZ の面の円から、上（+Y）を中心に Cone Angle まで開いて
	Box,    // 箱の中（From Surface なら表面）から、外へ
};

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(ParticleShape) {
	using enum ParticleShape;
	return MakeEnumTips<ParticleShape>({
	    {Point, "Entity の位置から、全部の向きへ"},
	    {Sphere, "球の中（From Surface なら表面）から、外へ"},
	    {Circle, "XY の面の円の中（From Surface なら輪）から、外へ（横から見るゲームで、輪から飛び散る）"},
	    {Cone, "XZ の面の円から、上（+Y）を中心に Cone Angle まで開いて"},
	    {Box, "箱の中（From Surface なら表面）から、外へ"},
	});
}

/// <summary>
/// 重ね方
/// </summary>
enum class ParticleBlend {
	Add,    // 足す（光る粒。重なるほど明るい）
	Normal, // 普通（奥から順には並べないので、重なると縁が汚くなることがある）
};

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(ParticleBlend) {
	using enum ParticleBlend;
	return MakeEnumTips<ParticleBlend>({
	    {Add, "足す（光る粒。重なるほど明るい）"},
	    {Normal, "普通（奥から順には並べないので、重なると縁が汚くなることがある）"},
	});
}

/// <summary>
/// 出た粒が、Entity に付いていくか
/// </summary>
enum class ParticleSpace {
	World, // 出た所に残る（Entity が動いても粒は付いていかない。煙・火花）
	Local, // Entity と一緒に動く・回る（動く物にまとわりつくキラキラ）
};

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(ParticleSpace) {
	using enum ParticleSpace;
	return MakeEnumTips<ParticleSpace>({
	    {World, "出た所に残る（Entity が動いても付いていかない。煙・火花）"},
	    {Local, "Entity と一緒に動く・回る（動く物にまとわりつくキラキラ）"},
	});
}

/// <summary>
/// 出し終わったとき（出すのをやめて、粒が全部消えたとき）にすること。Unity の Stop Action
/// <para>出すのをやめる＝Looping でなく Duration が過ぎた・ゲームが ParticleSystem::Stop を呼んだ。一度も Play していない物（Play On Start を外して Play を呼んでいない・Emit だけ）は出し終わりにならない</para>
/// </summary>
enum class ParticleStopAction {
	None,    // 何もしない（Entity は残る。また Play できる）
	Destroy, // この Component を持つ Entity を消す（子も一緒に消える。飛び散る物の Prefab を Instantiate して、出し終わったら勝手に消す）
};

// 選択肢ごとの説明（Inspector で選択肢にマウスを乗せると出る。書き方は EnumTips.h）
constexpr auto EnumTips(ParticleStopAction) {
	using enum ParticleStopAction;
	return MakeEnumTips<ParticleStopAction>({
	    {None, "何もしない（Entity は残る。また Play できる）"},
	    {Destroy, "この Entity を消す（子も一緒に消える。飛び散る物の Prefab を出して、出し終わったら勝手に消す）"},
	});
}

/// <summary>
/// パーティクルの設定
/// <para>Min〜Max の項目（Vector2）は、x が小さい方・y が大きい方。逆に入れても、その間で選ぶ</para>
/// </summary>
struct ParticleSystemComponent {
	bool enabled = true; // false の間は出さない・動かさない・描かない（粒は消さずに残す）

	// ===== 再生 =====
	bool playOnStart = true; // Play で流し始める（false：ゲームが ParticleSystem::Play を呼ぶまで出さない。使ったら飛び散る用）
	bool looping = true;     // Duration 秒ごとにくり返す（Burst もまた出る）。false：1回で出すのをやめる
	float duration = 1.0f;   // 1回の長さ（秒）
	ParticleStopAction stopAction = ParticleStopAction::None; // 出し終わったら（出すのをやめて、粒が全部消えたら）すること

	// ===== 出す量 =====
	float rateOverTime = 10.0f; // 1秒に出す数（0 で Burst だけ）
	int burstCount = 0;         // 1回の始めに、まとめて出す数
	int maxParticles = 256;     // 同時に出ている数の上限（ParticleSystem::kMaxParticles まで）。いっぱいの間は出さない

	// ===== 出る場所と向き（Shape）=====
	ParticleShape shape = ParticleShape::Sphere;
	float radius = 0.5f;                  // Sphere・Circle・Cone の半径（m）
	float coneAngle = 25.0f;              // Cone の開き（度。真上から何度まで）
	Vector3 boxSize = {1.0f, 1.0f, 1.0f}; // Box の大きさ（m）
	bool fromSurface = false;             // 形の表面（縁）から出す（円の輪から飛び散る）
	bool showGizmo = true;                // Scene ビューに、形を線で出す（エディタだけ。View メニューの Particle Shapes で全部まとめて消せる）

	// ===== 粒の初めの値（Min〜Max の乱数）=====
	Vector2 lifetime = {0.5f, 1.0f};           // 消えるまでの秒
	Vector2 speed = {1.0f, 2.0f};              // Shape の向きへの速さ（m/s）
	Vector2 size = {0.1f, 0.2f};               // 大きさ（m。板の1辺）
	Vector4 colorA = {1.0f, 1.0f, 1.0f, 1.0f}; // この2色の間でランダム（同じ色にすれば1色）
	Vector4 colorB = {1.0f, 1.0f, 1.0f, 1.0f};
	float intensity = 1.0f; // 明るさの倍率（全部の粒の色に掛ける。1 より大きいと HDR の明るさになり、ブルームで光る）

	// ===== 生きている間（Over Lifetime）=====
	Vector3 gravity = {0.0f, 0.0f, 0.0f};         // 加速（m/s²。下へ落とすなら -Y、上へ昇らせるなら +Y）
	float drag = 0.0f;                            // 1秒あたりの減速の割合（0 で減らない。大きいほどすぐ止まる火花）
	float endSizeRate = 1.0f;                     // 最後の大きさ（初めの大きさに掛ける。0 で消えるように縮む）
	Vector4 endColor = {1.0f, 1.0f, 1.0f, 0.0f};  // 最後の色（初めの色に掛ける。A 0 でフェードアウト）
	Ease::Type ease = Ease::Type::Linear;         // 大きさと色の変わり方
	bool converge = false;                        // 中心へ集まる（出た所から Entity の位置へ動き、寿命の終わりにちょうど着く。Speed・Gravity・Drag は使わない）
	Ease::Type convergeEase = Ease::Type::InQuad; // 集まり方（InQuad：だんだん速く吸い込まれる）

	// ===== 回転（Rotation）=====
	// 角度は画面の上での板の回り方（粒はいつもカメラの方を向くので、回すのは画面の上だけ）。正で左回り（Transform の Rotation Z と同じ）
	Vector2 startRotation = {0.0f, 0.0f};   // 初めの角度（度。Min〜Max の乱数。-180〜180 で全部の向き）
	Vector2 angularVelocity = {0.0f, 0.0f}; // 回る速さ（度/秒。Min〜Max の乱数）
	bool randomSpinDirection = false;       // 半分の粒を逆にする（初めの角度も回る向きも。葉が全部同じ向きに回らないように）
	bool alignToVelocity = false;           // 絵の右（+X）を、画面の上で進む向きに向ける（Start Rotation はそこから足す。風の筋・火花）
	float stretch = 1.0f;                   // 絵の横（X）だけに掛ける長さの倍率（Align To Velocity と合わせると、進む向きに細長い線）

	// ===== 見た目 =====
	uint32_t textureHandle = 0;                 // 粒の絵。0 なら丸いぼかし（白。色は Color で付ける）
	ParticleBlend blend = ParticleBlend::Add;   // 重ね方
	ParticleSpace space = ParticleSpace::World; // 粒が Entity に付いていくか
	bool useFog = true;                         // 霧（Fog）を混ぜる（外すと、霧の中でもそのままの色。光る粒をくっきり見せる）
	bool useDepthOfField = false;               // 被写界深度でぼかす（奥の層の葉・湯気など。ぼけ方は粒の後ろにある物の距離で決まる）
};

static_assert(std::is_trivially_copyable_v<ParticleSystemComponent>);