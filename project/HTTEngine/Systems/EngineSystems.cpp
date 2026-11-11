#include "HTTEngine/Systems/EngineSystems.h"

#include "HTTEngine/AI/AttackTableManager.h"
#include "HTTEngine/Combat/DamageTypes.h"
#include "HTTEngine/Curve/CurveManager.h"
#include "HTTEngine/Graphics/Material/MaterialManager.h"
#include "HTTEngine/Timeline/TimelineManager.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/ECS/Layers.h"
#include "HTTEngine/Particle/ParticleManager.h"
#include "HTTEngine/Physics/MotionQuery.h"
#include "HTTEngine/Physics/PhysicMaterialManager.h"
#include "HTTEngine/Scene/Prefab/PrefabManager.h"
#include "HTTEngine/Scene/SceneTransition.h"
// --- Systems ---
// Animation
#include "HTTEngine/Systems/Animation/AnimationSystem.h"
#include "HTTEngine/Systems/Animation/TimelineSystem.h"
// Audio
#include "HTTEngine/Systems/Audio/AudioSystem.h"
// Camera
#include "HTTEngine/Systems/Camera/CameraSystem.h"
// Combat
#include "HTTEngine/Systems/Combat/DamageSystem.h"
#include "HTTEngine/Systems/Combat/HitSystem.h"
#include "HTTEngine/Systems/Combat/HitboxTelegraphSystem.h"
// Effects
#include "HTTEngine/Systems/Effects/ParticleSystem.h"
#include "HTTEngine/Systems/Effects/TrailSystem.h"
// GamePlay
#include "HTTEngine/Systems/Gameplay/ScriptSystem.h"
#include "HTTEngine/Systems/Gameplay/AttackTableSystem.h"
#include "HTTEngine/Systems/Gameplay/TimelineTriggerSystem.h"
#include "HTTEngine/Systems/Gameplay/TriggerActivatorSystem.h"
// Lights
#include "HTTEngine/Systems/Lighting/LightSystem.h"
// Physics
#include "HTTEngine/Systems/Physics/CharacterControllerSystem.h"
#include "HTTEngine/Systems/Physics/CollisionSystem.h"
#include "HTTEngine/Systems/Physics/ElasticSurfaceSystem.h"
#include "HTTEngine/Systems/Physics/PhysicsSystem.h"
#include "HTTEngine/Systems/Physics/WindZoneSystem.h"
// Rendering2D
#include "HTTEngine/Systems/Rendering2D/SpriteRenderSystem.h"
// Rendering3D
#include "HTTEngine/Systems/Rendering3D/ArrowStripSystem.h"
#include "HTTEngine/Systems/Rendering3D/DecalRenderSystem.h"
#include "HTTEngine/Systems/Rendering3D/DepthOfFieldSystem.h"
#include "HTTEngine/Systems/Rendering3D/FogSystem.h"
#include "HTTEngine/Systems/Rendering3D/PolygonShapeSystem.h"
#include "HTTEngine/Systems/Rendering3D/PrimitiveRenderSystem.h"
// UI
#include "HTTEngine/Systems/UI/UIMaskSystem.h"
#include "HTTEngine/Systems/UI/UIStateSystem.h"
#include "HTTEngine/Systems/UI/PolylineSystem.h"
#include "HTTEngine/Systems/UI/TextRenderSystem.h"
#include "HTTEngine/Systems/UI/VectorShapeSystem.h"
#include "HTTEngine/Systems/UI/UIWorldFollowSystem.h"

#include "HTTEngine/Time/EngineTime.h"
#include "HTTEngine/UI/UIScreen.h"
#ifdef USE_IMGUI
#include "HTTEngine/Editor/Gizmos/CameraGizmo.h"
#include "HTTEngine/Editor/History/EditorHistory.h"
#include "HTTEngine/Editor/Inspector/AssetInspector.h"
#include "HTTEngine/Editor/Inspector/AttackTableEditor.h"
#include "HTTEngine/Editor/Inspector/CurveAssetEditor.h"
#include "HTTEngine/Editor/Inspector/MaterialAssetEditor.h"
#include "HTTEngine/Editor/Inspector/TimelineAssetEditor.h"
#include "HTTEngine/Editor/Windows/UIScreenEditor.h"
#endif


