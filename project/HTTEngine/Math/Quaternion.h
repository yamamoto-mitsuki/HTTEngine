#pragma once
#include "HTTEngine/Math/Matrix4x4.h"
#include "HTTEngine/Math/Vector3.h"

/// <summary>
/// クォータニオン
/// <para>回すのに使う物は、長さ1（Normalize した物）
/// </summary>
struct Quaternion {
	float x = 0.0f; // 虚部 i（軸の x × sin(角度/2)）
	float y = 0.0f; // 虚部 j（軸の y × sin(角度/2)）
	float z = 0.0f; // 虚部 k（軸の z × sin(角度/2)）
	float w = 1.0f; // 実部（cos(角度/2)）。初期値は「回っていない」
};

// ===== 演算子 =====
// 積。q1 * q2 は「q2 で回してから、q1 で回す」（行列と順番が逆なので注意。下の MakeRotateMatrix の解説）
Quaternion operator*(const Quaternion& q1, const Quaternion& q2);
// 全部の符号を反転した物。-q は q と同じ回転を表す（軸も角度も反対＝結局同じ向き）
Quaternion operator-(const Quaternion& q);

// ===== 基本の計算 =====
Quaternion IdentityQuaternion();                                 // 回っていない（0, 0, 0, 1）
Quaternion Multiply(const Quaternion& q1, const Quaternion& q2); // 積（operator* と同じ）
Quaternion Conjugate(const Quaternion& q);                       // 共役（虚部の符号を反転。長さ1なら逆の回転）
float Norm(const Quaternion& q);                                 // 長さ
Quaternion Normalize(const Quaternion& q);                       // 長さを1にする
Quaternion Inverse(const Quaternion& q);                         // 逆（掛けると回っていない状態に戻る）
float Dot(const Quaternion& q1, const Quaternion& q2);           // 内積（2つの向きがどれだけ近いか。±1 なら同じ向き）

// ===== 作る =====
// 軸 axis のまわりに angle（度）回す。axis は長さ1でなくてよい（中で長さ1にする）（Unity の Quaternion.AngleAxis と同じ）
Quaternion MakeRotateAxisAngleQuaternion(const Vector3& axis, float angle);
// オイラー角（度）から作る。Z → X → Y の順に回す（Unity の Quaternion.Euler・Inspector の Rotation と同じ）
Quaternion MakeQuaternionFromEuler(const Vector3& euler);
// 回転行列から作る。行列の1〜3行目が長さ1（大きさを割った後）で、直交していること
Quaternion MakeQuaternionFromMatrix(const Matrix4x4& rotateMatrix);
// ローカルの +Z（前）を forward へ、+Y（上）をなるべく up へ向ける回転（Unity の Quaternion.LookRotation と同じ）
// forward が up と同じ向き・0 のときは、up を変えて作る
Quaternion MakeLookRotation(const Vector3& forward, const Vector3& up = {0.0f, 1.0f, 0.0f});

// ===== 使う =====
// v を q で回す
Vector3 RotateVector(const Vector3& v, const Quaternion& q);
// 回転行列にする（MakeRotateXMatrix などと同じ、行ベクトル（v × 行列）の形）
Matrix4x4 MakeRotateMatrix(const Quaternion& q);
// 大きさ・回転・位置から行列を作る（MakeAffineMatrix の、回転をクォータニオンで渡す版）
Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Quaternion& rotate, const Vector3& translate);
// オイラー角（度。Z → X → Y の順）にする。どれも 0〜360 の中で返す（Unity の q.eulerAngles と同じ。-10 度は 350 度になる）
Vector3 ToEuler(const Quaternion& q);
// オイラー角（度）にする。同じ向きを表すオイラー角はたくさんあるので、hint（度）に一番近い物を返す
// （Inspector の数字が、少し回しただけで 180 度飛んだり、360 度の所で 0 に戻ったりしないように）
Vector3 ToEulerNearest(const Quaternion& q, const Vector3& hint);
// 球面線形補間。q0 から q1 へ、t（0〜1）の割合だけ、同じ速さで回った向き
Quaternion Slerp(const Quaternion& q0, const Quaternion& q1, float t);