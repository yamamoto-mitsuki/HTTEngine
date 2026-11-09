#include "ElasticSurfaceSimulation.h"

#include <cmath>
#include <numbers>
#include <algorithm>


//=============================================================================
// x の周りの点に重みを配る（AddForce より前に書く。テンプレートなので、使う所より前に中身が要る）
//=============================================================================
template<class Function>
void ElasticSurfaceSimulation::ForEachWeighted(float x, float radius, Function function) const { 
	const size_t count = heights_.size();
	if (count < 3) {
		return; // 支柱が2つと、動く点が最低1つ
	}
	// 半径が点の間隔より狭いと、どの点にも届かないことがあるので、最低でも間隔1つ分にする
	const float reach = (std::max)(radius, spacing_);
	// 届く範囲の点の番号。両端の支柱は動かないので、1～count-2に収める
	const float lastMovable = static_cast<float>(count - 2);
	const size_t first = static_cast<size_t>(std::clamp(std::ceil(ToIndex(x - reach)), 1.0f, lastMovable));
	const size_t last = static_cast<size_t>(std::clamp(std::floor(ToIndex(x + reach)), 1.0f, lastMovable));

	// 重み: 真ん中が1、半径の所で0になる山の形（cosの山）。端で急に途切れないので、ギザギザが出にくい
	const auto bump = [this, x, reach](size_t index) {
		const float distance = std::abs(GetPointX(index) - x) / reach; // 0（真ん中）〜 1（半径の所）
		return (distance < 1.0f) ? 0.5f * (1.0f + std::cos(std::numbers::pi_v<float> * distance)) : 0.0f;
	};
	// 合計を先に求めて、合計が1になるように割る（半径を変えても、押す強さの合計は同じになる）
	float total = 0.0f;
	for (size_t index = first; index <= last; ++index) {
		total += bump(index);
	}
	if (total <= 0.0f) {
		return; // 面の外（どの点にも届かない）
	}
	for (size_t index = first; index <= last; ++index) {
		function(index, bump(index) / total);
	}
}


//=============================================================================
// 設定
//=============================================================================
void ElasticSurfaceSimulation::SetSettings(const ElasticSurfaceSettings& settings) {
	// --- 変な値を直す ---
	ElasticSurfaceSettings fixed = settings;
	fixed.pointCount = (std::max)(fixed.pointCount, 3); // 支柱が2つと、動く点が最低1つ
	fixed.width = (std::max)(fixed.width, 0.1f);
	fixed.massPerMeter = (std::max)(fixed.massPerMeter, 0.01f);  // 0で割らないように
	fixed.edgeStiffness = (std::max)(fixed.edgeStiffness, 0.0f); // マイナスだと、端が押し出されて暴れる
	fixed.edgeDamping = (std::max)(fixed.edgeDamping, 0.0f);
	fixed.edgeFalloff = (std::max)(fixed.edgeFalloff, 0.0f);

	// --- 作り直しがいるか（幅か点の数が変わった・まだ作っていない）---
	const bool needRebuild = (heights_.size() != static_cast<size_t>(fixed.pointCount)) || (fixed.width != settings_.width);
	// 端の効き方を計算し直すか（作り直す・効き方の形が変わった）
	const bool needEdgeWeights = needRebuild || (fixed.edgeFalloff != settings_.edgeFalloff) || edgeWeights_.empty();
	settings_ = fixed;
	if (needRebuild) {
		spacing_ = settings_.width / static_cast<float>(settings_.pointCount - 1);
		const size_t count = static_cast<size_t>(settings_.pointCount);
		heights_.assign(count, 0.0f);
		velocities_.assign(count, 0.0f);
		forces_.assign(count, 0.0f);
		accelerations_.assign(count, 0.0f);
	}
	if (needEdgeWeights) {
		// 端の効き方：真ん中からの距離を幅の半分で割った値（真ん中0・支柱1）を edgeFalloff 乗する
		// edgeFalloff 1 なら真っすぐ増える、2 なら真ん中の近くはほとんど0で端で急に増える、0 ならどこでも1（全体が同じだけ硬い）
		const float halfWidth = settings_.width * 0.5f;
		edgeWeights_.resize(heights_.size());
		for (size_t i = 0; i < edgeWeights_.size(); ++i) {
			edgeWeights_[i] = std::pow(std::abs(GetPointX(i)) / halfWidth, settings_.edgeFalloff);
		}
	}
}

