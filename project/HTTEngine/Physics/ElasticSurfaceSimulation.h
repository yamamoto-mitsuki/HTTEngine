#pragma once
#include <vector>

// 弾む面（トランポリンの膜みたいな）の中身の計算。描画もEntityも知らない
// ElasticSurfaceSystem がEntityごとに1つ持ち、PhysicsSystemの小さな1歩ごとに、Stepを呼ぶ


/// <summary>
/// 膜の張り方（ElasticSurfaceComponentのInspectorで触る値）
/// </summary>
struct ElasticSurfaceSettings {
	float width = 20.0f;       // 横幅（m）。両端が支柱
	int pointCount = 65;       // 点の数（両端の支柱を含む）。多いほど細かく上がるので
	float tension = 30000.0f;  // 張りの強さ（N）。強いほど沈みにくく、速く跳ね返す
	float massPerMeter = 1.5f; // 1mあたりの膜の重さ（kg）。軽いほど、乗ったものの勢いがそのまま跳ね返りになる
	float damping = 1.0f;      // 揺れが収まる速さ。大きいほど跳ね返りが弱くなる
	float smoothing = 0.3f;    //細かいギザギザを消す強さ。大きい曲がりにはほとんど効かない
	// --- 端の硬さ（真ん中はよく弾み、端はあまり弾まないようにする。どちらも0で今まで通り）---
	float edgeStiffness = 0.3f; // 端の点を平らな位置へ引き戻す強さ（膜1mが1m沈んだときの力。N/m²）。大きいほど端が沈みにくい
	float edgeDamping = 0.0f;   // 端の点の揺れを止める強さ（damping に足す）。大きいほど端で跳ね返りが弱くなる
	float edgeFalloff = 2.0f;   // 端からどれだけ内側まで効かせるか。効き方は (真ん中からの距離 ÷ 幅の半分)^この値。大きいほど端の近くだけ
};

/// <summary>
/// 物とのぶつかり方
/// </summary>
struct ElasticContactSettings {
	float stiffness = 50000.0f; // めり込んだ深さ1mあたりの押し返す力（N/m）。膜より十分硬くして、物が膜の上に乗るようにする
	float damping = 50.0f;      // 近づく速さ1m/sあたりの押し返す力（N・s/m）。止まっている物が小刻みに震えないように
};


/// <summary>
/// 弾む面の中身（横1列のピンと張った膜を点の並びで表した物）
/// <para>座標は面から見た値。x は真ん中が0（左端が -width/2、右端が +width/2）、高さは平らなときが0（上が+）</para>
/// <para>両端の点は支柱（動かない）。物が乗ると、そこを1番下にした V の字に沈み、張りで戻って物を跳ね返す</para>
/// </summary>
class ElasticSurfaceSimulation {
public:
	// 設定を入れる。幅か点の数が変わったときだけ、点を作り直す（平らに戻る）
	void SetSettings(const ElasticSurfaceSettings& settings);
	// dt秒だけ進める（小さな1歩。1歩が長いと暴れるので、PhysicsSystemが細かく分けて呼ぶ）
	void Step(float dt);
	// 平らに戻す（揺れも止める）
	void Flatten();

	// ===== 物とのぶつかり =====
	/// <summary>
	/// 物の点 (x, y) が面より下に入っていたら、押し返す力の大きさ（N。面に垂直な向き）を返す。入っていなければ0
	/// <para>同じ大きさの力を逆向きに面へ足す（反作用）。面が沈むのは、この力のせい</para>
	/// </summary>
	/// <param name="velocityY">その点の上下の速さ（m/s。上が+）</param>
	/// <param name="radius">面を押す範囲の半径（m）</param>
	/// <param name="maxPenetration">これより深く入っている点は押さない（面の下をくぐった物を、真上へ打ち上げないように）</param>
	float PushBack(float x, float y, float velocityY, float radius, float maxPenetration, const ElasticContactSettings& contact);
	// x の周り（半径 radius）に力を足す（N。下向きは負）。次の Step で使って消える
	void AddForce(float x, float force, float radius);
	// x の周り（半径 radius）を一瞬で叩く（N・s。下向きは負）。その場で速さが変わる（ヒップドロップなど）
	void AddImpulse(float x, float impulse, float radius);

	// ===== 読む =====
	float GetHeight(float x) const;   // x の高さ（点と点の間はまっすぐつなぐ）
	float GetVelocity(float x) const; // x の上下の速さ（m/s。上が+）
	float GetSlope(float x) const;    // x の傾き（右へ1m進むと何m上がるか）
	bool IsInside(float x) const;     // x が面の上か（支柱の外なら false）

	// ===== 描画用 =====
	size_t GetPointCount() const { return heights_.size(); }
	float GetPointX(size_t index) const; // index 番目の点の x
	float GetPointHeight(size_t index) const { return heights_[index]; }
	float GetSpacing() const { return spacing_; }
	const ElasticSurfaceSettings& GetSettings() const { return settings_; }

private:
	// xを「何番目の点か」の小数に直す（左端が0、右端が 点の数-1）
	float ToIndex(float x) const;
	// x が何番目の点と次の点の間か（左の点の番号を返す）。t にはその間のどのあたりか（0〜1）が入る
	size_t FindSegment(float x, float* t) const;
	// x の周り（半径 radius）の点に、合計が1になる重みを配って function(番号, 重み) を呼ぶ
	template<class Function> 
	void ForEachWeighted(float x, float radius, Function function) const;

	ElasticSurfaceSettings settings_;
	float spacing_ = 0.0f;             // 点と点の間隔（m）
	std::vector<float> heights_;       // 点の高さ
	std::vector<float> velocities_;    // 点の上下の速さ
	std::vector<float> forces_;        // 次の Step で使う力（使ったら0に戻す）
	std::vector<float> accelerations_; // Step の中だけで使う（毎回作らないように持っておく）
	std::vector<float> edgeWeights_;   // 点ごとの端の効き方（真ん中が0・支柱が1）。SetSettings で作り直す（Step で毎回 pow を計算しないように）
};