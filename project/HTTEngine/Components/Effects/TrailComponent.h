#pragma once
#include <cstdint>
#include <type_traits>

#include "HTTEngine/Math/Vector4.h"

// 軌跡（通った跡・描いた線を、帯にして描く）。Entityに付けるComponent（カテゴリ：Rendering3D）
// 点の列は TrailSystem が持つ。Component に可変長の配列は入れられない
// ここは「どう描くか」の設定だけ。AudioSource と鳴っている音の関係と同じ
// 持ち主が消えた後も残して描く（After Owner が Shrink・Fade）ときは、TrailSystem がこの設定を丸ごと写して持つ（なので trivially copyable のままにする）

/// <summary>
/// 帯の向き
/// </summary>
enum class TrailAlignment {
	View,   // カメラの方を向く（動いた跡・剣の残像）
	Ground, // 地面に寝かせる（帯の面が真上を向く。上から見る線・地面に残るインク）
};

/// <summary>
/// 持ち主（Entity かこの Component）が消えた後の、尾の消え方
/// <para>離れた尾は TrailSystem が設定を写して持つ（ゲームからは触れない）。どれも新しい点は足さない</para>
/// </summary>
enum class TrailAfterOwner {
	Discard, // 消えたフレームに捨てる
	Shrink,  // 点が古い方から寿命で消える（尾が縮む。Life Time 0 の尾は消えないので残さない）
	Fade,    // 形はそのままで、Fade Seconds かけて尾全体を薄くして消す（点は古くしない。Life Time 0 の尾も消える）
};

/// <summary>
/// 重ね方
/// </summary>
enum class TrailBlend {
	Normal, // 普通（透けるのはテクスチャと色のアルファの分）
	Add,    // 足す（光る線。重なるほど明るい）
};


/// <summary>
/// 軌跡の設定
/// </summary>
struct TrailComponent {
	bool enabled = true;                             // false の間は描かない（点は消さずに残す）
	bool emitting = true;                            // true：Entityが動いたら自動で点を足す（動いた跡）／false：TrailSystem::AddPoint で足す（描いた線）
	float minVertexDistance = 0.1f;                  // 自動で足すとき、この距離動くたびに1点足す
	float lifeTime = 0.5f;                           // 点が消えるまでの秒（0 で消えない。Clear か Entityを消すまで残る）
	TrailAfterOwner afterOwner = TrailAfterOwner::Shrink; // 持ち主（Entity かこの Component）が消えた後の消え方
	float fadeSeconds = 0.3f;                        // After Owner が Fade のとき、薄くしきるまでの秒（Time::GetDeltaTime で数える。0 なら Discard と同じ）
	int maxPoints = 256;                             // 点の上限（超えたら古い方から消す）
	float width = 0.3f;                              // 太さ（新しい方の端）
	float endWidthRate = 0.0f;                       // 古い方の端の太さ（width に掛ける。1 でずっと同じ太さ、0 で先が細くなる）
	TrailAlignment alignment = TrailAlignment::View; // 帯の向き
	Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};        // 色（RGBA。0〜1）
	float intensity = 1.0f;                          // 明るさの倍率（色はそのまま。1 より大きいと HDR の明るさになり、ブルームで光る）
	uint32_t textureHandle = 0;                      // 0 なら白（色だけ）。U が古い方 0 → 新しい方 1、V が帯の幅の向き
	TrailBlend blend = TrailBlend::Normal;           // 重ね方
};

static_assert(std::is_trivially_copyable_v<TrailComponent>);