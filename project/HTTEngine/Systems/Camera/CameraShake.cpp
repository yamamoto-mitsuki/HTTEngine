#include "CameraShake.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/Math/Easing.h"
#include "HTTEngine/Systems/Camera/CameraSystem.h"

namespace {
constexpr float kMinDamping = 0.01f;         // 減衰比の下限（0 だと永遠に揺れる）
constexpr float kMaxDamping = 0.99f;         // 減衰比の上限（1 以上は振れずに戻る別の式になる）
constexpr float kMinImpulseFrequency = 0.1f; // 衝撃の回数の下限
constexpr float kStopOffset = 0.0001f;       // ずれ（m）と
constexpr float kStopVelocity = 0.001f;      // 速さ（m/秒）がこれより小さくなったら、0 にして止める
constexpr float kTestStrength = 0.3f;        // Inspector の試しのボタンの強さ（m）
constexpr float kChannelShift = 0.37f;       // チャンネルごとにノイズを読む位置をずらす量（パーリンノイズは整数の所で必ず 0 になるので、全部が同時に 0 にならないように）

// ===== ノイズのチャンネル（1つずつ別の揺れ方をする）=====
enum NoiseChannel : uint32_t {
	kNoisePositionX,
	kNoisePositionY,
	kNoisePositionZ,
	kNoiseRotationX,
	kNoiseRotationY,
	kNoiseRotationZ,
};

// ===== 減衰振動の値 =====
struct SpringParam {
	float omega = 0.0f;       // ばねの強さ（1秒に回る角度。2π × 回数）
	float damping = 0.0f;     // 減衰比
	float dampedOmega = 0.0f; // 減衰があるときの、実際に揺れる速さ
};

SpringParam MakeSpringParam(const CameraShakeComponent& shake) {
	const float frequency = (std::max)(shake.impulseFrequency, kMinImpulseFrequency);
	const float damping = std::clamp(shake.impulseDamping, kMinDamping, kMaxDamping);
	const float omega = 2.0f * std::numbers::pi_v<float> * frequency;
	return {omega, damping, omega * std::sqrt(1.0f - damping * damping)};
}

//=============================================================================
// ノイズ（1次元のパーリンノイズ）
//=============================================================================
// ===== 整数の番号から、-1〜1 の傾きを作る（同じ番号なら、いつも同じ値）=====
float MakeGradient(int32_t index, uint32_t channel) {
	uint32_t hash = static_cast<uint32_t>(index) * 374761393u + channel * 668265263u;
	hash = (hash ^ (hash >> 13)) * 1274126177u;
	hash ^= hash >> 16;
	return static_cast<float>(hash) / static_cast<float>(UINT32_MAX) * 2.0f - 1.0f;
}

// ===== time の所のノイズ（-1〜1。time をなめらかに動かすと、なめらかに変わる）=====
float CalcNoise(float time, uint32_t channel) {
	const float shifted = time + static_cast<float>(channel) * kChannelShift;
	const float cell = std::floor(shifted);
	const float local = shifted - cell; // マスの中の位置（0〜1）
	const int32_t index = static_cast<int32_t>(cell);
	// マスの左右の端に傾きを置いて、それぞれの傾きの直線の高さを、なめらかな重みで混ぜる
	const float left = MakeGradient(index, channel) * local;
	const float right = MakeGradient(index + 1, channel) * (local - 1.0f);
	const float weight = local * local * local * (local * (local * 6.0f - 15.0f) + 10.0f); // 6t^5 - 15t^4 + 10t^3（端で傾きも曲がりも0になるので、マスの境目が目立たない）
	return Ease::Lerp(left, right, weight) * 2.0f;                                         // 1次元のパーリンノイズの値は ±0.5 に収まるので、2倍して ±1 にする
}
} // namespace

//=============================================================================
// Inspector と保存
//=============================================================================
// 実行中だけの値は並べない＝保存しない
void CameraShake::Describe(ComponentUI& ui, CameraShakeComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと揺らさない"));
	ui.Separator("Noise");
	ui.Field("Position###noisePosition", value.noisePosition, AtLeast(0.0f).Tip("位置の揺れ幅（m）。X：カメラの右 Y：上 Z：前"));
	ui.Field("Rotation###noiseRotation", value.noiseRotation, AtLeast(0.0f).Tip("向きの揺れ幅（度）。X：上下の首振り Y：左右の首振り Z：傾き"));
	ui.Field("Frequency###noiseFrequency", value.noiseFrequency, AtLeast(0.0f).Tip("1秒に揺れる回数の目安"));
	ui.Field("Gain###noiseGain", value.noiseGain, AtLeast(0.0f).Tip("揺れ幅に掛ける倍率。0 で止まる"));
	ui.Separator("Impulse");
	ui.Label("CameraSystem::AddImpulse(強さ) で受ける。強さは最初に振れる幅（m）");
	ui.Field("Gain###impulseGain", value.impulseGain, AtLeast(0.0f).Tip("受けた強さに掛ける倍率。0 なら受けない"));
	ui.Field("Frequency###impulseFrequency", value.impulseFrequency, AtLeast(kMinImpulseFrequency).Tip("1秒に揺れる回数"));
	ui.Field("Damping###impulseDamping", value.impulseDamping, Range(kMinDamping, kMaxDamping).Tip("収まる速さ（減衰比）。0 に近いほど長く揺れ、1 に近いほどすぐ止まる"));
	ui.Field("Rotation###impulseRotation", value.impulseRotation, AtLeast(0.0f).Tip("1m ずれたときに一緒に回す角度（度）。遠くの景色も揺れて見える。0 なら位置だけ揺れる"));
	if (ui.Button("Test Impulse")) {
		CameraSystem::AddImpulse(kTestStrength); // 再生中だけ揺れる
	}
}

