#include "CurveSelfTest.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <random>
#include <set>
#include <string>
#include <vector>

#include <externals/magic_enum/magic_enum.hpp>
#include <externals/nlohmann/json.hpp>

#include "MyEngine/Curve/Curve.h"
#include "MyEngine/Curve/CurveManager.h"
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Diagnostics/MyAssert.h"

namespace {
constexpr float kExact = 0.00002f;       // 式で答えが決まる値の許容
constexpr float kEaseTolerance = 0.001f; // Ease::Apply との差の許容
constexpr int kEaseSamples = 2000;       // Ease と比べる点の数
constexpr int kRandomCurves = 2000;      // でたらめなカーブの数
constexpr int kRandomSamples = 64;       // でたらめなカーブ1本で確かめる時間の数
constexpr int kMaxErrorLogs = 10;        // 合わなかった項目をログに出す数（全部出すと読めない）
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInfinity = std::numeric_limits<float>::infinity();

/// <summary>
/// 確かめた数と、合わなかった数を数える
/// </summary>
struct Tester {
	int passed = 0;
	int failed = 0;

	void Check(bool isOk, const std::string& name) {
		if (isOk) {
			++passed;
			return;
		}
		if (failed < kMaxErrorLogs) {
			LogManager::Error(std::format("[curve] self test: {}", name));
		}
		++failed;
	}
	void Near(float actual, float expected, float tolerance, const std::string& name) {
		Check(std::abs(actual - expected) <= tolerance, std::format("{}: {} （正しい値 {}）", name, actual, expected));
	}
};

// キーを作る（ハンドルは省略できる）
CurveKey MakeKey(float time, float value, CurveInterp interp = CurveInterp::Bezier, Vector2 inHandle = {0.0f, 0.0f}, Vector2 outHandle = {0.0f, 0.0f}) {
	CurveKey key;
	key.time = time;
	key.value = value;
	key.interp = interp;
	key.inHandle = inHandle;
	key.outHandle = outHandle;
	return key;
}

// 2つのキーの1区間のベジェを作る（CSS の cubic-bezier(x1, y1, x2, y2) と同じ指定。時間も値も 0〜1）
Curve MakeCubicBezier(float x1, float y1, float x2, float y2) {
	return Curve({MakeKey(0.0f, 0.0f, CurveInterp::Bezier, {}, {x1, y1}), MakeKey(1.0f, 1.0f, CurveInterp::Bezier, {x2 - 1.0f, y2 - 1.0f}, {})});
}

//=============================================================================
// 確かめる項目
//=============================================================================
// ===== キーが0個・1個 =====
void TestFewKeys(Tester& t) {
	const Curve empty;
	t.Near(empty.Evaluate(0.5f), 0.0f, 0.0f, "キー0個は0");
	const Curve single({MakeKey(2.0f, 3.0f)});
	t.Near(single.Evaluate(-1.0f), 3.0f, 0.0f, "キー1個：前");
	t.Near(single.Evaluate(2.0f), 3.0f, 0.0f, "キー1個：ちょうど");
	t.Near(single.Evaluate(9.0f), 3.0f, 0.0f, "キー1個：後ろ");
}

// ===== カーブの端 =====
void TestEnds(Tester& t) {
	const Curve curve({MakeKey(1.0f, 10.0f, CurveInterp::Linear), MakeKey(3.0f, 20.0f, CurveInterp::Linear)});
	t.Near(curve.Evaluate(0.0f), 10.0f, 0.0f, "最初のキーより前");
	t.Near(curve.Evaluate(1.0f), 10.0f, 0.0f, "最初のキーちょうど");
	t.Near(curve.Evaluate(3.0f), 20.0f, 0.0f, "最後のキーちょうど");
	t.Near(curve.Evaluate(4.0f), 20.0f, 0.0f, "最後のキーより後");
}

// ===== 補間の種類 =====
void TestInterp(Tester& t) {
	const Curve constant({MakeKey(0.0f, 1.0f, CurveInterp::Constant), MakeKey(1.0f, 5.0f)});
	t.Near(constant.Evaluate(0.999f), 1.0f, 0.0f, "Constant：次のキーの手前は今の値");
	t.Near(constant.Evaluate(1.0f), 5.0f, 0.0f, "Constant：次のキーちょうどは次の値");
	const Curve linear({MakeKey(0.0f, 1.0f, CurveInterp::Linear), MakeKey(2.0f, 5.0f)});
	t.Near(linear.Evaluate(0.5f), 2.0f, kExact, "Linear：途中");
	// 補間は左のキーで決まる（右のキーの interp は見ない）
	const Curve mixed({MakeKey(0.0f, 0.0f, CurveInterp::Linear), MakeKey(1.0f, 1.0f, CurveInterp::Constant), MakeKey(2.0f, 3.0f)});
	t.Near(mixed.Evaluate(0.5f), 0.5f, kExact, "左のキーの補間：Linear");
	t.Near(mixed.Evaluate(1.5f), 1.0f, kExact, "左のキーの補間：Constant");
}

// ===== ベジェの代表的な値（別の計算で求めた値と比べる。CSS の ease・ease-in・ease-in-out）=====
void TestBezierValues(Tester& t) {
	const Curve ease = MakeCubicBezier(0.25f, 0.1f, 0.25f, 1.0f);
	t.Near(ease.Evaluate(0.25f), 0.4085106f, kExact, "ease 0.25");
	t.Near(ease.Evaluate(0.5f), 0.8024034f, kExact, "ease 0.5");
	const Curve easeIn = MakeCubicBezier(0.42f, 0.0f, 1.0f, 1.0f);
	t.Near(easeIn.Evaluate(0.5f), 0.3153568f, kExact, "ease-in 0.5");
	const Curve easeInOut = MakeCubicBezier(0.42f, 0.0f, 0.58f, 1.0f);
	t.Near(easeInOut.Evaluate(0.25f), 0.1291619f, kExact, "ease-in-out 0.25");
	t.Near(easeInOut.Evaluate(0.5f), 0.5f, kExact, "ease-in-out 0.5");
	// 時間が 0〜1 でない区間でも、同じ形になる
	const Curve scaled({MakeKey(10.0f, 100.0f, CurveInterp::Bezier, {}, {0.5f, 2.0f}), MakeKey(12.0f, 120.0f, CurveInterp::Bezier, {-1.5f, 0.0f}, {})});
	t.Near(scaled.Evaluate(11.0f), 100.0f + 20.0f * 0.8024034f, 0.001f, "広げた区間の ease 0.5");
}

// ===== 同じ時間のキー =====
void TestSameTime(Tester& t) {
	const Curve step({MakeKey(0.0f, 0.0f, CurveInterp::Linear), MakeKey(1.0f, 1.0f, CurveInterp::Linear), MakeKey(1.0f, 5.0f, CurveInterp::Linear), MakeKey(2.0f, 6.0f)});
	t.Near(step.Evaluate(0.999f), 0.999f, 0.001f, "段差の手前");
	t.Near(step.Evaluate(1.0f), 5.0f, 0.0f, "段差ちょうどは後ろのキー");
	t.Near(step.Evaluate(1.5f), 5.5f, kExact, "段差の後");
	const Curve allSame({MakeKey(1.0f, 2.0f), MakeKey(1.0f, 3.0f), MakeKey(1.0f, 4.0f)});
	t.Near(allSame.Evaluate(0.0f), 2.0f, 0.0f, "全部同じ時間：前は最初のキー");
	t.Near(allSame.Evaluate(1.0f), 4.0f, 0.0f, "全部同じ時間：ちょうどは最後のキー");
	t.Near(allSame.Evaluate(2.0f), 4.0f, 0.0f, "全部同じ時間：後ろ");
	const CurveKey handleKey = MakeKey(1.0f, 0.0f, CurveInterp::Bezier, {-5.0f, 1.0f}, {5.0f, 1.0f});
	const Curve handles({handleKey, handleKey});
	t.Check(handles.GetKeys()[0].outHandle.x == 0.0f && handles.GetKeys()[1].inHandle.x == 0.0f, "同じ時間のキーの間のハンドルは長さ 0");
}

// ===== 壊れた入力 =====
void TestBadInput(Tester& t) {
	const Curve curve({MakeKey(0.0f, 1.0f, CurveInterp::Linear), MakeKey(1.0f, 2.0f)});
	t.Near(curve.Evaluate(kNaN), 1.0f, 0.0f, "NaN の時間は最初の値");
	t.Near(curve.Evaluate(kInfinity), 2.0f, 0.0f, "+無限大の時間は最後の値");
	t.Near(curve.Evaluate(-kInfinity), 1.0f, 0.0f, "-無限大の時間は最初の値");

	const Curve broken({MakeKey(kNaN, kInfinity, CurveInterp::Bezier, {kNaN, kNaN}, {kInfinity, -kInfinity}), MakeKey(1.0f, -kInfinity), MakeKey(kInfinity, kNaN)});
	for (const CurveKey& key : broken.GetKeys()) {
		const bool isFinite =
		    std::isfinite(key.time) && std::isfinite(key.value) && std::isfinite(key.inHandle.x) && std::isfinite(key.inHandle.y) && std::isfinite(key.outHandle.x) && std::isfinite(key.outHandle.y);
		t.Check(isFinite, "NaN・無限大のキーは有限の値に直る");
	}
	t.Check(std::isfinite(broken.Evaluate(0.5f)), "直したカーブの値は有限");

	const Curve unsorted({MakeKey(2.0f, 2.0f), MakeKey(0.0f, 0.0f), MakeKey(1.0f, 1.0f)});
	t.Check(unsorted.GetKeys()[0].time == 0.0f && unsorted.GetKeys()[2].time == 2.0f, "ばらばらの順のキーは時間の順に並ぶ");

	const Curve huge({MakeKey(-1.0e30f, -1.0e30f, CurveInterp::Bezier, {}, {1.0e30f, 1.0e30f}), MakeKey(1.0e30f, 1.0e30f, CurveInterp::Bezier, {-1.0e30f, -1.0e30f}, {})});
	t.Check(std::isfinite(huge.Evaluate(0.0f)), "とても大きな値でも有限");

	Curve invalidIndex({MakeKey(0.0f, 1.0f)});
	const Curve copy = invalidIndex;
	invalidIndex.RemoveKey(-1);
	invalidIndex.RemoveKey(1);
	invalidIndex.SetHandle(5, CurveHandleSide::Out, {1.0f, 1.0f});
	invalidIndex.ApplyEaseToSegment(0, Ease::Type::OutCubic); // 次のキーが無い
	t.Check(invalidIndex == copy && invalidIndex.SetKey(3, MakeKey(0.0f, 0.0f)) == 3, "知らない番号は何もしない");
}

// ===== ハンドルの制限 =====
void TestHandleLimit(Tester& t) {
	Curve curve({MakeKey(0.0f, 0.0f), MakeKey(1.0f, 1.0f)});
	curve.SetHandle(0, CurveHandleSide::Out, {3.0f, 6.0f});
	t.Near(curve.GetKeys()[0].outHandle.x, 1.0f, 0.0f, "出のハンドルは区間の長さまで");
	t.Near(curve.GetKeys()[0].outHandle.y, 2.0f, kExact, "縮めても傾きは同じ");
	curve.SetHandle(0, CurveHandleSide::Out, {-1.0f, 1.0f});
	t.Near(curve.GetKeys()[0].outHandle.x, 0.0f, 0.0f, "出のハンドルは前の時間へ行かない");
	curve.SetHandle(1, CurveHandleSide::In, {1.0f, 1.0f});
	t.Near(curve.GetKeys()[1].inHandle.x, 0.0f, 0.0f, "入りのハンドルは後の時間へ行かない");
	// 区間が縮むと、はみ出たハンドルも縮む
	Curve shrink({MakeKey(0.0f, 0.0f, CurveInterp::Bezier, {}, {2.0f, 2.0f}), MakeKey(3.0f, 1.0f)});
	CurveKey moved = shrink.GetKeys()[1];
	moved.time = 1.0f;
	shrink.SetKey(1, moved);
	t.Near(shrink.GetKeys()[0].outHandle.x, 1.0f, 0.0f, "区間が縮むとハンドルも縮む");
	t.Near(shrink.GetKeys()[0].outHandle.y, 1.0f, kExact, "区間が縮んでも傾きは同じ");
}

// ===== 解き方 =====
void TestSolver(Tester& t) {
	// 接線が真上を向く（x1 = 1, x2 = 0）：s = 0.5 で傾きが 0 になる。Newton 法だけだと 0 で割る
	const Curve vertical = MakeCubicBezier(1.0f, 0.0f, 0.0f, 1.0f);
	t.Near(vertical.Evaluate(0.25f), 0.0297246f, kExact, "真上を向く接線 0.25");
	float previous = vertical.Evaluate(0.0f);
	bool isIncreasing = true;
	for (int i = 1; i <= kEaseSamples; ++i) {
		const float value = vertical.Evaluate(static_cast<float>(i) / static_cast<float>(kEaseSamples));
		isIncreasing = isIncreasing && value >= previous - kExact;
		previous = value;
	}
	t.Check(isIncreasing, "真上を向く接線でも値が増え続ける");

	// 値の制御点を時間の制御点と同じにすると、正しく解ければ Evaluate(t) = t になる
	std::mt19937 random(1234);
	std::uniform_real_distribution<float> unit(0.0f, 1.0f);
	float worst = 0.0f;
	for (int i = 0; i < kRandomCurves; ++i) {
		const float x1 = unit(random);
		const float x2 = unit(random);
		const Curve curve = MakeCubicBezier(x1, x1, x2, x2);
		for (int j = 0; j <= kRandomSamples; ++j) {
			const float time = static_cast<float>(j) / static_cast<float>(kRandomSamples);
			worst = (std::max)(worst, std::abs(curve.Evaluate(time) - time));
		}
	}
	t.Near(worst, 0.0f, kExact, "どのハンドルでも時間を解ける");
}

// ===== でたらめなカーブ =====
void TestRandom(Tester& t) {
	std::mt19937 random(5678);
	std::uniform_real_distribution<float> position(-10.0f, 10.0f);
	std::uniform_real_distribution<float> handle(-30.0f, 30.0f);
	std::uniform_int_distribution<int> keyCount(1, 6);
	std::uniform_int_distribution<int> interp(0, 2);
	bool isFinite = true;
	bool isHandleInside = true;
	for (int i = 0; i < kRandomCurves; ++i) {
		std::vector<CurveKey> keys(keyCount(random));
		for (CurveKey& key : keys) {
			key = MakeKey(position(random), position(random), static_cast<CurveInterp>(interp(random)), {handle(random), handle(random)}, {handle(random), handle(random)});
		}
		const Curve curve(std::move(keys));
		const std::vector<CurveKey>& sorted = curve.GetKeys();
		for (size_t k = 0; k + 1 < sorted.size(); ++k) {
			const float duration = sorted[k + 1].time - sorted[k].time;
			isHandleInside = isHandleInside && sorted[k].outHandle.x >= 0.0f && sorted[k].outHandle.x <= duration * 1.0001f;
			isHandleInside = isHandleInside && sorted[k + 1].inHandle.x <= 0.0f && -sorted[k + 1].inHandle.x <= duration * 1.0001f;
		}
		for (int j = 0; j <= kRandomSamples; ++j) {
			isFinite = isFinite && std::isfinite(curve.Evaluate(-12.0f + 24.0f * static_cast<float>(j) / static_cast<float>(kRandomSamples)));
		}
	}
	t.Check(isFinite, "でたらめなカーブの値は有限");
	t.Check(isHandleInside, "でたらめなカーブのハンドルは区間の中");
}

// ===== Ease と同じ形 =====
void TestEase(Tester& t) {
	size_t maxKeys = 0;
	for (const Ease::Type type : magic_enum::enum_values<Ease::Type>()) {
		const Curve curve = Curve::FromEase(type);
		float worst = 0.0f;
		for (int i = 0; i <= kEaseSamples; ++i) {
			const float time = static_cast<float>(i) / static_cast<float>(kEaseSamples);
			worst = (std::max)(worst, std::abs(curve.Evaluate(time) - Ease::Apply(time, type)));
		}
		t.Near(worst, 0.0f, kEaseTolerance, std::format("Ease {} との差", magic_enum::enum_name(type)));
		maxKeys = (std::max)(maxKeys, curve.GetKeys().size());
	}
	LogManager::Log(std::format("[curve] 1つの Ease に使ったキーの最大数：{}", maxKeys));

	// 区間だけを Ease にする（隣の区間と、両端のキーは変わらない）
	Curve curve({MakeKey(0.0f, 0.0f, CurveInterp::Linear), MakeKey(1.0f, 10.0f, CurveInterp::Linear), MakeKey(3.0f, 30.0f, CurveInterp::Linear), MakeKey(4.0f, 40.0f)});
	curve.ApplyEaseToSegment(1, Ease::Type::OutBounce);
	t.Near(curve.Evaluate(0.5f), 5.0f, kExact, "区間の Ease：前の区間は変わらない");
	t.Near(curve.Evaluate(3.5f), 35.0f, kExact, "区間の Ease：後ろの区間は変わらない");
	t.Near(curve.Evaluate(1.0f), 10.0f, kExact, "区間の Ease：始まりのキー");
	t.Near(curve.Evaluate(3.0f), 30.0f, kExact, "区間の Ease：終わりのキー");
	t.Near(curve.Evaluate(2.0f), 10.0f + 20.0f * Ease::Apply(0.5f, Ease::Type::OutBounce), 0.03f, "区間の Ease：途中");

	Curve whole({MakeKey(2.0f, 1.0f), MakeKey(3.0f, 9.0f), MakeKey(4.0f, 3.0f)});
	whole.ApplyEaseToAll(Ease::Type::InQuad);
	t.Near(whole.Evaluate(3.0f), 1.0f + 2.0f * 0.25f, 0.001f, "全体の Ease");
	Curve none;
	none.ApplyEaseToAll(Ease::Type::Linear);
	t.Check(none.GetKeys().size() == 2, "キーが無いカーブの全体の Ease は 0〜1 で作る");
}

// ===== キーの足し引き =====
void TestEdit(Tester& t) {
	// 線の上へ足しても形が変わらない
	for (const CurveInterp interp : magic_enum::enum_values<CurveInterp>()) {
		Curve curve({MakeKey(0.0f, 0.0f, interp, {}, {0.8f, 0.0f}), MakeKey(1.0f, 1.0f, interp, {-0.1f, 0.0f}, {})});
		const Curve before = curve;
		const int index = curve.InsertKeyOnCurve(0.3f);
		t.Check(index == 1 && curve.GetKeys().size() == 3, "線の上へ足す");
		float worst = 0.0f;
		for (int i = 0; i <= kRandomSamples; ++i) {
			const float time = static_cast<float>(i) / static_cast<float>(kRandomSamples);
			worst = (std::max)(worst, std::abs(curve.Evaluate(time) - before.Evaluate(time)));
		}
		t.Near(worst, 0.0f, 0.001f, std::format("線の上へ足しても形が同じ（{}）", magic_enum::enum_name(interp)));
		t.Check(curve.InsertKeyOnCurve(0.3f) == index && curve.GetKeys().size() == 3, "同じ時間のキーがあれば足さない");
	}
	Curve outside({MakeKey(1.0f, 2.0f)});
	t.Check(outside.InsertKeyOnCurve(0.0f) == 0 && outside.InsertKeyOnCurve(3.0f) == 2, "カーブの外へ足す");

	// AddKey：端へ足すと、つかめる長さのハンドルが付く
	Curve added({MakeKey(0.0f, 0.0f)});
	const int addedIndex = added.AddKey(3.0f, 1.0f);
	t.Check(addedIndex == 1 && added.GetKeys()[0].outHandle.x > 0.0f && added.GetKeys()[1].inHandle.x < 0.0f, "端へ足すとハンドルが付く");

	// 追い越すと並びが変わり、番号が返る
	Curve order({MakeKey(0.0f, 0.0f), MakeKey(1.0f, 1.0f), MakeKey(2.0f, 2.0f)});
	CurveKey moved = order.GetKeys()[0];
	moved.time = 1.5f;
	t.Check(order.SetKey(0, moved) == 1, "隣を追い越すと番号が変わる");
	t.Near(order.GetKeys()[1].time, 1.5f, 0.0f, "追い越した場所に入る");

	// 同じ時間のキーの順番は、値を変えただけでは入れ替わらない
	Curve same({MakeKey(1.0f, 1.0f), MakeKey(1.0f, 2.0f)});
	CurveKey changed = same.GetKeys()[0];
	changed.value = 7.0f;
	t.Check(same.SetKey(0, changed) == 0 && same.GetKeys()[0].value == 7.0f, "同じ時間のキーは順番が変わらない");

	// Aligned は反対のハンドルの傾きをそろえる。Free はそろえない
	Curve aligned({MakeKey(0.0f, 0.0f), MakeKey(1.0f, 1.0f, CurveInterp::Bezier, {-0.5f, 0.0f}, {0.25f, 0.0f}), MakeKey(2.0f, 2.0f)});
	aligned.SetHandle(1, CurveHandleSide::Out, {0.5f, 1.0f});
	t.Near(aligned.GetKeys()[1].inHandle.x, -0.5f, 0.0f, "Aligned：反対の時間の長さはそのまま");
	t.Near(aligned.GetKeys()[1].inHandle.y, -1.0f, kExact, "Aligned：反対の傾きがそろう");
	CurveKey freeKey = aligned.GetKeys()[1];
	freeKey.handleMode = CurveHandleMode::Free;
	aligned.SetKey(1, freeKey);
	aligned.SetHandle(1, CurveHandleSide::Out, {0.5f, 0.0f});
	t.Near(aligned.GetKeys()[1].inHandle.y, -1.0f, kExact, "Free：反対は動かない");

	// キーを消す
	Curve removal({MakeKey(0.0f, 0.0f), MakeKey(1.0f, 1.0f)});
	removal.RemoveKey(0);
	removal.RemoveKey(0);
	t.Near(removal.Evaluate(1.0f), 0.0f, 0.0f, "全部消すとキー0個のカーブ");
}

// ===== 保存の形 =====
void TestJson(Tester& t) {
	// --- 書いて読むと同じカーブになる（文字にしてから読み直す）---
	std::vector<CurveKey> keys = Curve::FromEase(Ease::Type::OutBack).GetKeys();
	keys.push_back(MakeKey(2.0f, 0.1f, CurveInterp::Constant));
	keys.push_back(MakeKey(3.0f, 0.2f, CurveInterp::Linear));
	const Curve curve(std::move(keys));
	std::set<std::string> warnings;
	const Curve loaded = CurveManager::FromJson(SceneJson::parse(CurveManager::ToJson(curve).dump()), warnings);
	t.Check(loaded == curve && warnings.empty(), "書いて読むと同じカーブになる");
	const Curve emptyLoaded = CurveManager::FromJson(SceneJson::parse(CurveManager::ToJson(Curve{}).dump()), warnings);
	t.Check(emptyLoaded.GetKeys().empty() && warnings.empty(), "キー0個も書いて読める");

	// --- 壊れた形でも止まらない ---
	std::set<std::string> broken;
	t.Check(CurveManager::FromJson(SceneJson::parse("{}"), broken).GetKeys().empty() && !broken.empty(), "keys が無いファイル");
	broken.clear();
	t.Check(CurveManager::FromJson(SceneJson::parse(R"({"keys": 5})"), broken).GetKeys().empty() && !broken.empty(), "keys が配列でないファイル");
	broken.clear();
	const Curve partial = CurveManager::FromJson(SceneJson::parse(R"({"keys": [1, "a", {"time": "x", "value": 2}, {"time": 1, "interp": "Nope", "inHandle": [1]}]})"), broken);
	t.Check(partial.GetKeys().size() == 2 && !broken.empty(), "壊れた項目は既定の値のまま読む");
	t.Check(std::isfinite(partial.Evaluate(0.5f)), "壊れた項目を読んだカーブの値は有限");
	broken.clear();
	const Curve reversed = CurveManager::FromJson(SceneJson::parse(R"({"keys": [{"time": 2, "value": 2}, {"time": 1, "value": 1}]})"), broken);
	t.Check(reversed.GetKeys()[0].time == 1.0f, "ファイルの順が逆でも時間の順に直る");
}
} // namespace

//=============================================================================
// 確かめる
//=============================================================================
void CurveSelfTest::Run() {
	Tester tester;
	TestFewKeys(tester);
	TestEnds(tester);
	TestInterp(tester);
	TestBezierValues(tester);
	TestSameTime(tester);
	TestBadInput(tester);
	TestHandleLimit(tester);
	TestSolver(tester);
	TestRandom(tester);
	TestEase(tester);
	TestEdit(tester);
	TestJson(tester);
	MY_ASSERT_MSG(tester.failed == 0, std::format("カーブの計算が合いません（{} 項目。ログを見てください）", tester.failed));
	LogManager::Log(std::format("[curve] self test OK ({} / {})", tester.passed, tester.passed + tester.failed));
}