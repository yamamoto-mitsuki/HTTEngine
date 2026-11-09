#pragma once
#include "HTTEngine/Components/Audio/AudioSourceComponent.h"
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Sound/AudioBus.h"

// 前方宣言
class Camera;


/// <summary>
/// AudioSourceComponent を持つEntityの音を、鳴らす・止める
/// <para>鳴っている音の番号（SoundManager の再生ハンドル）はここが持つ。Componentはデータだけ</para>
/// <para>エディタの停止中は鳴らさない。Entityを消す・無効にする・AudioSource を外すと、その音は止まる</para>
/// <para>一覧などの中身は AudioSystem.cpp の中だけに置く（このヘッダはゲームのComponentのファイルがみんな読むので、軽くしておく）</para>
/// </summary>
class AudioSystem {
public:
	// AudioSourceComponent を登録する（置き場所・保存・Inspector）。種類ごとの音量を Parameters に出す。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	static void Release();

	/// <summary>
	/// 毎フレーム1回、EntityManager::FlushDestroy の後に呼ぶ
	/// <para>種類ごとの音量を伝え、消えた・外した・無効になった AudioSource の音を止め、始まったばかりの AudioSource を鳴らし（Play On Start）、
	/// Inspector で変えた音量・ピッチと、3Dの音の大きさ・左右を鳴っている音に伝える</para>
	/// </summary>
	/// <param name="isGameRunning">ゲームが動いているか（再生中・一時停止中は true、エディタの停止中は false）</param>
	/// <param name="listener">聞く人（3Dの音の距離と左右の基準）。ゲームのカメラを渡す。nullptr なら3Dの音も普通の音と同じに鳴る</param>
	static void Update(bool isGameRunning, const Camera* listener);

	// ===== ゲームから（SYSTEM の中で）使う =====

	// そのEntityの AudioSource を頭から鳴らす（鳴っていたら止めてから）。AudioSource が無い・音が未選択なら false
	static bool Play(Handle<Entity> entity);
	// そのEntityの音を止める（鳴っていなければ何もしない）
	static void Stop(Handle<Entity> entity);
	// そのEntityの音が鳴っているか（一時停止中も true。鳴り終わったら false）
	static bool IsPlaying(Handle<Entity> entity);
	// 重ねて鳴らす（効果音）。Entityを消しても最後まで鳴る。AudioSystem::Stop では止まらない（エディタの Stop では止まる）
	// 3Dの音なら、鳴らした瞬間の位置で大きさと左右が決まる（鳴っている間は動かない）
	static bool PlayOneShot(Handle<Entity> entity);

	// ===== 音の種類ごとの音量（設定画面から。0=無音、1=元の大きさ）=====
	// Parameters ウィンドウの Audio / Volume と同じ値（そこで Save すると、次の起動からその値で始まる）
	static void SetBusVolume(AudioBus bus, float volume);
	static float GetBusVolume(AudioBus bus);
	static void SetMasterVolume(float volume);
	static float GetMasterVolume();

	// ===== エディタの再生コントロールから使う（SceneManager が呼ぶ）=====

	// 全部の音を止める（AudioSource の音も、SoundManager で直接鳴らした音も、試し聞きも）。シーンを作り直す前に呼ぶ
	static void StopAll();
	// 全部の音を一時停止する / 再開する
	static void SetPaused(bool paused);

	// ===== 試し聞き（Inspector の Audio Source の ▶ から）=====
	// 停止中でも鳴らせる。同時に1つだけ（鳴っていたら止めてから鳴らす）。ループ・3D・Play On Start は使わない
	static void Preview(const AudioSourceComponent& source);
	static void StopPreview();
	static bool IsPreviewing();
};