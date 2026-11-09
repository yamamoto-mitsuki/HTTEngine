> HTTEngine リファレンスの1章。ほかの章は [索引](../Reference.md) から探す。

# 12. 音

**ふつうは AudioSource（Component）で鳴らす。** Inspector の **Add Component → Audio → Audio Source** で付け、Sound（`resources/` の下の `.wav` / `.mp3`）・Volume・Pitch・Loop・Play On Start を決めて Save する。

- **BGM はコードが要らない**：Loop と Play On Start に印を付け、**Bus を BGM** にするだけ。Play を押すと鳴り、Pause で止まり、Stop で止まる。停止中は鳴らない。
- Entity を消す・無効にする・Audio Source を外すと、その音は止まる。
- **▶ Preview** で、停止中でも試し聞きできる（Inspector の Volume・Pitch・Bus で1回鳴らす）。

| 項目 | 中身 |
|---|---|
| Bus | 音の種類（BGM / SE）。種類ごとの音量が掛かる。新しく付けた物は SE |
| Spatial | 3Dの音。**ゲームのカメラ**から離れると小さくなり、左右に振れる（位置は Transform） |
| Min Distance / Max Distance | Min まで近いと元の大きさ、Max より遠いと聞こえない。その間はまっすぐ小さくなる |

種類ごとの音量は、Settings ウィンドウ（Edit → Settings）の **Audio** のページ → Volume（**Master・BGM・SE**。1が元の大きさ）。Save すると `Parameters/Audio/Volume.json` に残る。ゲームの設定画面からは次のように書く。

```cpp
	// 設定画面（0〜1）。Settings ウィンドウの Audio / Volume も一緒に変わる
	AudioSystem::SetBusVolume(AudioBus::BGM, 0.5f);
	AudioSystem::SetBusVolume(AudioBus::SE, 0.8f);
	AudioSystem::SetMasterVolume(1.0f);
	const float bgmVolume = AudioSystem::GetBusVolume(AudioBus::BGM);
```

効果音は、SYSTEM の中で `AudioSystem` を呼ぶ（`GameComponent.h` だけで使える）。

```cpp
void EnemyHit(Handle<Entity> entity, Enemy& enemy) {
	for (const CollisionHit& hit : CollisionSystem::GetHits(entity)) {
		if (hit.phase != HitPhase::Enter || EntityManager::Get<Bullet>(hit.other) == nullptr) {
			continue;
		}
		EntityManager::Destroy(hit.other); // 弾は消す
		--enemy.hp;
		// 敵に付けた Audio Source の音を重ねて鳴らす（Play On Start は外しておく）。この後で敵を消しても、音は最後まで鳴る
		AudioSystem::PlayOneShot(entity);
	}
	if (enemy.hp <= 0) {
		EntityManager::Destroy(entity);
	}
}
SYSTEM(EnemyHit);
```

| 関数 | すること |
|---|---|
| `AudioSystem::Play(entity)` | その Entity の Audio Source を頭から鳴らす（鳴っていたら止めてから）。Entity を消すと止まる |
| `AudioSystem::Stop(entity)` | 止める |
| `AudioSystem::IsPlaying(entity)` | 鳴っているか（鳴り終わったら `false`。一時停止中は `true`） |
| `AudioSystem::PlayOneShot(entity)` | 重ねて鳴らす。Entity を消しても最後まで鳴る（倒した音など）。3Dの音なら、鳴らした瞬間の位置で大きさ・左右が決まる |

```cpp
// ✗ だめな例
void EnemyDeath(Handle<Entity> entity, Enemy& enemy) {
	if (enemy.hp <= 0) {
		AudioSystem::Play(entity);      // ✗ 次の行で消すので、鳴った瞬間に止まる
		EntityManager::Destroy(entity); //   → PlayOneShot にする
	}
}
```

`SoundManager` を直接呼んでもよい（Entity に付かない音。シーンをまたいで鳴らし続けたい BGM など。`#include <HTTEngine/Sound/SoundManager.h>`）。

```cpp
const uint32_t bgm = SoundManager::Load("resources/sounds/bgm.wav");                  // 読む（番号が返る）
const uint32_t play = SoundManager::Play(bgm, true, 0.5f, 1.0f, AudioBus::BGM);        // 鳴らす（ループ, 音量, ピッチ, 種類）
SoundManager::SetVolume(play, 0.2f);
SoundManager::Stop(play);
```

`Load` の戻り値が**音のデータ**、`Play` の戻り値が**鳴っている1つ**。止めたり音量を変えたりするのは後者。直接鳴らした音も、エディタの Pause で止まり、Play / Stop / Restart で全部止まる（次のシーンへの切り替えでは止まらない）。`Play` の最後の引数（種類）を書かなければ SE。

```cpp
// ✗ だめな例
void GameScene::Update() {
	const uint32_t se = SoundManager::Load("resources/se.wav"); // ✗ 毎フレーム読む（Loadは Initialize で1回）
	SoundManager::Play(se, false);
}
```

```cpp
	SoundManager::Play(bgm, true); // ✗ SE の音量つまみを通る（BGM の音量を下げても小さくならない）→ 最後に AudioBus::BGM
```

- **ファイル名は英数字にする**（日本語の名前は読めない）。日本語の名前のファイルは Inspector の一覧に出ず、Console に「名前を英数字にしてください」と出る。
