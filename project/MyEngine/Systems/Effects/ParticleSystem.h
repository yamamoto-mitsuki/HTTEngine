#pragma once
#include "MyEngine/Components/Effects/ParticleSystemComponent.h"
#include "MyEngine/Core/Handle.h"
#include "MyEngine/ECS/Entity.h"

/// <summary>
/// ParticleSystemComponent を持つEntityの粒を出し・動かし・描く
/// <para>粒は実行中だけの状態（保存しない。Play / Stop で消える。停止中は持たない。ただし Inspector の Preview 中の1つだけは動かす）</para>
/// <para>一覧などの中身は ParticleSystem.cpp の中だけに置く（このヘッダはゲームのComponentのファイルがみんな読むので、軽くしておく）</para>
/// </summary>
class ParticleSystem {
public:
	static constexpr int kMaxParticles = 4096; // 1つの Particle System が同時に出せる数の上限（Max Particles の上限）

	// ParticleSystemComponent を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// 毎フレーム1回、EntityManager::FlushDestroy の前に呼ぶ（ワールド行列も計算済み。Stop Action の Destroy は、すぐ後の FlushDestroy で消える）
	/// <para>消えた・外した Particle System の粒を捨て、出して、動かして、寿命の粒を消す。出し終わった物は Stop Action をする（Destroy なら Entity を消す予約）</para>
	/// <para>Instantiate した Prefab の Component は次のフレームの頭に付くので、Play On Start もそのフレームから（付いて初めて見つけたとき）</para>
	/// </summary>
	/// <param name="isGameRunning">ゲームが動いているか（再生中・一時停止中は true、エディタの停止中は false＝Preview 中の1つ以外の粒を全部捨てる）</param>
	/// <param name="deltaTime">経過秒（Time::GetDeltaTime。スロー・一時停止が掛かった物）</param>
	static void Update(bool isGameRunning, float deltaTime);

	// 粒の描画リクエストを積む（WindowManager::DrawAll から。粒はどのビューでもカメラの方を向く）
	static void Draw();

	// ===== ゲームから（Script・SYSTEM の中で）使う =====

	// 最初から出し始める（Burst も出る）。出している途中で呼んでも最初から。Play On Start を外した物は、これを呼ぶまで出さない
	static void Play(Handle<Entity> entity);
	// 出すのをやめる。clear：出ている粒も消す（false なら、出ている粒は寿命まで残る）
	// 出し始めた後に呼ぶと「出し終わり」になる（粒が全部消えたら Stop Action。Looping の物も）
	static void Stop(Handle<Entity> entity, bool clear = false);
	// 今すぐ count 個出す（Play していなくても出る。Component の値で出す）。Component が付く前（RequestAdd した同じフレーム）は何もしない
	static void Emit(Handle<Entity> entity, int count);
	// 出している途中か、粒が1つでも残っているか（Burst が全部消えたら false。出し終わったら消すだけなら、Stop Action の Destroy で足りる）
	static bool IsAlive(Handle<Entity> entity);

	// ===== エディタ（Inspector の Preview ボタン）=====
	// 停止中にも、この1つだけ流す（止めない時間で。選んでいる物を変えたら止まる）。component は Inspector が描いている Component
	static void StartPreview(const ParticleSystemComponent& component);
	static void StopPreview();
	// component を Preview しているか
	static bool IsPreviewing(const ParticleSystemComponent& component);
};