#pragma once
#include "HTTEngine/Components/Physics/ElasticSurfaceComponent.h"
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector3.h"

// 前方宣言
class Camera;
class IBLEnvironment;
struct WorldCollider;


/// <summary>
/// ElasticSurfaceComponent（弾む面）を持つ Entity の、揺れている形を持ち、厚みのある板として描く
/// <para>形を進めるのと、乗った物を押し返すのは PhysicsSystem の小さな1歩の中（PhysicsSystem が下の「PhysicsSystem から」の関数を呼ぶ）</para>
/// <para>形は実行中だけの状態（保存しない）。停止中はいつも平ら。Play / Stop で Entity が作り直されると、平らから始まる</para>
/// <para>一覧などの中身は ElasticSurfaceSystem.cpp の中だけに置く（このヘッダはゲームの Script・Component のファイルがみんな読むので、軽くしておく）</para>
/// </summary>
class ElasticSurfaceSystem {
public:
	// ElasticSurfaceComponent を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// 毎フレーム1回、EntityManager::FlushDestroy の後に呼ぶ
	/// <para>消えた・外した面の形を捨てる。停止中は平らに戻す</para>
	/// </summary>
	/// <param name="isGameRunning">ゲームが動いているか（再生中・一時停止中は true、エディタの停止中は false）</param>
	static void Update(bool isGameRunning);

	/// <summary>
	/// 面の描画リクエストを積む（WindowManager::DrawAll から。停止中も描く）
	/// </summary>
	/// <param name="environment">環境光（見た目の Shading が PBR のときに要る。nullptr なら Half Lambert で描かれる）</param>
	static void Draw(Camera* camera, IBLEnvironment* environment);


	// ===== PhysicsSystem から（1フレームに BeginPhysics を1回 → 小さな1歩ごとに CalcContactForce・Step）=====

	/// <summary>
	/// 使う面を集めて、Inspector の値を入れる。ワールド行列の計算の後に呼ぶ
	/// </summary>
	/// <returns>面が要求する1歩の長さ（秒）。面が1つも無ければ0</returns>
	static float BeginPhysics();
	/// <summary>
	/// 物の形（Collider）が面に入っていたら、面から押し返される力（N）を返す。面には同じ力が下向きにたまる（反作用）
	/// </summary>
	/// <param name="velocityY">物の上下の速さ（m/s。上が+）</param>
	static Vector3 CalcContactForce(const WorldCollider& shape, float velocityY);
	// 全部の面を stepTime 秒進める（たまった力で沈み、張りで戻る）
	static void Step(float stepTime);


	// ===== ゲームから使う（ワールド座標）=====

	// worldX の真上・真下にある面（無ければ無効な Handle）。面が重なっていたら最初の1つ
	static Handle<Entity> FindSurface(float worldX);
	// その面の、worldX の所の高さ（ワールドの Y）。面が無ければ0
	static float GetHeight(Handle<Entity> surface, float worldX);
	// その面の、worldX の所の上下の速さ（m/s。上が+。床が戻る勢いに合わせて跳ぶときに使う）
	static float GetVelocity(Handle<Entity> surface, float worldX);
	// その面を、worldX の周り（半径 radius）で一瞬で叩く（N・s。下向きは負）。ヒップドロップなど
	static void AddImpulse(Handle<Entity> surface, float worldX, float impulse, float radius);
};
