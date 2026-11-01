#include "MyEngine/Systems/EngineSystems.h"

#include "MyEngine/AI/AttackTableManager.h"
#include "MyEngine/Curve/CurveManager.h"
#include "MyEngine/ECS/ComponentSerializer.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/ECS/GameComponent.h"
#include "MyEngine/Particle/ParticleManager.h"
#include "MyEngine/Physics/PhysicMaterialManager.h"
#include "MyEngine/Scene/PrefabManager.h"
#include "MyEngine/Scene/SceneTransition.h"
// --- Systems ---
// Animation
#include "MyEngine/Systems/Animation/AnimationSystem.h"
// Audio
#include "MyEngine/Systems/Audio/AudioSystem.h"
// Camera
#include "MyEngine/Systems/Camera/CameraSystem.h"
// Effects
#include "MyEngine/Systems/Effects/ParticleSystem.h"
#include "MyEngine/Systems/Effects/TrailSystem.h"
// GamePlay
#include "MyEngine/Systems/Gameplay/ScriptSystem.h"
#include "MyEngine/Systems/Gameplay/AttackTableSystem.h"
#include "MyEngine/Systems/Gameplay/TriggerActivatorSystem.h"
// Lights
#include "MyEngine/Systems/Lighting/LightSystem.h"
#include "MyEngine/Systems/Physics/CollisionSystem.h"
#include "MyEngine/Systems/Physics/ElasticSurfaceSystem.h"
#include "MyEngine/Systems/Physics/PhysicsSystem.h"
#include "MyEngine/Systems/Physics/WindZoneSystem.h"
#include "MyEngine/Systems/Rendering2D/SpriteRenderSystem.h"
// Rendering3D
#include "MyEngine/Systems/Rendering3D/ArrowStripSystem.h"
#include "MyEngine/Systems/Rendering3D/DepthOfFieldSystem.h"
#include "MyEngine/Systems/Rendering3D/FogSystem.h"
#include "MyEngine/Systems/Rendering3D/PolygonShapeSystem.h"
#include "MyEngine/Systems/Rendering3D/PrimitiveRenderSystem.h"
// UI
#include "MyEngine/Systems/UI/TextRenderSystem.h"
#include "MyEngine/Systems/UI/VectorShapeSystem.h"
#include "MyEngine/Time/EngineTime.h"
#include "MyEngine/UI/UIScreen.h"
#ifdef USE_IMGUI
#include "MyEngine/Editor/History/EditorHistory.h"
#include "MyEngine/Editor/Inspector/AttackTableEditor.h"
#include "MyEngine/Editor/Inspector/CurveAssetEditor.h"
#include "MyEngine/Editor/Windows/UIScreenEditor.h"
#endif


//=============================================================================
// 初期化・終了
//=============================================================================
// ===== 初期化 =====
void EngineSystems::Initialize() {
	EntityManager::Initialize();

	// --- Component を EntityManager に登録する（保存・Inspector も。どれも EntityManager の後）---
	LightSystem::Initialize();            // ライト
	CollisionSystem::Initialize();        // Collider
	PhysicsSystem::Initialize();          // RigidBody
	AudioSystem::Initialize();            // AudioSource
	CameraSystem::Initialize();           // Camera
	TrailSystem::Initialize();            // Trail
	ParticleSystem::Initialize();         // Particle System
	ElasticSurfaceSystem::Initialize();   // Elastic Surface
	PrimitiveRenderSystem::Initialize();  // Primitive Renderer
	PolygonShapeSystem::Initialize();     // Polygon Shape・Polygon Renderer
	ArrowStripSystem::Initialize();       // Arrow Strip
	WindZoneSystem::Initialize();         // Wind Zone
	AnimationSystem::Initialize();        // Animator
	PrefabManager::Initialize();          // Prefab（Entity をまとめて取っておいたファイル）。中身は JSON だけなので、GPU の物とは関係ない
	PhysicMaterialManager::Initialize();  // Physic Material（跳ね返り・摩擦のファイル）。Inspector で直せるように AssetInspector にも登録する
	SpriteRenderSystem::Initialize();     // Sprite Renderer・Image
	TextRenderSystem::Initialize();       // Text
	VectorShapeSystem::Initialize();      // Vector Shape
	FogSystem::Initialize();              // Fog
	DepthOfFieldSystem::Initialize();     // Depth Of Field
	TriggerActivatorSystem::Initialize(); // Trigger Activator
	AttackTableManager::Initialize();     // 攻撃表（.attacktable）。AttackTableSystem が読むので先に
	AttackTableSystem::Initialize();      // Attack Table
	CurveManager::Initialize();           // カーブ（.curve）
	UIScreen::Initialize();               // UI Screen・UI Screen Element
#ifdef USE_IMGUI
	UIScreenEditor::Initialize(); // .uiscreen のファイルを Inspector に出す（Edit UI Screen のボタン）
	AttackTableEditor::Initialize(); // .attacktable のファイルを Inspector で直す
	CurveAssetEditor::Initialize();  // .curve のファイルを Inspector で直す
#endif

	// --- Script とゲーム固有の Component ---
	ScriptSystem::Initialize(); // Entityが消える直前の知らせをEntityManagerに頼むので、EntityManagerの後（Scriptの種類の登録は下のApplyAll）
	ComponentSerializer::RegisterEngineComponents(); // Transform・ModelRenderer・ライトの保存項目（Releaseでもシーンを読むのに要る）
	GameComponentRegistry::ApplyAll(); // COMPONENT(...) で書いたゲーム固有Componentを登録（登録待ちの表を空にする）
}