//=============================================================================
// 初期化・終了
//=============================================================================
// ===== 初期化 =====
void EngineSystems::Initialize() {
	EntityManager::Initialize();

	MotionQuery::Initialize(); // 動き方の窓口（速さ・地面にいるか）。動き方の System（Rigidbody・Character Controller）が答え方を登録するので先に
	Layers::Initialize();      // レイヤーの名前（Settings の Layers）。シーンを読む前に、名前の行を登録しておく
	DamageTypes::Initialize(); // 攻撃の種類の名前（Settings の DamageTypes）

	// --- Component を EntityManager に登録する（保存・Inspector も。どれも EntityManager の後）---
	LightSystem::Initialize();            // ライト
	CollisionSystem::Initialize();        // Collider
	PhysicsSystem::Initialize();          // RigidBody
	CharacterControllerSystem::Initialize(); // Character Controller
	AudioSystem::Initialize();            // AudioSource
	CameraSystem::Initialize();           // Camera
	TrailSystem::Initialize();            // Trail
	ParticleSystem::Initialize();         // Particle System
	ElasticSurfaceSystem::Initialize();   // Elastic Surface
	PrimitiveRenderSystem::Initialize();  // Primitive Renderer
	PolygonShapeSystem::Initialize();     // Polygon Shape・Polygon Renderer
	ArrowStripSystem::Initialize();       // Arrow Strip
	DecalRenderSystem::Initialize();      // Decal Renderer
	WindZoneSystem::Initialize();         // Wind Zone
	AnimationSystem::Initialize();        // Animator
	PrefabManager::Initialize();          // Prefab（Entity をまとめて取っておいたファイル）。中身は JSON だけなので、GPU の物とは関係ない
	PhysicMaterialManager::Initialize();  // Physic Material（跳ね返り・摩擦のファイル）。Inspector で直せるように AssetInspector にも登録する
	SpriteRenderSystem::Initialize();     // Sprite Renderer・Image
	TextRenderSystem::Initialize();       // Text
	VectorShapeSystem::Initialize();      // Vector Shape
	PolylineSystem::Initialize();         // Polyline
	UIMaskSystem::Initialize();           // UI Mask
	UIWorldFollowSystem::Initialize();    // UI World Follow（UI をワールドの物・点に重ねる）
	FogSystem::Initialize();              // Fog
	DepthOfFieldSystem::Initialize();     // Depth Of Field
	TriggerActivatorSystem::Initialize(); // Trigger Activator
	TimelineTriggerSystem::Initialize();  // Timeline Trigger（範囲に入ったら Timeline を再生する）
	HitSystem::Initialize();              // Hitbox・Hurtbox（攻撃の当たり）
	DamageSystem::Initialize();           // Health・Damage Source（攻撃のダメージ）。HitSystem に答え方を登録するので、HitSystem の後
	HitboxTelegraphSystem::Initialize();  // Hitbox Telegraph（攻撃の予告を床に描く。Hitbox の形を読む）
	AttackTableManager::Initialize();     // 攻撃表（.attacktable）。AttackTableSystem が読むので先に
	AttackTableSystem::Initialize();      // Attack Table
	CurveManager::Initialize();           // カーブ（.curve）
	MaterialManager::Initialize();        // マテリアル（.material）。Shader の #PROPERTIES の値と Curve を使う
	TimelineManager::Initialize();        // Timeline（.timeline）。TimelineSystem が読むので先に
	TimelineSystem::Initialize();         // Timeline
	UIScreen::Initialize();               // UI Screen・UI Screen Element
	UIStateSystem::Initialize();          // UI State（要素の状態で Timeline を再生する）
#ifdef USE_IMGUI
	AssetInspector::Initialize(); // Project で名前を変えたファイルを、Inspector が新しいパスで直し続けるように
	UIScreenEditor::Initialize(); // .uiscreen のファイルを Inspector に出す（Edit UI Screen のボタン）
	AttackTableEditor::Initialize(); // .attacktable のファイルを Inspector で直す
	CurveAssetEditor::Initialize();  // .curve のファイルを Inspector で直す
	MaterialAssetEditor::Initialize(); // .material のファイルを Inspector で直す
	TimelineAssetEditor::Initialize(); // .timeline のファイルを Inspector に出す（開くボタン）
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
	CharacterControllerSystem::Release();
	AudioSystem::Release();
	CameraSystem::Release();
	TrailSystem::Release();
	ParticleSystem::Release();
	ElasticSurfaceSystem::Release();
	PrimitiveRenderSystem::Release();
	PolygonShapeSystem::Release();
	ArrowStripSystem::Release();
	DecalRenderSystem::Release();
	WindZoneSystem::Release();
	FogSystem::Release();
	DepthOfFieldSystem::Release();
	TriggerActivatorSystem::Release();
	TimelineTriggerSystem::Release();
	DamageSystem::Release();
	HitboxTelegraphSystem::Release();
	HitSystem::Release();
	AttackTableSystem::Release();
	AttackTableManager::Release();
	MaterialManager::Release();
	CurveManager::Release();
	TimelineSystem::Release();
	TimelineManager::Release();
	UIStateSystem::Release();
	UIScreen::Release();

	AnimationSystem::Release(); // AnimationManager::Release より前にする（AnimationPlayer が中身を見ているので）
	PrefabManager::Release(); // 作りかけ（Component を付ける予約）も一緒に消える
	PhysicMaterialManager::Release();
	SpriteRenderSystem::Release();
	TextRenderSystem::Release();
	VectorShapeSystem::Release();
	PolylineSystem::Release();
	UIMaskSystem::Release();
	UIWorldFollowSystem::Release();
	MotionQuery::Release(); // 動き方の System の Release より後
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
	UIStateSystem::Update(isGameRunning);                           // UI State（マウスで状態を決めて、状態の Timeline の再生を頼む。Script の後・Timeline の前）
	TimelineSystem::Update(isGameRunning);                          // Timeline（Script とアニメーションの後。書いた値を、このフレームのワールド行列・描画に使う。時間は Component ごとに選ぶ）
	EntityManager::UpdateTransforms();      // ワールド行列（親→子）
	WindZoneSystem::Update(isGameRunning, Time::GetDeltaTime()); // 風（Wind Zone）の範囲を集め直して、再生中なら Rigidbody を押す（押した速さで、すぐ後の物理が動かす）。画面の風の線の値も書く

	// キャラ（Character Controller）を動かす。動かす前に、当たりの形を今の位置で集め直す（Script・アニメ・Timeline がこのフレームに動かした物を、動いた後の位置で見る）
	// 物理より前（Unity・Unreal と同じ。Rigidbody から見ると、キャラは動いた後の位置にある壁）。動かしたら、ワールド行列を計算し直す
	if (isGameRunning) {
		CollisionSystem::SyncWorld();
		if (CharacterControllerSystem::Update(Time::GetDeltaTime())) {
			EntityManager::UpdateTransforms();
		}
	}

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
	UIWorldFollowSystem::Update(gameCamera.get());               // UI をワールドの物・点に重ねる（カメラをこのフレームの位置にした後。置いたら、ワールド行列を計算し直す）
	ParticleManager::Update();              // パーティクル更新
	ParticleSystem::Update(isGameRunning, Time::GetDeltaTime()); // パーティクル（Particle System Component）。停止中は Inspector の Preview の1つだけ動く
	LightSystem::Update();                  // ライト更新
	FogSystem::Update();                    // 霧更新
	DepthOfFieldSystem::Update();           // 被写界深度の値（ピントを合わせる Target の位置を使うので、カメラの Update の後）
	ScriptSystem::FlushDestroy();           // Destroy(Script) の予約の反映（OnDestroy を呼ぶ。Entity の破棄より先）
	EntityManager::FlushDestroy();          // 破棄予約の反映
	CollisionSystem::Update();              // 当たり判定（消すと決めたEntityを消した後。結果は次のフレームのSYSTEMが読む）
	DamageSystem::Update(isGameRunning, Time::GetDeltaTime()); // 被弾の後の無敵の残りを減らす（攻撃の当たりより前）
	HitSystem::Update(isGameRunning, Time::GetDeltaTime());    // 攻撃の当たり（全部が動いた後の位置で。消すと決めた Entity はもう当たらない。ダメージの層に聞いて HP を減らす。知らせは次のフレームの Script・SYSTEM が読む）
	TriggerActivatorSystem::Update(isGameRunning, Time::GetDeltaTime()); // 範囲に入った・出たで、Target を出す・消す（当たり判定の後。入ったフレームのうちに出し始める）
	TimelineTriggerSystem::Update(isGameRunning);                        // 範囲に入った・出たで、Timeline の再生を頼む（当たり判定の後。値が動くのは次のフレームの TimelineSystem::Update から）
	UIScreen::Update(isGameRunning);        // 画面（ポーズ画面など）の出す・消す動きを進めて、ゲームを止める・動かす（Motion の動きと、Timeline が終わったかを見る）
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
	DecalRenderSystem::Draw();                                // 箱の中の床・壁に塗る（Decal Renderer）の描画
	HitboxTelegraphSystem::Draw();                            // 攻撃の予告を床に塗る（Hitbox Telegraph）の描画
	SpriteRenderSystem::Draw(gameCamera); // 画像の板（Sprite Renderer）と、画面に重ねる絵（Image）の描画
	VectorShapeSystem::Draw(); // 画像を使わない UI の図形（Vector Shape）の描画（Image・Text とは Order の順に重なる）
	PolylineSystem::Draw();    // 点をつないだ UI の線・形（Polyline）の描画（Image・Text とは Order の順に重なる）
	TextRenderSystem::Draw();  // 画面に重ねる文字（Text）の描画（Image とは Order の順に重なる）
	SceneTransition::Draw();   // シーンを切り替える幕（一番手前。Game ビューだけ）

#ifdef USE_IMGUI
	// --- ライト・Collider のギズモ（エディタ用）---
	LightSystem::DrawGizmos(gameCamera);
	CameraGizmo::Draw(gameCamera);
	CollisionSystem::DrawGizmos(gameCamera);
	CharacterControllerSystem::DrawGizmos(gameCamera); // 上れる段の高さ・地面の向き（Collider のギズモと一緒に出す・消す）
	WindZoneSystem::DrawGizmos(gameCamera); // 風の範囲と向き（Collider のギズモと一緒に出す・消す）
	DecalRenderSystem::DrawGizmos(gameCamera); // デカールの箱（Scene ビューだけ）
	ParticleSystem::DrawGizmos(gameCamera);    // 粒の出る形（Scene ビューだけ）
	HitSystem::DrawGizmos(gameCamera);      // 攻撃の形（段階で色が変わる）・ジャスト判定の範囲・Hurtbox
	AnimationSystem::DrawGizmos(gameCamera);
#endif
	TimelineSystem::FinishPreview(); // Timeline のプレビューの値を元へ戻す（描画の要求を積んだ後。この後の ImGui と、次のフレームの頭の保存・Undo は元の値を見る）
}
