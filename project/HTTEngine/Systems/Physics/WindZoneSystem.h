#pragma once
#include "HTTEngine/Components/Physics/WindZoneComponent.h"
#include "HTTEngine/Math/Vector3.h"

// 前方宣言
class Camera;


/// <summary>
/// Wind Zone（風の吹く範囲）を登録し、範囲の中の Rigidbody を押す。画面の風の線（LensPass）の濃さと向きも決める
/// <para>押し方：風の向きの速さが、風の速さ（Speed）より遅い間だけ、Strength で速くする（それより速ければ押さない）。Unity の Area Effector に近い</para>
/// <para>Rigidbody の Wind Scale が 0 の物は押さない（自分で風を読んで動く物。氷のキューブなど）。その物は Sample・Push を自分で呼ぶ</para>
/// <para>範囲は毎フレーム集め直して覚えておく（Sample は前のフレームの終わりの範囲を使う。Script の Update は、ここより前に呼ばれるので）</para>
/// </summary>
class WindZoneSystem {
public:
	/// <summary>
	/// ある位置の風（重なった範囲は足し合わせる）
	/// </summary>
	struct WindSample {
		Vector3 velocity = {0.0f, 0.0f, 0.0f}; // 風の速さ（向き × 速さ。ゆれ込み。m/s）
		float strength = 0.0f;                 // 風の速さまで速くする強さ（m/s²。重なったら一番強い物。0 なら風の外）
		float lineAmount = 0.0f;               // 画面の風の線の濃さ（ゆれ込み）

		// 風の中か
		bool IsBlowing() const { return strength > 0.0f; }
	};

	// WindZoneComponent を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// 範囲を集め直して、再生中なら Rigidbody を押す。画面の風の線の値を LensPass へ書く
	/// <para>ワールド行列の計算（UpdateTransforms）の後・PhysicsSystem::Update の前に呼ぶ（押した速さで、この後の物理が動かす）</para>
	/// </summary>
	/// <param name="isGameRunning">再生中か（停止中は範囲を集めるだけ。押さない・線を出さない）</param>
	/// <param name="deltaTime">経過秒（Time::GetDeltaTime。一時停止中は 0）</param>
	static void Update(bool isGameRunning, float deltaTime);

	/// <summary>
	/// position の風。範囲の外なら、全部 0
	/// </summary>
	static WindSample Sample(const Vector3& position);

	/// <summary>
	/// velocity を風で押した後の速さ（風の向きの速さが、風の速さより遅い間だけ、strength × deltaTime まで速くする）
	/// </summary>
	static Vector3 Push(const WindSample& wind, const Vector3& velocity, float deltaTime);

	// 範囲と向きを線で描く（エディタ用。Collider のギズモを出しているときだけ）
	static void DrawGizmos(Camera* camera);
};