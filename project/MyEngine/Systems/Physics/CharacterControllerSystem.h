#pragma once
#include "MyEngine/Components/Physics/CharacterControllerComponent.h"

// 前方宣言
class Camera;


/// <summary>
/// Character Controller を、速さと重力で動かす（立てる坂で止まる・段を上る・壁に沿って滑る・地面に吸い付く）
/// <para>形は同じ Entity の Collider（Sphere・Capsule）。何に当たるかは CollisionSystem::Cast・Overlap に聞く（形の種類は知らない。新しい形も、そのまま歩ける）</para>
/// <para>押し出しだけでなく、動かす前に当たる所を探して（Cast）そこで止めるので、速く動いても薄い壁をすり抜けない</para>
/// <para>ぶつかった組は CollisionSystem::ReportImpact で知らせる（Script の OnCollision〜 が Rigidbody と同じように来る）</para>
/// <para>動かすのは再生中だけ。一時停止・ヒットストップ中は時間が0なので止まる。スロー（SetGameTimeScale）ではゆっくり動く</para>
/// </summary>
class CharacterControllerSystem {
public:
	// CharacterControllerComponent を登録する（置き場所・保存・Inspector・動き方の窓口）。EntityManager・MotionQuery の Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// Character Controller を動かす。ワールド行列の計算と CollisionSystem::SyncWorld の後、物理（PhysicsSystem）の前に呼ぶ
	/// </summary>
	/// <param name="deltaTime">経過秒（Time::GetDeltaTime。0 なら何もしない）</param>
	/// <returns>動かした物があったか（あれば、呼んだ側がワールド行列を計算し直す）</returns>
	static bool Update(float deltaTime);

	// 上れる段の高さの輪・地面の向きの線を描く（エディタ用。Collider のギズモと一緒に出す・消す）
	static void DrawGizmos(Camera* camera);
};
