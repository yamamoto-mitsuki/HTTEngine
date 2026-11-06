#pragma once
#include <vector>
#include <string_view>

#include "MyEngine/Components/Animation/AnimatorComponent.h"
#include "MyEngine/Core/Handle.h"
#include "MyEngine/ECS/Entity.h"
#include "MyEngine/Math/Matrix4x4.h"

// 前方宣言
class Camera;
class AnimationPlayer;


/// <summary>
/// AnimatorComponentを持つEntityのアニメーションを進めて、ModelRendererのノード行列を作る
/// <para>Entityごとの AnimationPlayer（今の時刻・ノードの行列）はここが持つ。Componentはデータだけ</para>
/// <para>再生中は時刻を進める。停止中（Editing）は、InspectorのPreviewTimeのポーズを見せる</para>
/// </summary>
class AnimationSystem {
public:
	// AnimatorComponentを登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ。
	static void Initialize();
	static void Release();

	/// <summary>
	/// 毎フレーム1回、EntityManager::UpdateTransform の前に呼ぶ
	/// <para>消えた・外した Animatorを一覧から外し、始まったばかりの Animatorを再生し、
	/// 時刻を進めて、ノードの行列を作り直す</para>
	/// </summary>
	/// <param name="isGameRunning">ゲームが動いているか（再生中・一時停止中は true、エディタの停止中は false）</param>
	/// <param name="deltaTime">経過秒（Time::GetDeltaTime。一時停止中は0なので、時刻は進まない）</param>
	static void Update(bool isGameRunning, float deltaTime);

	/// <summary>
	/// そのEntityのノード行列
	/// </summary>
	/// <param name="entity"></param>
	/// <returns>Animator が無い・無効・まだ作っていないなら nullptr（モデルに入っている形で描く）</returns>
	static const std::vector<Matrix4x4>* GetNodeMatrices(Handle<Entity> entity);


	// ===== ゲームから使う =====
	// Script・攻撃部品から呼ぶ。どれも、呼んだフレームの Update（Script・攻撃表の AI より後に回る）から効く
	// 流し始めたフレームは時刻を進めないので、そのフレームはアニメの 0 秒の形が見える
	// アニメーションは名前で選ぶ（Blender ならアクションの名前。ファイルの中で並びが変わっても、同じアニメが流れる）
	// 名前が Animator のファイルに無いときは、ログに警告を出す（同じファイル・同じ名前は1回だけ）

	/// <summary>
	/// アニメーションを名前で流す（毎フレーム呼んでよい）。Animator の Clip・Loop・Speed をこの値に書き換える
	/// <para>名前が違う・止まっている（ループしないアニメが最後まで行った・Stop した）→ そのアニメを最初から</para>
	/// <para>同じ名前が流れている → 速さと loop だけ変える（最初には戻らない。途中で最初からにしたいときは Restart）</para>
	/// </summary>
	/// <param name="clip">アニメーションの名前（Animator の Clip と同じ名前）</param>
	/// <param name="loop">ループするか</param>
	/// <param name="speed">1 で等速（CalcSpeedToFit で「N 秒で流しきる」速さを作れる）</param>
	/// <returns>流れているか（Animator が無い・その名前のアニメが無い・Entity が無効なら false）</returns>
	static bool Play(Handle<Entity> entity, std::string_view clip, bool loop, float speed = 1.0f);
	// 今のアニメーション（Animator の Clip）を最初から流し直す。Animator が無い・アニメーションが未選択・ファイルに無いなら false
	static bool Restart(Handle<Entity> entity);
	// そのアニメーションの長さ（秒）。Animator・アニメが無ければ 0
	static float GetClipDuration(Handle<Entity> entity, std::string_view clip);
	// そのアニメーションを、ちょうど seconds 秒で流しきる速さ（長さ ÷ seconds）。Animator・アニメが無い・seconds が 0 以下なら 1
	static float CalcSpeedToFit(Handle<Entity> entity, std::string_view clip, float seconds);
	// 止めて最初の時刻に戻す
	static void Stop(Handle<Entity> entity);
	// 再生中か（ループしないアニメが最後まで行ったら false）
	static bool IsPlaying(Handle<Entity> entity);
	// 細かい操作（SetTime・SetPauseなど）をしたい用。無ければ nullptr
	// 返したポインタはそのフレームの中だけ使う（Animatorを足す・消すと、一覧の場所が変わる）
	static AnimationPlayer* GetPlayer(Handle<Entity> entity);


	// ===== ギズモ =====
	/// <summary>
	/// 骨（スキンのあるモデルのJoint）を球、親子を線で描く。Model Rendererを持つ全部の Entityが対象
	/// <para>Animator があれば動いているポーズ、無ければモデルに入っている形。物の後ろ・中でも見えるように描く</para>
	/// </summary>
	/// <param name="camera"></param>
	static void DrawGizmos(Camera* camera);
	// 骨のギズモを出すか
	static bool& GetShowGizmos();
};
