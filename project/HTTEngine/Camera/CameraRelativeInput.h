#pragma once
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector2.h"
#include "HTTEngine/Math/Vector3.h"


/// <summary>
/// 移動の入力（2D）を、画面に写っているカメラから見た地面の向き（ワールド）へ直す。動かすキャラ（プレイヤーの Script など）が1つずつ持つ
/// <para>上（W・スティックの上）＝画面の奥、右（D・スティックの右）＝画面の右。カメラが回り込んでも、いつも画面に合った向きに動く</para>
/// <para>押している間にカメラが切り替わったら、離すまで切り替わる前の向きを使い続ける（切り替えた瞬間に、進む向きが急に変わらないように）</para>
/// <para>カメラが回る（同じカメラのまま向きが変わる）間は、毎フレーム今の向きを使う。使い続けるのは、写すカメラ（Camera Component の Entity）が変わったときだけ</para>
/// </summary>
class CameraRelativeInput {
public:
	/// <summary>
	/// 2D の入力 → ワールドの地面の向き（Y は 0。長さは入力の長さのまま。スティックを半分倒せば半分）
	/// <para>毎フレーム1回呼ぶ（押しているか・カメラが変わったかを、呼ぶたびに前に呼んだときと比べる）</para>
	/// </summary>
	/// <param name="input">x：右が＋、y：奥（上）が＋。InputManager::GetMoveAxis の値</param>
	Vector3 ToWorld(const Vector2& input);

	/// <summary>
	/// 覚えている向き・カメラを忘れる（次に呼んだときは、今のカメラの向きから始める）。ワープ・シーンの切り替えの後など、前の向きを持ち越したくないときに
	/// </summary>
	void Reset();

	// 今、切り替わる前のカメラの向きを使い続けているか
	bool IsHolding() const { return isHolding_; }

	// 前に呼んだときに使った向き（度。CameraSystem::GetMainCameraYaw と同じ測り方）
	float GetYaw() const { return yaw_; }

private:
	Handle<Entity> camera_;   // 前に呼んだときの、写していたカメラ（Camera Component の Entity）
	float yaw_ = 0.0f;        // 前に呼んだときに使った向き（度）
	bool wasPressed_ = false; // 前に呼んだときに押していたか
	bool isHolding_ = false;  // 切り替わる前のカメラの向きを使い続けているか
};
