#include "Quaternion.h"

#include <array>
#include <cfloat>
#include <cmath>

#include "HTTEngine/Math/MathUtility.h"

namespace {
constexpr float kFullTurn = 360.0f;                             // 1回転（度）
constexpr float kEpsilon = 1e-6f;                               // これより短い長さは0とみなす
constexpr float kSlerpLinearDot = 0.9995f;                      // Slerp で、内積がこれより大きい（ほとんど同じ向き）なら、ふつうの線形補間にする
constexpr float kGimbalLockCos = 1e-6f;                         // |cos(x)| がこれ以下なら、ジンバルロック（x がちょうど ±90 度）

// angle（度）を、360 度ずつずらした値の中で hint に一番近い物にする
// std::remainder(a, b) は「a を b で割った余り」を、-b/2 〜 +b/2 の範囲で返す（std::fmod は 0 〜 b の側で返す）
float WrapNear(float angle, float hint) { return hint + std::remainder(angle - hint, kFullTurn); }

// angle（度）を 0〜360 の中にする（Unity の eulerAngles と同じ範囲）
float WrapTo360(float angle) {
	const float wrapped = angle - kFullTurn * std::floor(angle / kFullTurn);
	// -0.00001 は 359.99999 になるはずだが、float では 360 に丸まることがある。360 は 0 と同じなので 0 にする
	return (wrapped >= kFullTurn) ? 0.0f : wrapped;
}

// 回転の行列の |cos(x)|（x は Z → X → Y の順のオイラー角の x）
// 右.y = cos(x)sin(z)、上.y = cos(x)cos(z) なので、2乗して足すと cos(x)²（下の EulerFromRotateMatrix の解説）
// sin(x) = −前.y だけで判定すると、x が 89.95 度でも sin は 0.9999996 で「ちょうど90度」と区別できない
float AbsCosX(const Matrix4x4& rotateMatrix) {
	const auto& m = rotateMatrix.m;
	return std::sqrt(m[0][1] * m[0][1] + m[1][1] * m[1][1]);
}

// 回転の行列から、オイラー角（度。Z → X → Y の順）を取り出す。x は -90〜90、y と z は -180〜180
// 行列の1〜3行目は「ローカルの右（+X）・上（+Y）・前（+Z）が、親から見てどちらを向くか」。Z → X → Y の順に回すと、
//   前 = (sin(y)cos(x), −sin(x), cos(y)cos(x))  → 前の向きだけで、y（左右を向く）と x（上下を向く）が決まる
//   右を、y と x の分だけ逆に回して戻すと (cos(z), sin(z), 0) になる → z（前の軸のまわりに傾ける）が決まる
// cos(x) が 0（真上・真下を向いた。ジンバルロック）のときは、y と z が同じ軸のまわりの回転になって区別できないので、z = 0 にして y に入れる
Vector3 EulerFromRotateMatrix(const Matrix4x4& rotateMatrix) {
	const auto& m = rotateMatrix.m;
	const float cosX = AbsCosX(rotateMatrix);
	Vector3 euler;
	euler.x = std::atan2(-m[2][1], cosX);
	if (cosX > kGimbalLockCos) {
		euler.y = std::atan2(m[2][0], m[2][2]);
		// 右 = (m[0][0], m[0][1], m[0][2]) を、Y のまわりに −y、X のまわりに −x 回すと (cos(z), sin(z), 0)
		// z を「右.y と 上.y」から別に出すと、ジンバルロックの近く（cos(x) がとても小さい）で y に乗った誤差がそのまま残り、
		// x・y・z から作り直した向きがずれる。出した y を使って戻すと、y の誤差を z が打ち消すので、向きはずれない
		const float sinY = std::sin(euler.y);
		const float cosY = std::cos(euler.y);
		const float cosZ = m[0][0] * cosY - m[0][2] * sinY;
		const float sinZ = m[0][1] * std::cos(euler.x) + (m[0][0] * sinY + m[0][2] * cosY) * std::sin(euler.x);
		euler.z = std::atan2(sinZ, cosZ);
	} else {
		// x = +90 なら 右 = (cos(y − z), 0, −sin(y − z))、x = −90 なら 右 = (cos(y + z), 0, −sin(y + z))
		// どちらも「決まっている分（y ∓ z）」が atan2 で出るので、z = 0 として y に入れる
		euler.y = std::atan2(-m[0][2], m[0][0]);
		euler.z = 0.0f;
	}
	return euler * MathUtility::kRadToDeg;
}
} // namespace