//=============================================================================
// 揺れを進める
//=============================================================================
// 衝撃は減衰振動（ばね）の式をそのまま解いて進める。1歩ずつ足し込む方法と違い、deltaTime の大きさで結果が変わらない
//   x(t) = e^(-ζωt) × (x0 cos(ω'·t) + K sin(ω'·t))
//   K = (v0 + ζω x0) / ω'　　ω' = ω√(1 - ζ²)
//   v(t) = e^(-ζωt) × (v0 cos(ω'·t) - (ζω K + ω' x0) sin(ω'·t))
void CameraShake::Update(CameraShakeComponent& shake, float deltaTime) {
	shake.noiseTime += deltaTime * shake.noiseFrequency;
	if (LengthSq(shake.impulseOffset) == 0.0f && LengthSq(shake.impulseVelocity) == 0.0f) {
		return; // 衝撃を受けていない
	}
	const SpringParam spring = MakeSpringParam(shake);
	const float decay = spring.damping * spring.omega;
	const float exponent = std::exp(-decay * deltaTime);
	const float cosine = std::cos(spring.dampedOmega * deltaTime);
	const float sine = std::sin(spring.dampedOmega * deltaTime);
	const Vector3 k = (shake.impulseVelocity + shake.impulseOffset * decay) * (1.0f / spring.dampedOmega);
	const Vector3 offset = (shake.impulseOffset * cosine + k * sine) * exponent;
	const Vector3 velocity = (shake.impulseVelocity * cosine - (k * decay + shake.impulseOffset * spring.dampedOmega) * sine) * exponent;
	const bool isStopped = LengthSq(offset) < kStopOffset * kStopOffset && LengthSq(velocity) < kStopVelocity * kStopVelocity;
	shake.impulseOffset = isStopped ? Vector3{0.0f, 0.0f, 0.0f} : offset;
	shake.impulseVelocity = isStopped ? Vector3{0.0f, 0.0f, 0.0f} : velocity;
}

//=============================================================================
// 今のずれ
//=============================================================================
CameraShakeOffset CameraShake::CalcOffset(const CameraShakeComponent& shake) {
	const float time = shake.noiseTime;
	const float gain = shake.noiseGain;
	CameraShakeOffset offset;
	// --- ノイズ ---
	offset.position = {
	    shake.noisePosition.x * CalcNoise(time, kNoisePositionX) * gain,
	    shake.noisePosition.y * CalcNoise(time, kNoisePositionY) * gain,
	    shake.noisePosition.z * CalcNoise(time, kNoisePositionZ) * gain,
	};
	offset.rotation = {
	    shake.noiseRotation.x * CalcNoise(time, kNoiseRotationX) * gain,
	    shake.noiseRotation.y * CalcNoise(time, kNoiseRotationY) * gain,
	    shake.noiseRotation.z * CalcNoise(time, kNoiseRotationZ) * gain,
	};
	// --- 衝撃：位置のずれに合わせて向きも回す（右へずれたら右を向く＝画面が同じ向きへ流れて、揺れが強く見える）---
	offset.position += shake.impulseOffset;
	offset.rotation += Vector3{-shake.impulseOffset.y, shake.impulseOffset.x, shake.impulseOffset.z} * shake.impulseRotation;
	return offset;
}

//=============================================================================
// 衝撃を受ける
//=============================================================================
// x(t) = v0 / ω' × e^(-ζωt) × sin(ω't) の山の高さが strength になる v0 を、速さに足す
//   山は tan(ω't) = ω' / (ζω) の所で、高さは v0 / ω × e^(-ζωt)
void CameraShake::AddImpulse(CameraShakeComponent& shake, float strength, const Vector3& direction) {
	const SpringParam spring = MakeSpringParam(shake);
	const float decay = spring.damping * spring.omega;
	const float peakTime = std::atan2(spring.dampedOmega, decay) / spring.dampedOmega;
	const float speed = strength * shake.impulseGain * spring.omega * std::exp(decay * peakTime);
	shake.impulseVelocity += Normalize(direction) * speed;
}