// ===== Script の終了 =====
void EngineSystems::ReleaseScripts() {
	ScriptSystem::Release(); // 動いている Script の OnDestroy を、ほかの物を片付ける前に呼ぶ（中で音やパーティクルを使ってもよいように）
}

// ===== 終了 =====
void EngineSystems::Release() {
	// --- どれもシーン（ウィンドウ）の破棄より後にする ---
	LightSystem::Release();
	CollisionSystem::Release();
	PhysicsSystem::Release();
	AudioSystem::Release();
	CameraSystem::Release();
	TrailSystem::Release();
	ParticleSystem::Release();
	ElasticSurfaceSystem::Release();
	PrimitiveRenderSystem::Release();
	PolygonShapeSystem::Release();
	ArrowStripSystem::Release();
	WindZoneSystem::Release();
	FogSystem::Release();
	DepthOfFieldSystem::Release();
	TriggerActivatorSystem::Release();
	AttackTableSystem::Release();
	AttackTableManager::Release();
	CurveManager::Release();
	UIScreen::Release();

	AnimationSystem::Release(); // AnimationManager::Release より前にする（AnimationPlayer が中身を見ているので）
	PrefabManager::Release(); // 作りかけ（Component を付ける予約）も一緒に消える
	PhysicMaterialManager::Release();
	SpriteRenderSystem::Release();
	TextRenderSystem::Release();
	VectorShapeSystem::Release();
	EntityManager::Release();
}


//=============================================================================
// 更新
//=============================================================================
// ===== 前フレームの予約の反映 =====
void EngineSystems::FlushPending() {
	EntityManager::FlushComponentChanges(); // 前フレームに予約したComponentを追加
	PrefabManager::Flush(); // 前フレームに Prefab から作った Entity に、Component・Script を付ける（エディタから置く予約もここで作る）
	UIScreen::Flush();      // 前フレームに UIScreen::Open で作った画面に、出し始めの形を当てる（Component が付いた直後・描く前）
#ifdef USE_IMGUI
	EditorHistory::Flush();
	UIScreenEditor::Flush(); // 画面（UI Screen）の編集に入る・出る（Undo・Delete などの予約を済ませた後。中身の Component は次のフレームの頭に付く）
#endif
}