//=============================================================================
// 演算子
//=============================================================================
Quaternion operator*(const Quaternion& q1, const Quaternion& q2) { return Multiply(q1, q2); }
Quaternion operator-(const Quaternion& q) { return {-q.x, -q.y, -q.z, -q.w}; }

//=============================================================================
// 基本の計算
//=============================================================================
Quaternion IdentityQuaternion() { return {0.0f, 0.0f, 0.0f, 1.0f}; }

// q1 = (v1, w1)、q2 = (v2, w2)（v は虚部の3つ）とすると
//   q1 q2 = (w1 v2 + w2 v1 + v1 × v2,  w1 w2 − v1・v2)
// 外積（v1 × v2）が入るので、掛ける順番を入れ替えると結果が変わる
Quaternion Multiply(const Quaternion& q1, const Quaternion& q2) {
	return {
	    q1.w * q2.x + q1.x * q2.w + q1.y * q2.z - q1.z * q2.y,
	    q1.w * q2.y + q1.y * q2.w + q1.z * q2.x - q1.x * q2.z,
	    q1.w * q2.z + q1.z * q2.w + q1.x * q2.y - q1.y * q2.x,
	    q1.w * q2.w - q1.x * q2.x - q1.y * q2.y - q1.z * q2.z,
	};
}

Quaternion Conjugate(const Quaternion& q) { return {-q.x, -q.y, -q.z, q.w}; }

float Norm(const Quaternion& q) { return std::sqrt(Dot(q, q)); }

Quaternion Normalize(const Quaternion& q) {
	const float norm = Norm(q);
	if (norm < kEpsilon) {
		return IdentityQuaternion(); // 長さ0は向きが無いので、回っていない物にする
	}
	return {q.x / norm, q.y / norm, q.z / norm, q.w / norm};
}

// q × 共役 = 長さの2乗（実数）になるので、共役を長さの2乗で割ると逆になる
Quaternion Inverse(const Quaternion& q) {
	const float normSq = Dot(q, q);
	if (normSq < kEpsilon) {
		return IdentityQuaternion();
	}
	const Quaternion conjugate = Conjugate(q);
	return {conjugate.x / normSq, conjugate.y / normSq, conjugate.z / normSq, conjugate.w / normSq};
}

float Dot(const Quaternion& q1, const Quaternion& q2) { return q1.x * q2.x + q1.y * q2.y + q1.z * q2.z + q1.w * q2.w; }

//=============================================================================
// 作る
//=============================================================================
Quaternion MakeRotateAxisAngleQuaternion(const Vector3& axis, float angle) {
	const Vector3 n = Normalize(axis);
	if (LengthSq(n) < kEpsilon) {
		return IdentityQuaternion(); // 軸が無い
	}
	const float halfRadian = angle * MathUtility::kDegToRad * 0.5f; // sin・cos はラジアンで渡す
	const float s = std::sin(halfRadian);
	return {n.x * s, n.y * s, n.z * s, std::cos(halfRadian)};
}

// Unity と同じ Z → X → Y の順に回す。Y（左右を向く）を最後に回すので、Y はいつも親の上（+Y）のまわりに回り、
// X（上下を向く）は物の横の軸で倒れ、Z（傾ける）は物の前の軸のまわりに傾く（人やカメラの向きを、そのまま数字で書ける）
// クォータニオンは「右から順に回す」ので、Y × X × Z と逆に並べる
Quaternion MakeQuaternionFromEuler(const Vector3& euler) {
	const Quaternion qx = MakeRotateAxisAngleQuaternion({1.0f, 0.0f, 0.0f}, euler.x);
	const Quaternion qy = MakeRotateAxisAngleQuaternion({0.0f, 1.0f, 0.0f}, euler.y);
	const Quaternion qz = MakeRotateAxisAngleQuaternion({0.0f, 0.0f, 1.0f}, euler.z);
	return qy * qx * qz;
}

