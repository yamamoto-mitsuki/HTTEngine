#include "Curve.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <utility>

namespace {
constexpr int kMaxSolveIterations = 32;      // ベジェを解く回数の上限（float なら、二分法だけでも 24 回で精度の限界まで狭まる）
constexpr float kSolveTolerance = 0.000001f; // 解けたとみなす時間の差（区間の長さを 1 とした割合）
constexpr float kMinSolveSlope = 0.000001f;  // 傾きがこれより小さいと Newton 法の割り算をしない

constexpr float kFitTolerance = 0.0005f;       // Ease を写すときの、Ease::Apply との差の許容（値は 0〜1）
constexpr int kMaxFitDepth = 8;                // Ease を写すときに、区間を半分にする回数の上限
constexpr int kFitChecks = 16;                 // 1つの区間で、差を測る点の数
constexpr float kFitSlopeStep = 1.0f / 256.0f; // 傾きを測る幅（区間の長さに対する割合）

// 式が切り替わる所（Bounce は跳ねた瞬間に傾きが急に変わる。Easing.cpp の式の切り替わりと同じ値）
constexpr std::array<float, 3> kOutBounceJoints = {1.0f / 2.75f, 2.0f / 2.75f, 2.5f / 2.75f};
constexpr std::array<float, 3> kInBounceJoints = {1.0f - 2.5f / 2.75f, 1.0f - 2.0f / 2.75f, 1.0f - 1.0f / 2.75f};
constexpr std::array<float, 1> kMiddleJoint = {0.5f};

//=============================================================================
// 値を守る
//=============================================================================
// ===== NaN は 0、大きすぎる値は上限へ =====
float ClampValue(float value) {
	if (std::isnan(value)) {
		return 0.0f;
	}
	return std::clamp(value, -Curve::kMaxValue, Curve::kMaxValue);
}

// ===== ハンドルの時間の長さを limit までにする =====
// 時間だけ切ると傾きが変わるので、値も同じ割合で縮める
Vector2 LimitHandle(const Vector2& handle, float limit) {
	const float length = std::abs(handle.x);
	if (length <= limit) {
		return handle;
	}
	return handle * (limit / length);
}

// ===== キー1つの値を守る =====
CurveKey SanitizeKey(CurveKey key) {
	key.time = ClampValue(key.time);
	key.value = ClampValue(key.value);
	key.inHandle = {(std::min)(ClampValue(key.inHandle.x), 0.0f), ClampValue(key.inHandle.y)};
	key.outHandle = {(std::max)(ClampValue(key.outHandle.x), 0.0f), ClampValue(key.outHandle.y)};
	return key;
}

//=============================================================================
// 3次ベジェ
//=============================================================================
// ===== 媒介変数 s（0〜1）での位置 =====
float CubicBezier(float p0, float p1, float p2, float p3, float s) {
	const float u = 1.0f - s;
	return u * u * u * p0 + 3.0f * u * u * s * p1 + 3.0f * u * s * s * p2 + s * s * s * p3;
}

// ===== s での傾き（CubicBezier を s で微分した物）=====
float CubicBezierSlope(float p0, float p1, float p2, float p3, float s) {
	const float u = 1.0f - s;
	return 3.0f * (u * u * (p1 - p0) + 2.0f * u * s * (p2 - p1) + s * s * (p3 - p2));
}

// ===== 時間の割合 target（0〜1）になる s を求める =====
// 時間の制御点は 0, x1, x2, 1。x1 と x2 が 0〜1 の中なら、時間は s に対して減らない（解は1つ）
// Newton 法で速く近づき、傾きが 0 に近くて飛び出すときだけ二分法にする。回数の上限があるので必ず終わる
float SolveBezierParam(float x1, float x2, float target) {
	float low = 0.0f;  // 答えはここより右
	float high = 1.0f; // 答えはここより左
	float s = target;  // 最初の予想。ハンドルが等間隔なら、これがそのまま答え
	for (int i = 0; i < kMaxSolveIterations; ++i) {
		const float error = CubicBezier(0.0f, x1, x2, 1.0f, s) - target;
		if (std::abs(error) < kSolveTolerance) {
			break;
		}
		// --- 答えのある範囲を狭める ---
		if (error > 0.0f) {
			high = s;
		} else {
			low = s;
		}
		// --- Newton 法の次の場所。範囲の外へ出るなら、範囲の真ん中にする ---
		const float slope = CubicBezierSlope(0.0f, x1, x2, 1.0f, s);
		const float next = (slope > kMinSolveSlope) ? s - error / slope : -1.0f;
		s = (next > low && next < high) ? next : (low + high) * 0.5f;
	}
	return s;
}

// ===== a から b までのベジェで、time になる s を求める（a.time < b.time のとき）=====
float CalcBezierParam(const CurveKey& a, const CurveKey& b, float time) {
	const float duration = b.time - a.time;
	return SolveBezierParam(a.outHandle.x / duration, 1.0f + b.inHandle.x / duration, (time - a.time) / duration);
}

//=============================================================================
// Ease をカーブに写す
//=============================================================================
// ===== 式が切り替わる所 =====
std::span<const float> JointsOf(Ease::Type type) {
	switch (type) {
	case Ease::Type::OutBounce:
		return kOutBounceJoints;
	case Ease::Type::InBounce:
		return kInBounceJoints;
	case Ease::Type::InOutCubic:
		return kMiddleJoint;
	default:
		return {};
	}
}

// ===== 区間の端の傾きを、内側の3点から求める =====
// 3点を通る2次式を端まで延ばしたときの傾き。inward は内側へ進む1歩（終わりの端なら負の値）
// 外側を読むと、Bounce の跳ねた所で隣の区間の傾きが混ざる。端の点そのものを読むと、Elastic の t = 1 の値の飛びを拾う
float CalcEaseSlope(Ease::Type type, float edge, float inward) {
	const float first = Ease::Apply(edge + inward, type);
	const float second = Ease::Apply(edge + inward * 2.0f, type);
	const float third = Ease::Apply(edge + inward * 3.0f, type);
	return (-5.0f * first + 8.0f * second - 3.0f * third) / (inward * 2.0f);
}

// ===== start から end までを、Ease の値と傾きが両端で合うベジェにする =====
// 差が許容より大きければ、半分に分けてやり直す。keys の最後のキーが start の所にある状態で呼ぶ
void FitEase(std::vector<CurveKey>& keys, Ease::Type type, float start, float end, int depth) {
	const float duration = end - start;
	const float step = duration * kFitSlopeStep;
	const float startValue = Ease::Apply(start, type);
	const float endValue = Ease::Apply(end, type);
	const float startSlope = CalcEaseSlope(type, start, step);
	const float endSlope = CalcEaseSlope(type, end, -step);

	// --- ハンドルの時間を長さの 1/3 にすると、ベジェの時間が s に比例する（s と時間の割合が同じ）---
	const float third = duration / 3.0f;
	const float control1 = startValue + startSlope * third;
	const float control2 = endValue - endSlope * third;

	// --- Ease との差を測る ---
	float worst = 0.0f;
	for (int i = 1; i < kFitChecks; ++i) {
		const float s = static_cast<float>(i) / static_cast<float>(kFitChecks);
		worst = (std::max)(worst, std::abs(CubicBezier(startValue, control1, control2, endValue, s) - Ease::Apply(start + duration * s, type)));
	}
	if (worst > kFitTolerance && depth < kMaxFitDepth) {
		const float middle = start + duration * 0.5f;
		FitEase(keys, type, start, middle, depth + 1);
		FitEase(keys, type, middle, end, depth + 1);
		return;
	}

	// --- 区間を確定する ---
	keys.back().outHandle = {third, startSlope * third};
	CurveKey key;
	key.time = end;
	key.value = endValue;
	key.inHandle = {-third, -endSlope * third};
	key.handleMode = CurveHandleMode::Free; // 跳ねた所の折れ目を残す
	keys.push_back(key);
}
} // namespace