// ===== 平らに戻す（揺れも止める）=====
void ElasticSurfaceSimulation::Flatten() {
	std::ranges::fill(heights_, 0.0f);
	std::ranges::fill(velocities_, 0.0f);
	std::ranges::fill(forces_, 0.0f);
}


//=============================================================================
// 進める
//=============================================================================
void ElasticSurfaceSimulation::Step(float dt) {
	const size_t count = heights_.size();
	if (count < 3) {
		return; // 支柱が2つと、動く点が最低1つ
	}
	// ===== 毎回同じ値を先に計算 =====
	const float pointMass = settings_.massPerMeter * spacing_;            // 点1つの重さ（kg）
	const float tensionTerm = settings_.tension / (pointMass * spacing_); // 曲がりを加速度に直す係数（張力 ÷ 点の重さ ÷ 間隔）
	const float smoothTerm = settings_.smoothing / (spacing_ * spacing_); // 速さの曲がりを加速度に直す係数
	const float inversePointMass = 1.0f / pointMass;                      // F = ma → a = F/m
	const float edgeSpringTerm = settings_.edgeStiffness / settings_.massPerMeter; // 端のバネを加速度に直す係数（点の力 k×間隔×高さ ÷ 点の重さ μ×間隔）

	// ===== 1. 今の高さ・速さから、加速度を求める（両端の支柱は動かないので飛ばす）=====
	for (size_t i = 1; i + 1 < count; ++i) {
		// 曲がり: 隣の2点の真ん中より低いと＋
		const float bend = heights_[i - 1] + heights_[i + 1] - 2.0f * heights_[i];
		// 速さの曲がり: 隣と速さがそろっていないほど大きい。細かいギザギザほど大きくなるので、そこだけ消せる
		const float velocityBend = velocities_[i - 1] + velocities_[i + 1] - 2.0f * velocities_[i];
		accelerations_[i] = 
			tensionTerm * bend                   // 張りでまっすぐに戻ろうとする
			- settings_.damping * velocities_[i] // 揺れが収まる
			+ smoothTerm * velocityBend          // 細かいギザギザを消す
		    - edgeSpringTerm * edgeWeights_[i] * heights_[i] // 端ほど強く、平らな位置へ引き戻す（端が沈みにくい。edgeWeights_ は真ん中0・支柱1）
			+ forces_[i] * inversePointMass;     // 乗っている物に押された力
	}

	// ===== 2. 速さ → 高さの順に進める =====
	for (size_t i = 1; i + 1 < count; ++i) {
		// 端のブレーキは、速さを (1 + 強さ × dt) で割って減らす（Rigidbody の Drag と同じ形）
		// 加速度に「− 強さ × 速さ」と足す形だと、強さ × dt が 1 を超えたあたりから逆向きに振れて暴れる。割る形なら、いくら大きくしても0に近づくだけ
		const float edgeBrake = 1.0f + settings_.edgeDamping * edgeWeights_[i] * dt;
		velocities_[i] = (velocities_[i] + accelerations_[i] * dt) / edgeBrake;
		heights_[i] += velocities_[i] * dt;
	}

	// ===== 3. 使った力を消す（力は「その1歩の間だけ」の物）=====
	std::ranges::fill(forces_, 0.0f);
}


