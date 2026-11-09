#pragma once
#include <span>

#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector3.h"
#include "HTTEngine/Components/Effects/TrailComponent.h"

// 前方宣言
class Camera;

/// <summary>
/// 軌跡の点1つ
/// </summary>
struct TrailPoint {
	Vector3 position; // ワールド座標
	float age = 0.0f; // 足してからの秒（Time::GetDeltaTime で数える。スロー・一時停止で遅くなる・止まる）
};


/// <summary>
/// TrailComponent を持つEntityの点の列を持ち、帯にして描く
/// <para>点の列は実行中だけの状態（保存しない。Play / Stop で作り直すと消える。停止中は持たない）</para>
/// <para>一覧などの中身は TrailSystem.cpp の中だけに置く（このヘッダはゲームのComponentのファイルがみんな読むので、軽くしておく）</para>
/// </summary>
class TrailSystem {
public:
	// TrailComponent を登録する（置き場所・保存・Inspector）。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// 毎フレーム1回、EntityManager::FlushDestroy の後に呼ぶ（ワールド行列も計算済み）
	/// <para>持ち主（Entity か TrailComponent）が消えた尾を持ち主から離し、点を古くして寿命の物を消し、Emitting の Trail に今の位置を足す</para>
	/// <para>離れた尾は、写しておいた設定の After Owner で消す（Shrink：点が寿命で消えきるまで／Fade：形はそのままで Fade Seconds かけて薄く／Discard：そのフレームに捨てる）。新しい点は足さない</para>
	/// </summary>
	/// <param name="isGameRunning">ゲームが動いているか（再生中・一時停止中は true、エディタの停止中は false＝点を全部捨てる）</param>
	/// <param name="deltaTime">経過秒（Time::GetDeltaTime。スロー・一時停止が掛かった物）</param>
	static void Update(bool isGameRunning, float deltaTime);

	// 帯の描画リクエストを積む（WindowManager::DrawAll から）。camera は帯をカメラへ向けるのに使う
	static void Draw(Camera* camera);

	// 全部の尾（持ち主から離れて消えている途中の物も）を捨てる。シーンを変えたときに SceneManager から呼ぶ
	// 点は前のシーンのワールドの位置なので、新しいシーンへ持ち越さない（ParticleManager::ClearGroups と同じ所で呼ぶ）
	static void ClearAll();

	// ===== ゲームから（SYSTEM の中で）使う =====

	// 点を1つ足す（新しい方の端へ）。TrailComponent を RequestAdd したのと同じフレームに呼んでもよい
	static void AddPoint(Handle<Entity> entity, const Vector3& position);
	// 点を全部消す
	static void Clear(Handle<Entity> entity);
	// 点の列（古い方 → 新しい方）。無ければ空（持ち主から離れた尾は、もう誰の物でもないので出ない）。次に点を足す・Update するまで有効（覚えておかない）
	static std::span<const TrailPoint> GetPoints(Handle<Entity> entity);
	// 線の長さ（点と点の距離の合計）。呼ぶたびに数える
	static float CalcLength(Handle<Entity> entity);
};