//=============================================================================
// 作る
//=============================================================================
Curve::Curve(std::vector<CurveKey> keys) : keys_(std::move(keys)) {
	std::ranges::transform(keys_, keys_.begin(), SanitizeKey);
	std::ranges::stable_sort(keys_, {}, &CurveKey::time); // 同じ時間のキーの順番を変えない
	ClampHandles();
}

Curve Curve::FromEase(Ease::Type type) {
	std::vector<CurveKey> keys(1);
	keys[0].value = Ease::Apply(0.0f, type);
	keys[0].handleMode = CurveHandleMode::Free;
	float start = 0.0f;
	for (const float joint : JointsOf(type)) {
		FitEase(keys, type, start, joint, 0);
		start = joint;
	}
	FitEase(keys, type, start, 1.0f, 0);
	return Curve(std::move(keys));
}

//=============================================================================
// 読む
//=============================================================================
float Curve::Evaluate(float time) const {
	if (keys_.empty()) {
		return 0.0f;
	}
	// 比べる前に NaN を除く（NaN はどの比較も false になり、下の探し方が使えない）
	if (std::isnan(time) || time < keys_.front().time) {
		return keys_.front().value;
	}
	// --- time より後の最初のキーを探す。a は time 以前の最後のキーなので、a.time <= time < b.time ---
	const auto next = std::ranges::upper_bound(keys_, time, {}, &CurveKey::time);
	if (next == keys_.end()) {
		return keys_.back().value;
	}
	const CurveKey& a = *(next - 1);
	const CurveKey& b = *next;
	switch (a.interp) {
	case CurveInterp::Constant:
		return a.value;
	case CurveInterp::Linear:
		return Ease::Lerp(a.value, b.value, (time - a.time) / (b.time - a.time));
	case CurveInterp::Bezier:
		break;
	}
	return CubicBezier(a.value, a.value + a.outHandle.y, b.value + b.inHandle.y, b.value, CalcBezierParam(a, b, time));
}

