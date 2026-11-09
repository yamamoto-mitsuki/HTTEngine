#pragma once
#include "HTTEngine/Components/Effects/TrailComponent.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/ECS/Script.h"

//=============================================================================
// 照準の UI と OS のカーソル（Sandbox のテスト用）
//=============================================================================

/// <summary>
/// 画面に重ねる UI（Anchor は Center）を、マウスか右スティックで動かす照準にする（Sandbox のテスト用）
/// <para>Start で OS のカーソルを隠し（ゲーム画面の上だけ）、Confine On Start ならゲーム画面に閉じ込める。C キーで隠す、L キーで閉じ込めるを切り替える</para>
/// <para>閉じ込めている間：マウスの動いた量（数えた数）× Pixels Per Count ＝ピクセルの量 → 画面の位置の量（GameScreen::PixelToNdc）を足す（画面の端でも動く）</para>
/// <para>閉じ込めていない間：マウスは画面の位置（端が ±1）。右スティックはいつもピクセルの量 → 画面の位置の量</para>
/// </summary>
class AimCursorSample : public Script {
public:
	void Start() override;
	void Update() override;
	void Fields(ComponentUI& ui) override;

private:
	// ===== Inspector で変える値 =====
	float padSpeed_ = 1200.0f;    // 右スティックで照準を動かす速さ（ピクセル／秒。倒しきったとき）
	bool confineOnStart_ = true;  // Start で OS のカーソルをゲーム画面に閉じ込める（照準をマウスの動いた量で動かす）
	float pixelsPerCount_ = 1.0f; // 閉じ込めている間、マウスが1つ数えたら照準を何ピクセル動かすか（感度）

	// ===== 実行中だけの値 =====
	Vector2 screenPosition_ = {0.0f, 0.0f}; // 照準の画面の位置（真ん中 0、右と上が＋、端が ±1）
};


//=============================================================================
// 持ち主が消えた後の光の尾（Sandbox のテスト用）
//=============================================================================

/// <summary>
/// 弾がいなくなる形（光の尾の残り方を見比べる）
/// </summary>
enum class TrailFadeEnd {
	DestroyEntity, // Entity ごと消す
	RemoveTrail,   // Trail だけ外す（球は残って飛び続け、同じだけ飛んだら消える）
};

/// <summary>
/// 決まった間隔で、光の尾（Trail）を付けた弾を撃つ（Sandbox のテスト用。Entity の前 +Z へ）
/// <para>弾は Flight Time 秒飛んだら、End のやり方でいなくなる。After Owner を変えて、尾の残り方（捨てる・縮む・薄くする）を見比べる</para>
/// </summary>
struct TrailFadeShooter {
	float interval = 0.8f;                                // 撃つ間隔（秒）
	float speed = 12.0f;                                  // 弾の速さ（m/s）
	float flightTime = 0.6f;                              // 弾が飛ぶ秒（過ぎたら End のやり方でいなくなる）
	TrailFadeEnd end = TrailFadeEnd::DestroyEntity;       // 弾がいなくなる形
	TrailAfterOwner afterOwner = TrailAfterOwner::Fade;   // 弾の Trail の After Owner（弾がいなくなった後の尾の消え方）
	float fadeSeconds = 0.3f;                             // 弾の Trail の Fade Seconds（After Owner が Fade のとき、薄くしきるまでの秒）
	float trailLifeTime = 0.6f;                           // 弾の Trail の Life Time（点が消えるまでの秒）
	TrailBlend blend = TrailBlend::Add;                   // 弾の Trail の Blend（Add と Normal の両方で薄くなるのを見る）
	Vector4 trailColor = {0.4f, 0.8f, 1.0f, 1.0f};        // 弾の Trail の色（RGBA。0〜1）
	float timer = 0.0f;                                   // 次に撃つまでの秒（実行中だけ）
};

/// <summary>
/// TrailFadeShooter が撃った弾（まっすぐ飛び、Flight Time が過ぎたら End のやり方でいなくなる）
/// </summary>
struct TrailFadeBullet {
	Vector3 velocity = {0.0f, 0.0f, 12.0f};         // 速さ（m/s。ワールド座標）
	float flightTime = 0.6f;                        // 1回に飛ぶ秒（RemoveTrail は、Trail を外した後にもう1回飛ぶ）
	float timeLeft = 0.6f;                          // いなくなるまでの秒
	TrailFadeEnd end = TrailFadeEnd::DestroyEntity; // いなくなる形
	bool isTrailRemoved = false;                    // Trail を外した後か（RemoveTrail のとき。外した後は、同じだけ飛んだら消える）
};
