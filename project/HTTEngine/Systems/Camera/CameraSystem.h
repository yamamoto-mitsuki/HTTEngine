#pragma once
#include <memory>

#include "HTTEngine/Components/Camera/CameraComponent.h"
#include "HTTEngine/Components/Camera/CameraFollowComponent.h"
#include "HTTEngine/Components/Camera/CameraLookAtComponent.h"
#include "HTTEngine/Components/Camera/CameraPathComponent.h"
#include "HTTEngine/Components/Camera/CameraShakeComponent.h"
#include "HTTEngine/Components/Camera/CameraVolumeComponent.h"
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Easing.h"
#include "HTTEngine/Math/Vector3.h"
#include "HTTEngine/Time/EngineTime.h"

// 前方宣言
class Camera;
struct TransformComponent;

/// <summary>
/// 通常のカメラ（Priority で選ぶカメラ）の代わりに写すカメラの頼み。無効な Handle は「通常のカメラ」という意味
/// </summary>
struct CameraOverride {
	Handle<Entity> from;                      // 混ぜる前のカメラ
	Handle<Entity> to;                        // 混ぜた先のカメラ
	float weight = 1.0f;                      // to の割合（0：from 〜 1：to）
	float exitTime = 0.0f;                    // 頼むのをやめたら、この秒をかけて通常のカメラへ戻す（0 ならすぐ）
	Ease::Type exitEase = Ease::Type::Linear; // 戻すときの速さの変え方
	TimeMode timeMode = TimeMode::Game;       // from・to のカメラの動きと、戻す補間に使う時間
};


/// <summary>
/// CameraComponent を持つEntityから、ゲームの絵を撮るカメラを決める（Unity の Cinemachine の Brain と Virtual Camera の小さい版）
/// <para>カメラ（Camera クラス）はここが1つだけ持ち、毎フレーム、有効な Camera Component の中で priority が一番大きい物の値をそこへ写す。
/// シーンの GetCamera() は、上書きしなければこのカメラを返す</para>
/// <para>1台分の値 ＝ Transform ＋ Camera Path（位置）＋ Camera Look At（向き）＋ Camera Shake（揺れ）＋ Camera Volume（ポストエフェクト）。Transform を動かすのは Camera Follow と Timelineのキーだけで、ほかは写す値にだけ入る</para>
/// <para>priority が一番大きいカメラが変わると、Camera の Blend の設定で、今見えている値からそのカメラの値へ補間する</para>
/// <para>Timeline のカメラのトラックは RequestOverride で、そのフレームに写すカメラを頼む（頼まなくなれば通常のカメラへ戻る）</para>
/// </summary>
class CameraSystem {
public:
	// カメラの Component を全部登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// Camera Follow を付けた Entity（カメラ）の Transform を、追う物へ近づける（再生中だけ。EngineSystems から1フレームに1回）
	/// <para>物理で動いた後（ワールド行列を計算し直した後）、Update の前に呼ぶ。追う物の、そのフレームの位置を追うため</para>
	/// <para>時間はカメラごとに Camera の Time Mode で決まる</para>
	/// </summary>
	/// <returns>動かしたカメラがあったか（あれば、呼んだ側でワールド行列を計算し直す）</returns>
	static bool UpdateFollow();

	// 毎フレーム1回、EntityManager::UpdateTransforms の後に呼ぶ（親の下に置いたカメラも、そのフレームの位置で撮るため）
	// isGameRunning が false（エディタの停止中）なら、補間も揺れも無しで、置いた通りの絵を撮る
	static void Update(bool isGameRunning);

	// シーンを入れ替えるときに呼ぶ（SceneManager から）。次の Update は、前のシーンのカメラから補間せずに、新しいカメラをそのまま撮る
	static void ResetOutput();

	// ゲームの絵を撮るカメラ。Camera Component が1つも無いときは、既定の位置（(0, 0, -10) から +Z を向く）のカメラ
	// nullptr にはならない。借りるだけ（その場で使う）ならこちら。注視点・揺れ・補間が入った、画面に写っている値を持つ
	static Camera* GetMainCamera();
	// 同じカメラを shared_ptr で返す。フレームをまたいで覚えておきたい物は、これを weak_ptr にして持つ
	// （CameraSystem::Release でカメラが消えても、weak_ptr なら「もう無い」と分かる）
	static std::shared_ptr<Camera> GetSharedMainCamera();
	// 今使っている Camera Component の Entity（無ければ無効な Handle）。補間の途中は、切り替わった先のカメラ
	static Handle<Entity> GetMainCameraEntity();
	// カメラの切り替えの補間の途中か
	static bool IsBlending();
	// そのカメラの本来の位置（Camera Path があれば線の上。注視点・揺れ・補間は入らない。エディタの Camera Path の印に使う）
	static Vector3 CalcCameraPosition(Handle<Entity> entity);

	// メインカメラの Camera Follow が追っている物の Transform（Camera Follow・Target が無ければ nullptr。停止中も返す）
	// 風の線（WindZoneSystem）・被写界深度のピント（DepthOfFieldSystem）が使う
	static const TransformComponent* GetFollowTargetTransform();

	// 同じ物の Entity（無ければ無効な Handle）。Trigger Activator が「入ったのは追っている物か」を見るのに使う
	static Handle<Entity> GetFollowTargetEntity();

	// ===== ゲームから使う =====
	/// <summary>
	/// 有効な Camera Shake 全部へ、衝撃を送る（Unity の Cinemachine の Impulse Source の GenerateImpulse と同じ）。向きは画面の面の中でランダム
	/// </summary>
	/// <param name="strength">最初に振れる幅（m）。Camera Shake の Gain が掛かる</param>
	static void AddImpulse(float strength);

	/// <summary>
	/// 向きを決めて衝撃を送る
	/// </summary>
	/// <param name="strength">最初に振れる幅（m）。Camera Shake の Gain が掛かる</param>
	/// <param name="worldDirection">最初に振れる向き（ワールド）。長さは1でなくてよい</param>
	static void AddImpulse(float strength, const Vector3& worldDirection);


	// ===== Timeline から使う（どれも頼んだフレームの Update だけ効く。TimelineSystem::Update の中から呼ぶ）=====
	/// <summary>
	/// このフレームに写すカメラを頼む（停止中のプレビューでも効く）。2つ頼まれたら、後の頼みが勝つ
	/// </summary>
	static void RequestOverride(const CameraOverride& request);

	/// <summary>
	/// 停止中のプレビューで揺れを見せる。ノイズは time 秒の所を読む
	/// </summary>
	static void BeginPreviewShake(float time);

	/// <summary>
	/// BeginPreviewShake の後に呼ぶ。age 秒前に受けた衝撃を、今の揺れに足して見せる
	/// </summary>
	/// <param name="strength">最初に振れる幅（m）</param>
	/// <param name="direction">最初に振れる向き</param>
	/// <param name="isWorldDirection">true：direction はワールドの向き false：カメラの右・上・前の向き</param>
	/// <param name="age">受けてからの秒</param>
	static void AddPreviewImpulse(float strength, const Vector3& direction, bool isWorldDirection, float age);
};