//=============================================================================
// 書き換える
//=============================================================================
// ===== キーを足す =====
int Curve::AddKey(float time, float value) {
	CurveKey key;
	key.time = ClampValue(time);
	key.value = ClampValue(value);
	const int index = FindInsertIndex(key.time);
	const int count = static_cast<int>(keys_.size());
	const bool hasPrevious = index > 0;
	const bool hasNext = index < count;

	// --- 傾きは前後のキーを結ぶ向き、ハンドルの長さは隣までの 1/3 ---
	float slope = 0.0f;
	if (hasPrevious && hasNext && keys_[index].time > keys_[index - 1].time) {
		slope = (keys_[index].value - keys_[index - 1].value) / (keys_[index].time - keys_[index - 1].time);
	}
	const float inLength = hasPrevious ? (key.time - keys_[index - 1].time) / 3.0f : 0.0f;
	const float outLength = hasNext ? (keys_[index].time - key.time) / 3.0f : 0.0f;
	key.inHandle = {-inLength, -inLength * slope};
	key.outHandle = {outLength, outLength * slope};

	// --- 隣のキーの、こちらを向くハンドルが長さ 0 なら伸ばす（カーブの端のキーのハンドルは 0 のままなので、つかめなくなる）---
	if (hasPrevious) {
		key.interp = keys_[index - 1].interp;
		if (keys_[index - 1].outHandle.x == 0.0f) {
			keys_[index - 1].outHandle.x = inLength;
		}
	}
	if (hasNext && keys_[index].inHandle.x == 0.0f) {
		keys_[index].inHandle.x = -outLength;
	}
	keys_.insert(keys_.begin() + index, key);
	ClampHandles();
	return index;
}

// ===== 線の上へキーを足す =====
int Curve::InsertKeyOnCurve(float time) {
	const float clampedTime = ClampValue(time);
	const int index = FindInsertIndex(clampedTime);
	// --- ちょうどその時間のキーがあれば、それを返す ---
	if (index > 0 && keys_[index - 1].time == clampedTime) {
		return index - 1;
	}
	// --- カーブの外なら、端の値でふつうに足す ---
	const int count = static_cast<int>(keys_.size());
	if (index == 0 || index == count) {
		return AddKey(clampedTime, Evaluate(clampedTime));
	}

	CurveKey& a = keys_[index - 1];
	CurveKey& b = keys_[index];
	CurveKey key;
	key.time = clampedTime;
	key.value = Evaluate(clampedTime);
	key.interp = a.interp;
	switch (a.interp) {
	case CurveInterp::Constant:
		break;
	case CurveInterp::Linear: {
		// 線の傾きのままハンドルを付ける（あとで Bezier にしても折れない）
		const float slope = (b.value - a.value) / (b.time - a.time);
		const float inLength = (clampedTime - a.time) / 3.0f;
		const float outLength = (b.time - clampedTime) / 3.0f;
		key.inHandle = {-inLength, -inLength * slope};
		key.outHandle = {outLength, outLength * slope};
		break;
	}
	case CurveInterp::Bezier: {
		// de Casteljau 法：制御点の間を s で分けることを3回くり返すと、同じ形のベジェ2本になる
		const float s = CalcBezierParam(a, b, clampedTime);
		const Vector2 p0 = {a.time, a.value};
		const Vector2 p1 = p0 + a.outHandle;
		const Vector2 p3 = {b.time, b.value};
		const Vector2 p2 = p3 + b.inHandle;
		const Vector2 p01 = p0 + (p1 - p0) * s;
		const Vector2 p12 = p1 + (p2 - p1) * s;
		const Vector2 p23 = p2 + (p3 - p2) * s;
		const Vector2 p012 = p01 + (p12 - p01) * s;
		const Vector2 p123 = p12 + (p23 - p12) * s;
		const Vector2 middle = p012 + (p123 - p012) * s;
		a.outHandle = p01 - p0;
		b.inHandle = p23 - p3;
		key.inHandle = p012 - middle;
		key.outHandle = p123 - middle;
		key.value = middle.y;
		break;
	}
	}
	keys_.insert(keys_.begin() + index, key);
	ClampHandles();
	return index;
}