// MakeRotateMatrix の逆。行列の中には、こう入っている（m は MakeRotateMatrix の結果）
//   m[0][0] + m[1][1] + m[2][2] = 4w² − 1        → w を出せる
//   m[1][2] − m[2][1] = 4wx、m[2][0] − m[0][2] = 4wy、m[0][1] − m[1][0] = 4wz
// w が0に近いと割り算できないので、x・y・z のうち一番大きい物から出す（大きい物で割ると誤差が小さい）
Quaternion MakeQuaternionFromMatrix(const Matrix4x4& rotateMatrix) {
	const auto& m = rotateMatrix.m;
	const float trace = m[0][0] + m[1][1] + m[2][2];
	Quaternion q;
	if (trace > 0.0f) {
		const float s = std::sqrt(trace + 1.0f) * 2.0f; // 4w
		q.w = 0.25f * s;
		q.x = (m[1][2] - m[2][1]) / s;
		q.y = (m[2][0] - m[0][2]) / s;
		q.z = (m[0][1] - m[1][0]) / s;
	} else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
		const float s = std::sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2.0f; // 4x
		q.w = (m[1][2] - m[2][1]) / s;
		q.x = 0.25f * s;
		q.y = (m[0][1] + m[1][0]) / s;
		q.z = (m[2][0] + m[0][2]) / s;
	} else if (m[1][1] > m[2][2]) {
		const float s = std::sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2.0f; // 4y
		q.w = (m[2][0] - m[0][2]) / s;
		q.x = (m[0][1] + m[1][0]) / s;
		q.y = 0.25f * s;
		q.z = (m[1][2] + m[2][1]) / s;
	} else {
		const float s = std::sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2.0f; // 4z
		q.w = (m[0][1] - m[1][0]) / s;
		q.x = (m[2][0] + m[0][2]) / s;
		q.y = (m[1][2] + m[2][1]) / s;
		q.z = 0.25f * s;
	}
	return Normalize(q);
}

// 回転行列の1〜3行目は「ローカルの X・Y・Z 軸が、ワールドでどちらを向くか」。その3本を作って、行列から作る
// 左手座標系なので、右 = 上 × 前、上 = 前 × 右
Quaternion MakeLookRotation(const Vector3& forward, const Vector3& up) {
	const Vector3 axisZ = Normalize(forward);
	if (LengthSq(axisZ) < kEpsilon) {
		return IdentityQuaternion(); // 向きが無い
	}
	Vector3 axisX = Cross(up, axisZ);
	if (LengthSq(axisX) < kEpsilon) {
		// 前と上が同じ向き（真上・真下を見る）：上が決まらないので、ワールドの前（真上を見ているときは右）を上の代わりにする
		const Vector3 otherUp = (std::abs(axisZ.z) < 0.9f) ? Vector3{0.0f, 0.0f, 1.0f} : Vector3{1.0f, 0.0f, 0.0f};
		axisX = Cross(otherUp, axisZ);
	}
	axisX = Normalize(axisX);
	const Vector3 axisY = Cross(axisZ, axisX);

	Matrix4x4 rotateMatrix = MakeIdentity4x4();
	const std::array<Vector3, 3> axes = {axisX, axisY, axisZ};
	for (int row = 0; row < 3; ++row) {
		rotateMatrix.m[row][0] = axes[row].x;
		rotateMatrix.m[row][1] = axes[row].y;
		rotateMatrix.m[row][2] = axes[row].z;
	}
	return MakeQuaternionFromMatrix(rotateMatrix);
}

//=============================================================================
// 使う
//=============================================================================
// v を、虚部だけのクォータニオン (v, 0) にして、q × v × 共役(q) を計算すると、回した v が虚部に出てくる
Vector3 RotateVector(const Vector3& v, const Quaternion& q) {
	const Quaternion rotated = q * Quaternion{v.x, v.y, v.z, 0.0f} * Conjugate(q);
	return {rotated.x, rotated.y, rotated.z};
}

// RotateVector の計算を行列の形に書き直した物
// エンジンの行列は「v × 行列」（行ベクトル）なので、教科書によくある「行列 × v」（列ベクトル）の形を転置した並び
Matrix4x4 MakeRotateMatrix(const Quaternion& q) {
	const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
	const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
	const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
	Matrix4x4 result = MakeIdentity4x4();
	result.m[0][0] = 1.0f - 2.0f * (yy + zz);
	result.m[0][1] = 2.0f * (xy + wz);
	result.m[0][2] = 2.0f * (xz - wy);
	result.m[1][0] = 2.0f * (xy - wz);
	result.m[1][1] = 1.0f - 2.0f * (xx + zz);
	result.m[1][2] = 2.0f * (yz + wx);
	result.m[2][0] = 2.0f * (xz + wy);
	result.m[2][1] = 2.0f * (yz - wx);
	result.m[2][2] = 1.0f - 2.0f * (xx + yy);
	return result;
}