// ===== 更新 =====
void EngineSystems::Update(bool isGameRunning, const std::shared_ptr<Camera>& gameCamera) {
	ParticleManager::SetCamera(gameCamera); // パーティクルのビルボード用カメラは、現在のシーンから毎フレーム取る。見つけたカメラを毎フレーム渡す（シーンが替わっても古いカメラを掴まない）
	AttackTableSystem::Update(isGameRunning, Time::GetDeltaTime()); // 攻撃表の AI（シーン・Script・SYSTEM の後。攻撃部品が動かした位置を、このフレームのワールド行列・物理に使う）
	AnimationSystem::Update(isGameRunning, Time::GetDeltaTime());   // アニメーション。停止中もInspectorのPreview Timeのポーズを見せる。
	EntityManager::UpdateTransforms();      // ワールド行列（親→子）
	WindZoneSystem::Update(isGameRunning, Time::GetDeltaTime()); // 風（Wind Zone）の範囲を集め直して、再生中なら Rigidbody を押す（押した速さで、すぐ後の物理が動かす）。画面の風の線の値も書く
	
	// Rigid body を動かして、ぶつかった物から押し戻す
	// 動かした物があれば、ワールド行列を計算し直す（カメラ・ライト・当たり判定・描画が、押し戻した後の位置を使うように）
	if (isGameRunning && PhysicsSystem::Update(Time::GetDeltaTime())) {
		EntityManager::UpdateTransforms();
	}
	// カメラの追いかけ（Camera Follow）。物理で動いた後の位置を追う。カメラを動かしたら、カメラ（と子）のワールド行列を計算し直す
	if (isGameRunning && CameraSystem::UpdateFollow()) {
		EntityManager::UpdateTransforms();
	}

	CameraSystem::Update(isGameRunning);                         // Camera Component の位置・向きをゲームのカメラへ写す（ワールド行列の後、使う人たちの前）
	ParticleManager::Update();              // パーティクル更新
	ParticleSystem::Update(isGameRunning, Time::GetDeltaTime()); // パーティクル（Particle System Component）。停止中は Inspector の Preview の1つだけ動く
	LightSystem::Update();                  // ライト更新
	FogSystem::Update();                    // 霧更新
	DepthOfFieldSystem::Update();           // 被写界深度の値（ピントを合わせる Target の位置を使うので、カメラの Update の後）
	ScriptSystem::FlushDestroy();           // Destroy(Script) の予約の反映（OnDestroy を呼ぶ。Entity の破棄より先）
	EntityManager::FlushDestroy();          // 破棄予約の反映
	CollisionSystem::Update();              // 当たり判定（消すと決めたEntityを消した後。結果は次のフレームのSYSTEMが読む）
	TriggerActivatorSystem::Update(isGameRunning, Time::GetDeltaTime()); // 範囲に入った・出たで、Target を出す・消す（当たり判定の後。入ったフレームのうちに出し始める）
	UIScreen::Update(isGameRunning);        // 画面（ポーズ画面など）の出す・消す動きを進めて、ゲームを止める・動かす（止めない時間で動く）
	SceneTransition::Update(isGameRunning); // シーンを切り替える幕を進める（閉じきったら切り替えを頼む。切り替わるのは次のフレームの頭）
	AudioSystem::Update(isGameRunning, gameCamera.get()); // 音（消したEntityの音を止めるので、FlushDestroy の後）。エディタの停止中はAudioSourceを鳴らさない
	TrailSystem::Update(isGameRunning, Time::GetDeltaTime());
	ElasticSurfaceSystem::Update(isGameRunning);
}


//=============================================================================
// 描画
//=============================================================================
// ===== 描画リクエストを積む =====
void EngineSystems::Draw(Camera* gameCamera, IBLEnvironment* gameEnvironment) {
	ParticleManager::Draw();                                  // パーティクルの描画
	TrailSystem::Draw(gameCamera);                            // 軌跡の描画
	ParticleSystem::Draw();                                   // パーティクル（Particle System Component）の描画。粒はビューごとのカメラへ VS で向ける
	ElasticSurfaceSystem::Draw(gameCamera, gameEnvironment);  // 弾む面（厚みのある板と支柱）の描画
	PrimitiveRenderSystem::Draw(gameCamera, gameEnvironment); // 決まった形（球・箱）の描画
	PolygonShapeSystem::Draw(gameCamera, gameEnvironment);    // 多角形を押し出した形（足場）の描画
	ArrowStripSystem::Draw(gameCamera, gameEnvironment);      // 向きを示す矢印の列の描画
	SpriteRenderSystem::Draw(gameCamera); // 画像の板（Sprite Renderer）と、画面に重ねる絵（Image）の描画
	VectorShapeSystem::Draw(); // 画像を使わない UI の図形（Vector Shape）の描画（Image・Text とは Order の順に重なる）
	TextRenderSystem::Draw();  // 画面に重ねる文字（Text）の描画（Image とは Order の順に重なる）
	SceneTransition::Draw();   // シーンを切り替える幕（一番手前。Game ビューだけ）

#ifdef USE_IMGUI
	// --- ライト・Collider のギズモ（エディタ用）---
	LightSystem::DrawGizmos(gameCamera);
	CollisionSystem::DrawGizmos(gameCamera);
	WindZoneSystem::DrawGizmos(gameCamera); // 風の範囲と向き（Collider のギズモと一緒に出す・消す）
	AnimationSystem::DrawGizmos(gameCamera);
#endif
}
