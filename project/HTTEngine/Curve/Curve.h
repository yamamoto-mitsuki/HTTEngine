#pragma once
#include <vector>

#include "HTTEngine/Math/Easing.h"
#include "HTTEngine/Math/Vector2.h"

/// <summary>
/// キーから次のキーまでの補間の種類
/// </summary>
enum class CurveInterp {
	Constant, // 次のキーの時間まで、今の値のまま
	Linear,   // 次のキーまで直線
	Bezier,   // 3次ベジェ（このキーの出のハンドルと、次のキーの入りのハンドルを使う）
};

/// <summary>
/// 1つのキーの、入りと出のハンドルの関係
/// </summary>
enum class CurveHandleMode {
	Aligned, // 片方を動かすと、もう片方も同じ傾きになる（なめらかにつながる）
	Free,    // 別々に動く（折れ目を作れる）
};

/// <summary>
/// キーのどちら側のハンドルか
/// </summary>
enum class CurveHandleSide {
	In,  // 入り（キーより前の時間）
	Out, // 出（キーより後の時間）
};

/// <summary>
/// カーブのキー1つ
/// </summary>
struct CurveKey {
	float time = 0.0f;                                     // 時間
	float value = 0.0f;                                    // 値
	Vector2 inHandle = {0.0f, 0.0f};                       // 入りのハンドル。キーからの差（x：時間、y：値）。x は 0 以下
	Vector2 outHandle = {0.0f, 0.0f};                      // 出のハンドル。キーからの差（x：時間、y：値）。x は 0 以上
	CurveInterp interp = CurveInterp::Bezier;              // このキーから次のキーまでの補間（最後のキーでは使わない）
	CurveHandleMode handleMode = CurveHandleMode::Aligned; // 入りと出のハンドルの関係

	bool operator==(const CurveKey&) const = default;
};


/// <summary>
/// 時間に対して値がどう変わるかを表すカーブ（キーの間を 3次ベジェ・直線・一定でつなぐ）
/// <para>線に沿って進む PathCurve（3D の道）とは別物。こちらは「時間 → 値」の関数</para>
/// <para>次の3つをいつも守る（書き換えは全部関数を通すので、外から壊せない）</para>
/// <para>・キーは時間の順に並ぶ（同じ時間のキーがあってもよい＝値の段差）</para>
/// <para>・時間・値・ハンドルは有限で、±kMaxValue の中にある</para>
/// <para>・ハンドルの時間の長さは、隣のキーまでの長さを超えない（これで時間が逆戻りしない）</para>
/// </summary>
class Curve {
public:
	static constexpr float kMaxValue = 10000000.0f; // 時間と値の絶対値の上限（足し算で無限大にならず、float の精度が残る大きさ）

	// ===== 作る =====
	Curve() = default;
	// キーの並びから作る（ファイルから読むときなど。並び・NaN・ハンドルの範囲はここで直す）
	explicit Curve(std::vector<CurveKey> keys);
	/// <summary>
	/// Ease::Apply と同じ形のカーブを作る（時間 0〜1、値 0〜1）
	/// <para>1つのベジェで表せない形は、許容の差に収まるまでキーを増やす</para>
	/// </summary>
	static Curve FromEase(Ease::Type type);

	// ===== 読む =====
	/// <summary>
	/// 時間の値を求める。キーが無ければ 0
	/// <para>最初のキーより前は最初の値、最後のキーより後は最後の値。同じ時間のキーでは、ちょうどその時間は後ろのキーの値</para>
	/// <para>NaN を渡すと最初の値を返す</para>
	/// </summary>
	float Evaluate(float time) const;
	const std::vector<CurveKey>& GetKeys() const { return keys_; }

	// ===== 書き換える =====
	/// <summary>
	/// キーを足す。ハンドルは前後のキーを結ぶ傾きで作る
	/// </summary>
	/// <returns>足したキーの番号</returns>
	int AddKey(float time, float value);
	/// <summary>
	/// カーブの形を変えずに、線の上へキーを足す（ベジェは2つに分ける）。ちょうどその時間のキーがあれば、足さない
	/// </summary>
	/// <returns>足したキー（または、もとからあったキー）の番号</returns>
	int InsertKeyOnCurve(float time);
	// キーを消す（知らない番号なら何もしない）
	void RemoveKey(int index);
	/// <summary>
	/// キーの中身を置き換える。時間が変わって隣を追い越したら、並びも変わる（同じ時間の間では順番を変えない）
	/// </summary>
	/// <returns>置き換えた後のキーの番号（知らない番号なら index のまま）</returns>
	int SetKey(int index, const CurveKey& key);
	// ハンドルを動かす（Aligned なら反対のハンドルの傾きもそろえる）。handle はキーからの差
	void SetHandle(int index, CurveHandleSide side, const Vector2& handle);
	// index のキーから次のキーまでを、Ease の形にする（両端のキーの時間と値は変えない。間にキーが増えることがある）
	void ApplyEaseToSegment(int index, Ease::Type type);
	// 全体を Ease の形にする（最初と最後のキーの間。キーが2つより少なければ、時間 0〜1・値 0〜1 で作る）
	void ApplyEaseToAll(Ease::Type type);

	bool operator==(const Curve&) const = default;

private:
	bool IsValidIndex(int index) const { return index >= 0 && index < static_cast<int>(keys_.size()); }
	// time のキーを入れる場所の番号（同じ時間のキーの後ろ）
	int FindInsertIndex(float time) const;
	// 全部のキーのハンドルを、隣のキーまでの長さに収める
	void ClampHandles();

	std::vector<CurveKey> keys_; // 時間の順に並ぶ
};