// ===== キーを消す =====
void Curve::RemoveKey(int index) {
	if (IsValidIndex(index)) {
		keys_.erase(keys_.begin() + index);
	}
}

// ===== キーを置き換える =====
int Curve::SetKey(int index, const CurveKey& key) {
	if (!IsValidIndex(index)) {
		return index;
	}
	const CurveKey sanitized = SanitizeKey(key);
	keys_.erase(keys_.begin() + index);
	// equal_range：同じ時間のキーが並ぶ範囲。今の番号に一番近い場所へ戻すので、時間を変えなければ順番は変わらない
	const auto range = std::ranges::equal_range(keys_, sanitized.time, {}, &CurveKey::time);
	const int first = static_cast<int>(range.begin() - keys_.begin());
	const int last = static_cast<int>(range.end() - keys_.begin());
	const int newIndex = std::clamp(index, first, last);
	keys_.insert(keys_.begin() + newIndex, sanitized);
	ClampHandles();
	return newIndex;
}

// ===== ハンドルを動かす =====
void Curve::SetHandle(int index, CurveHandleSide side, const Vector2& handle) {
	if (!IsValidIndex(index)) {
		return;
	}
	const bool isOut = side == CurveHandleSide::Out;
	CurveKey& key = keys_[index];
	(isOut ? key.outHandle : key.inHandle) = handle;
	key = SanitizeKey(key);
	ClampHandles();

	// --- Aligned：反対のハンドルの時間の長さを変えず、傾きだけそろえる（時間と値は単位が違うので、向きより傾きで比べる）---
	if (key.handleMode == CurveHandleMode::Aligned) {
		const Vector2 moved = isOut ? key.outHandle : key.inHandle;
		Vector2& other = isOut ? key.inHandle : key.outHandle;
		if (moved.x != 0.0f) {
			other.y = ClampValue(other.x * (moved.y / moved.x));
		}
	}
}

// ===== 区間を Ease の形にする =====
void Curve::ApplyEaseToSegment(int index, Ease::Type type) {
	if (!IsValidIndex(index) || !IsValidIndex(index + 1)) {
		return;
	}
	const Vector2 start = {keys_[index].time, keys_[index].value};
	const Vector2 size = {keys_[index + 1].time - start.x, keys_[index + 1].value - start.y};
	if (size.x <= 0.0f) {
		return; // 同じ時間のキーの間には、形を入れられない
	}
	// --- 0〜1 の形を、この区間の大きさへ広げる ---
	std::vector<CurveKey> shape = FromEase(type).keys_;
	for (CurveKey& key : shape) {
		key.time = start.x + key.time * size.x;
		key.value = start.y + key.value * size.y;
		key.inHandle = {key.inHandle.x * size.x, key.inHandle.y * size.y};
		key.outHandle = {key.outHandle.x * size.x, key.outHandle.y * size.y};
	}
	// --- 両端のキーは残して、区間の内側を向くハンドルと補間だけ入れ替える（外側とは傾きが合わないので Free にする）---
	CurveKey& first = keys_[index];
	first.outHandle = shape.front().outHandle;
	first.interp = CurveInterp::Bezier;
	first.handleMode = CurveHandleMode::Free;
	CurveKey& last = keys_[index + 1];
	last.inHandle = shape.back().inHandle;
	last.handleMode = CurveHandleMode::Free;
	keys_.insert(keys_.begin() + index + 1, shape.begin() + 1, shape.end() - 1);
	ClampHandles();
}

// ===== 全体を Ease の形にする =====
void Curve::ApplyEaseToAll(Ease::Type type) {
	if (keys_.size() < 2) {
		*this = FromEase(type);
		return;
	}
	keys_.erase(keys_.begin() + 1, keys_.end() - 1); // 最初と最後のキーだけ残す
	ApplyEaseToSegment(0, type);
}

//=============================================================================
// 内部
//=============================================================================
int Curve::FindInsertIndex(float time) const { return static_cast<int>(std::ranges::upper_bound(keys_, time, {}, &CurveKey::time) - keys_.begin()); }

void Curve::ClampHandles() {
	for (size_t i = 0; i + 1 < keys_.size(); ++i) {
		const float duration = keys_[i + 1].time - keys_[i].time;
		keys_[i].outHandle = LimitHandle(keys_[i].outHandle, duration);
		keys_[i + 1].inHandle = LimitHandle(keys_[i + 1].inHandle, duration);
	}
}