//=============================================================================
// 物とのぶつかり
//=============================================================================
float ElasticSurfaceSimulation::PushBack(float x, float y, float velocityY, float radius, float maxPenetration, 
	const ElasticContactSettings& contact) {
	// 支柱の外には面が無い
	if (heights_.size() < 3 || !IsInside(x)) {
		return 0.0f;
	}
	// どれだけ面より下に入っているか（入っていない・深すぎる = 面の下にいるなら押さない）
	const float penetration = GetHeight(x) - y;
	if (penetration <= 0.0f || penetration > maxPenetration) {
		return 0.0f;
	}
	// 面から見た上下の速さ（面と一緒に動いていれば0。面に向かって近づいていると負）
	const float relativeVelocity = velocityY - GetVelocity(x);
	// 押し返す力：めり込むほど強く（バネ）、近づく速さほど強く（ダンパー）。面は物を引っ張れないので、負にはしない
	const float force = (std::max)(contact.stiffness * penetration - contact.damping * relativeVelocity, 0.0f);

	// 反作用: 同じ力で面を押し下げる。面の点は上下
	const float slope = GetSlope(x);
	const float normalY = 1.0f / std::sqrt(1.0f + slope * slope); // 面に垂直な向き (-slope, 1) を長さ1にしたときの上下の成分
	AddForce(x, -force * normalY, radius);
	return force;
}

// ===== x の周り（半径 radius）に力を足す（N。下向きは負）。次の Step で使って消える =====
void ElasticSurfaceSimulation::AddForce(float x, float force, float radius) {
	ForEachWeighted(x, radius, [this, force](size_t index, float weight) { forces_[index] += force * weight; });
}

// ===== x の周り（半径 radius）を一瞬で叩く（N・s。下向きは負）。その場で速さが変わる（ヒップドロップなど） =====
void ElasticSurfaceSimulation::AddImpulse(float x, float impulse, float radius) {
	// 力積 ÷ 重さ ＝ 速さの変化
	const float inversePointMass = 1.0f / (settings_.massPerMeter * spacing_);
	ForEachWeighted(x, radius, [this, impulse, inversePointMass](size_t index, float weight) { velocities_[index] += impulse * weight * inversePointMass; });
}


//=============================================================================
// 読む
//=============================================================================
float ElasticSurfaceSimulation::GetHeight(float x) const {
	if (heights_.size() < 2) {
		return 0.0f;
	}
	float t = 0.0f;
	const size_t left = FindSegment(x, &t);
	return heights_[left] + (heights_[left + 1] - heights_[left]) * t;
}

float ElasticSurfaceSimulation::GetVelocity(float x) const {
	if (velocities_.size() < 2) {
		return 0.0f;
	}
	float t = 0.0f;
	const size_t left = FindSegment(x, &t);
	return velocities_[left] + (velocities_[left + 1] - velocities_[left]) * t;
}

float ElasticSurfaceSimulation::GetSlope(float x) const {
	if (heights_.size() < 2) {
		return 0.0f;
	}
	float t = 0.0f;
	const size_t left = FindSegment(x, &t);
	return (heights_[left + 1] - heights_[left]) / spacing_; // 点と点の間はまっすぐなので、傾きはその区間で同じ
}

bool ElasticSurfaceSimulation::IsInside(float x) const { return std::abs(x) <= settings_.width * 0.5f; }

float ElasticSurfaceSimulation::GetPointX(size_t index) const { return -settings_.width * 0.5f + spacing_ * static_cast<float>(index); }

float ElasticSurfaceSimulation::ToIndex(float x) const { return (x + settings_.width * 0.5f) / spacing_; }

size_t ElasticSurfaceSimulation::FindSegment(float x, float* t) const {
	// 面の外は端の点（支柱）の所として読む
	const float index = std::clamp(ToIndex(x), 0.0f, static_cast<float>(heights_.size() - 1));
	// 左の点の番号。右端ちょうどでも右隣があるように、最後の1つ手前までにする
	const size_t left = (std::min)(static_cast<size_t>(index), heights_.size() - 2);
	*t = index - static_cast<float>(left);
	return left;
}