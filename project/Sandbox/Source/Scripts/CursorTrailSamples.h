#pragma once
#include "MyEngine/ECS/GameComponent.h"
#include "MyEngine/ECS/Script.h"

//=============================================================================
// 照準の UI と OS のカーソル（Sandbox のテスト用）
//=============================================================================

/// <summary>
/// 画面に重ねる UI（Anchor は Center）を、マウスか右スティックで動かす照準にする（Sandbox のテスト用）
/// <para>Start で OS のカーソルを隠す（ゲーム画面の上だけ）。C キーで出す・隠すを切り替える</para>
/// <para>マウスは画面の位置（端が ±1）→ UI のピクセル（GameScreen::NdcToPixel）。右スティックはピクセルの量 → 画面の位置の量（GameScreen::PixelToNdc）</para>
/// </summary>
class AimCursorSample : public Script {
public:
	void Start() override;
	void Update() override;
	void Fields(ComponentUI& ui) override;

private:
	// ===== Inspector で変える値 =====
	float padSpeed_ = 1200.0f; // 右スティックで照準を動かす速さ（ピクセル／秒。倒しきったとき）

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
/// <para>弾は Flight Time 秒飛んだら、End のやり方でいなくなる。Fade Out After Owner を変えて、尾の残り方を見比べる</para>
/// </summary>
struct TrailFadeShooter {
	float interval = 0.8f;                        // 撃つ間隔（秒）
	float speed = 12.0f;                          // 弾の速さ（m/s）
	float flightTime = 0.6f;                      // 弾が飛ぶ秒（過ぎたら End のやり方でいなくなる）
	TrailFadeEnd end = TrailFadeEnd::DestroyEntity; // 弾がいなくなる形
	bool fadeOutAfterOwner = true;                // 弾の Trail の Fade Out After Owner（true：消えても尾が寿命で消えきるまで残る）
	float trailLifeTime = 0.6f;                   // 弾の Trail の Life Time（点が消えるまでの秒）
	Vector4 trailColor = {0.4f, 0.8f, 1.0f, 1.0f}; // 弾の Trail の色（RGBA。0〜1）
	float timer = 0.0f;                           // 次に撃つまでの秒（実行中だけ）
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