// 大きさ × 回転 × 位置 を、行列の掛け算をせずに直接組み立てる（EntityManager::UpdateTransforms が毎フレーム全部の物で呼ぶので、速くしておく）
//   1〜3行目 … 回転の行列の i 行目 × i 番目の大きさ（大きさの行列を左から掛けると、行ごとに大きさが掛かる）
//   4行目   … 位置（位置の行列を右から掛けると、4行目に位置が入るだけ）
Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Quaternion& rotate, const Vector3& translate) {
	Matrix4x4 result = MakeRotateMatrix(rotate);
	const std::array<float, 3> scales = {scale.x, scale.y, scale.z};
	for (int row = 0; row < 3; ++row) {
		result.m[row][0] *= scales[row];
		result.m[row][1] *= scales[row];
		result.m[row][2] *= scales[row];
	}
	result.m[3][0] = translate.x;
	result.m[3][1] = translate.y;
	result.m[3][2] = translate.z;
	return result;
}

// 回転行列にしてから角度を取り出し、0〜360 にそろえる
Vector3 ToEuler(const Quaternion& q) {
	const Vector3 euler = EulerFromRotateMatrix(MakeRotateMatrix(Normalize(q)));
	return {WrapTo360(euler.x), WrapTo360(euler.y), WrapTo360(euler.z)};
}

// 同じ向きを表すオイラー角は、次の3通りの分だけたくさんある
//   1. どの軸も 360 度ずらしてよい（同じ向き）
//   2. (x, y, z) と (180 − x, y + 180, z + 180) は同じ向き（Y と Z を半回転すると、X の倒れ方が裏返る）
//   3. x がちょうど ±90 度（ジンバルロック）のときは、Y と Z が同じ軸のまわりの回転になり、y − z（x が −90 なら y + z）しか決まらない
// この中から hint に一番近い物を選ぶ
Vector3 ToEulerNearest(const Quaternion& q, const Vector3& hint) {
	const Matrix4x4 rotateMatrix = MakeRotateMatrix(Normalize(q));
	Vector3 euler = EulerFromRotateMatrix(rotateMatrix);

	// --- 3. ジンバルロック：z は hint のままにして、決まっている分を y に入れる ---
	if (AbsCosX(rotateMatrix) <= kGimbalLockCos) {
		// EulerFromRotateMatrix は z = 0 にして、x = +90 なら y − z、x = −90 なら y + z を y に入れている
		euler.y += (euler.x > 0.0f) ? hint.z : -hint.z;
		euler.z = hint.z;
	}

	// --- 1. と 2. の候補から、一番近い物 ---
	const std::array<Vector3, 2> candidates = {
	    euler,
	    Vector3{180.0f - euler.x, euler.y + 180.0f, euler.z + 180.0f},
	};
	Vector3 best = euler;
	float bestDistance = FLT_MAX;
	for (const Vector3& candidate : candidates) {
		const Vector3 wrapped = {WrapNear(candidate.x, hint.x), WrapNear(candidate.y, hint.y), WrapNear(candidate.z, hint.z)};
		const float distance = LengthSq(wrapped - hint);
		if (distance < bestDistance) {
			bestDistance = distance;
			best = wrapped;
		}
	}
	return best;
}

// 2つの向きの間の角度を θ とすると（cosθ = 内積）
//   Slerp = q0 × sin((1 − t)θ) / sinθ ＋ q1 × sin(tθ) / sinθ
// ふつうの線形補間と違って、t を同じだけ進めると、同じ角度だけ回る
Quaternion Slerp(const Quaternion& q0, const Quaternion& q1, float t) {
	Quaternion end = q1;
	float dot = Dot(q0, q1);
	// 内積が負なら、q1 の代わりに -q1（同じ向き）へ向かう。そうしないと、遠回り（360 度 − θ）で回ってしまう
	if (dot < 0.0f) {
		end = -end;
		dot = -dot;
	}
	// ほとんど同じ向き：sinθ が0に近くて割り算できないので、ふつうの線形補間をして長さを1に戻す
	if (dot > kSlerpLinearDot) {
		const Quaternion lerp = {
		    q0.x + (end.x - q0.x) * t,
		    q0.y + (end.y - q0.y) * t,
		    q0.z + (end.z - q0.z) * t,
		    q0.w + (end.w - q0.w) * t,
		};
		return Normalize(lerp);
	}
	const float theta = std::acos(dot);
	const float sinTheta = std::sin(theta);
	const float scale0 = std::sin((1.0f - t) * theta) / sinTheta;
	const float scale1 = std::sin(t * theta) / sinTheta;
	return {
	    q0.x * scale0 + end.x * scale1,
	    q0.y * scale0 + end.y * scale1,
	    q0.z * scale0 + end.z * scale1,
	    q0.w * scale0 + end.w * scale1,
	};
}