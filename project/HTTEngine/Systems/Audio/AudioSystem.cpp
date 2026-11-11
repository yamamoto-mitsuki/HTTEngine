#include "AudioSystem.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <externals/magic_enum/magic_enum.hpp>

#include "HTTEngine/Camera/Camera.h"
#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/ECS/GameComponent.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Sound/SoundManager.h"
#include "HTTEngine/UI/GlobalVariables.h"

namespace {
constexpr float kMaxVolume = 3.0f; // 音量の上限（Inspector で動かせる範囲と同じ。1より上は元より大きくなる）
constexpr float kMinPitch = 0.5f;  // ピッチの下限（Inspector で動かせる範囲と同じ）
constexpr float kMaxPitch = 2.0f;  // ピッチの上限（XAudio2 は、音を作るときの上限＝既定の2倍までしか上げられない）

// 種類ごとの音量を置く場所（Parameters ウィンドウの Audio タブ → Volume）
constexpr const char* kVolumeGroup = "Audio";
constexpr const char* kVolumeCategory = "Volume";
constexpr const char* kMasterName = "Master";
constexpr size_t kBusCount = magic_enum::enum_count<AudioBus>();

/// <summary>
/// AudioSource 1つ分の、実行中だけの状態（保存しない。Play / Stop で作り直すと消える）
/// </summary>
struct SourceState {
	Handle<Entity> entity;    // 持ち主
	uint32_t playHandle = 0;  // 鳴っている音（SoundManager の再生ハンドル。0は鳴っていない）
	uint32_t soundHandle = 0; // 鳴らした音の素材（Inspector で音を変えたら止める）
	float volume = 1.0f;      // SoundManager に最後に伝えた音量（変わったときだけ伝える。SoundManager は呼ぶたびにログを出すので）
	float pitch = 1.0f;       // 最後に伝えたピッチ
	float gain = 1.0f;        // 最後に伝えた3Dの大きさ（3Dでない音は1のまま）
	float pan = 0.0f;         // 最後に伝えた3Dの左右（3Dでない音は0のまま）
};

/// <summary>
/// AudioSystem が持つ物（Initialize で作り、Release で消す）
/// </summary>
struct AudioState {
	// 見つけた AudioSource の一覧。鳴っていなくても持つ（「もう始まった」の印。無いと Play On Start が毎フレーム鳴り直す）
	std::vector<SourceState> sources;
	// 聞く人（ゲームのカメラ）。3Dの音の距離と左右の基準
	bool hasListener = false;
	Vector3 listenerPosition = {0.0f, 0.0f, 0.0f};
	Vector3 listenerRight = {1.0f, 0.0f, 0.0f};
	// SoundManager に最後に伝えた、種類ごとの音量（-1 は「まだ伝えていない」）
	float masterVolume = -1.0f;
	std::array<float, kBusCount> busVolumes;
	// 試し聞きの音（0は鳴っていない）
	uint32_t previewHandle = 0;
};
std::unique_ptr<AudioState> state;

// 範囲に収めた音量・ピッチ（ファイルを手で書き換えて範囲の外になっても、XAudio2 に変な値を渡さない）
float ClampVolume(float volume) { return std::clamp(volume, 0.0f, kMaxVolume); }
float ClampPitch(float pitch) { return std::clamp(pitch, kMinPitch, kMaxPitch); }

// 種類の名前（Parameters の項目名。"BGM" / "SE"）
std::string BusName(AudioBus bus) { return std::string(magic_enum::enum_name(bus)); }

// Entityの状態を探す（無ければ nullptr）。AudioSource の数しか無いので、順に見れば足りる
SourceState* FindSource(Handle<Entity> entity) {
	const auto it = std::ranges::find(state->sources, entity, &SourceState::entity);
	return (it != state->sources.end()) ? &*it : nullptr;
}

// Entityの状態を探し、無ければ足す（ゲームの Play が、そのEntityを Update が見つけるより先だったとき）
SourceState& FindOrAddSource(Handle<Entity> entity) {
	if (SourceState* source = FindSource(entity)) {
		return *source;
	}
	state->sources.push_back({entity});
	return state->sources.back();
}

//=============================================================================
// 3Dの音
//=============================================================================
// 聞く人（カメラ）の位置と右の向き。カメラの行列の4行目が位置、1行目が右の向き
void UpdateListener(const Camera* listener) {
	state->hasListener = (listener != nullptr);
	if (listener == nullptr) {
		return;
	}
	const Matrix4x4 cameraMatrix = MakeAffineMatrix({1.0f, 1.0f, 1.0f}, listener->GetRotation(), listener->GetTranslation());
	state->listenerPosition = listener->GetTranslation();
	state->listenerRight = Normalize(Vector3{cameraMatrix.m[0][0], cameraMatrix.m[0][1], cameraMatrix.m[0][2]});
}

// 3Dの音の大きさ（gain）と左右（pan）を決める。3Dでない音・聞く人がいないときは 1 と 0（普通の音と同じ）
void CalcSpatial(Handle<Entity> entity, const AudioSourceComponent& component, float& gain, float& pan) {
	gain = 1.0f;
	pan = 0.0f;
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	if (!component.spatial || !state->hasListener || transform == nullptr) {
		return;
	}
	const Vector3 toSource = GetWorldPosition(*transform) - state->listenerPosition;
	const float distance = Length(toSource);
	// 大きさ：minDistance までは1、maxDistance で0、その間はまっすぐ下がる
	const float minDistance = (std::max)(component.minDistance, 0.0f);
	const float maxDistance = (std::max)(component.maxDistance, minDistance + 0.001f); // 0で割らないように、min より少しは遠くする
	gain = 1.0f - std::clamp((distance - minDistance) / (maxDistance - minDistance), 0.0f, 1.0f);
	// 左右：音のある向きと、カメラの右の向きの内積（真右で1、真左で-1、正面・真後ろ・真上で0）
	if (distance > 1e-4f) {
		pan = Dot(toSource / distance, state->listenerRight);
	}
}

// 鳴っている音に3Dの大きさ・左右を伝える（変わったときだけ）
void UpdateSpatial(SourceState& source, const AudioSourceComponent& component) {
	float gain = 1.0f;
	float pan = 0.0f;
	CalcSpatial(source.entity, component, gain, pan);
	if (gain != source.gain || pan != source.pan) {
		source.gain = gain;
		source.pan = pan;
		SoundManager::SetSpatial(source.playHandle, gain, pan);
	}
}

//=============================================================================
// 鳴らす・止める
//=============================================================================
// 鳴っていれば止める（鳴り終わった・StopAll で止まった音には何もしない＝SoundManager に警告を出させない）
void StopSource(SourceState& source) {
	if (SoundManager::IsPlaying(source.playHandle)) {
		SoundManager::Stop(source.playHandle);
	}
	source.playHandle = 0;
}

// 頭から鳴らす（鳴っていたら止めてから）。音が未選択なら鳴らさない
bool StartSource(SourceState& source, const AudioSourceComponent& component) {
	StopSource(source);
	if (component.soundHandle == 0) {
		return false;
	}
	source.soundHandle = component.soundHandle;
	source.volume = ClampVolume(component.volume);
	source.pitch = ClampPitch(component.pitch);
	source.playHandle = SoundManager::Play(source.soundHandle, component.loop, source.volume, source.pitch, component.bus);
	// 作ったばかりの音は、大きさ1・真ん中。3Dの音なら、最初の1フレームから距離の大きさで鳴らす
	source.gain = 1.0f;
	source.pan = 0.0f;
	UpdateSpatial(source, component);
	return source.playHandle != 0;
}

// 鳴っている音に、Inspector などで変えた値を伝える
void ApplyChanges(SourceState& source, const AudioSourceComponent& component) {
	// 鳴り終わった（ループしない音が最後まで鳴った。SoundManager::CleanupSourceVoices が片付けた）
	if (!SoundManager::IsPlaying(source.playHandle)) {
		source.playHandle = 0;
		return;
	}
	// 音そのものを変えた → 止める（前の音のまま鳴り続けないように）
	if (component.soundHandle != source.soundHandle) {
		StopSource(source);
		return;
	}
	// 音量・ピッチ（変わったときだけ伝える）
	const float volume = ClampVolume(component.volume);
	if (volume != source.volume) {
		source.volume = volume;
		SoundManager::SetVolume(source.playHandle, volume);
	}
	const float pitch = ClampPitch(component.pitch);
	if (pitch != source.pitch) {
		source.pitch = pitch;
		SoundManager::SetPitch(source.playHandle, pitch);
	}
	// 3Dの音（動いた・カメラが動いた・Spatial を切り替えた）
	UpdateSpatial(source, component);
}

//=============================================================================
// 種類ごとの音量
//=============================================================================
// Parameters の値を SoundManager に伝える（変わったときだけ）
void ApplyVolumes() {
	GlobalVariables* gv = GlobalVariables::GetInstance();
	const float master = ClampVolume(gv->Get<float>(kVolumeGroup, kVolumeCategory, kMasterName));
	if (master != state->masterVolume) {
		state->masterVolume = master;
		SoundManager::SetMasterVolume(master);
	}
	for (AudioBus bus : magic_enum::enum_values<AudioBus>()) {
		const float volume = ClampVolume(gv->Get<float>(kVolumeGroup, kVolumeCategory, BusName(bus)));
		float& applied = state->busVolumes[static_cast<size_t>(bus)];
		if (volume != applied) {
			applied = volume;
			SoundManager::SetBusVolume(bus, volume);
		}
	}
}

//=============================================================================
// Inspector と 保存
//=============================================================================
// AudioSource の見せ方。Inspector とシーンファイルの両方がこの並びを使う（Collider と同じ書き方）
// 保存の名前はメンバ名にし、Inspector には「表示###保存の名前」で読みやすい名前を出す
void DescribeAudioSource(ComponentUI& ui, AudioSourceComponent& value) {
	ui.AssetField("Sound###sound", value.soundHandle, AssetType::Sound, Tip("resources の下の .wav / .mp3"));
	// 試し聞き（停止中でも鳴る。保存・読み込みのときは Button が false を返すので何も起きない）
	if (AudioSystem::IsPreviewing()) {
		if (ui.Button("■ Stop Preview")) {
			AudioSystem::StopPreview();
		}
	} else if (ui.Button("▶ Preview")) {
		AudioSystem::Preview(value);
	}
	ui.Field("Volume###volume", value.volume, Range(0.0f, kMaxVolume));
	ui.Field("Pitch###pitch", value.pitch, Range(kMinPitch, kMaxPitch).Tip("0.5で1オクターブ下、2で1オクターブ上"));
	ui.Field("Loop###loop", value.loop, Tip("最後まで鳴ったら頭から（BGM）。鳴っている途中で変えたときは、次に鳴らしたときから効く"));
	ui.Field("Play On Start###playOnStart", value.playOnStart, Tip("Play を押したとき（シーンが始まったとき）に鳴らす"));
	ui.Field("Bus###bus", value.bus, Tip("音の種類。種類ごとの音量は Parameters ウィンドウの Audio / Volume"));
	ui.Separator("3D");
	ui.Field("Spatial###spatial", value.spatial, Tip("離れると小さくなり、左右に振れる（聞く人はゲームのカメラ）"));
	ui.Field("Min Distance###minDistance", value.minDistance, AtLeast(0.0f).Tip("ここまで近いと元の大きさ"));
	ui.Field("Max Distance###maxDistance", value.maxDistance, AtLeast(0.0f).Tip("ここより遠いと聞こえない"));
}
} // namespace

//=============================================================================
// 初期化 / 解放
//=============================================================================
void AudioSystem::Initialize() {
	HTT_ASSERT_MSG(state == nullptr, "Initialize()が2回以上呼ばれています");
	state = std::make_unique<AudioState>();
	state->busVolumes.fill(-1.0f);

	// AudioSource の置き場所・保存・Inspector を登録する。見せ方は DescribeAudioSource 1つで、保存と Inspector の両方に使う
	EntityManager::RegisterComponent<AudioSourceComponent>();
	ComponentSerializer::Register<AudioSourceComponent>("Audio Source", &DescribeAudioSource);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<AudioSourceComponent>>("Audio Source", "Audio", &DescribeAudioSource, "音を鳴らす。鳴らすのは Play On Start か、Script の AudioSystem::Play・PlayOneShot"));

	// 種類ごとの音量を Parameters ウィンドウに出す（Save すると Parameters/Audio/Volume.json に残り、次の起動で読む）
	GlobalVariables* gv = GlobalVariables::GetInstance();
	GlobalVariables::CategoryBuilder volume = gv->Group(kVolumeGroup).Category(kVolumeCategory);
	volume.Add(kMasterName, 1.0f);
	for (AudioBus bus : magic_enum::enum_values<AudioBus>()) {
		volume.Add(BusName(bus), 1.0f);
	}
	gv->LoadGroup(kVolumeGroup);
}

// SoundManager はウィンドウと一緒に先に片付いているので、ここでは SoundManager を呼ばない
void AudioSystem::Release() { state.reset(); }

//=============================================================================
// 毎フレーム
//=============================================================================
void AudioSystem::Update(bool isGameRunning, const Camera* listener) {
	HTT_ASSERT_MSG(state != nullptr, "AudioSystem::Initialize()を先に呼んでください");
	std::vector<SourceState>& sources = state->sources;

	// ===== 種類ごとの音量・聞く人（停止中も。試し聞きにも効くように）=====
	ApplyVolumes();
	UpdateListener(listener);

	// ===== 停止中は鳴らさない（試し聞きは止めない）=====
	if (!isGameRunning) {
		for (SourceState& source : sources) {
			StopSource(source);
		}
		sources.clear(); // 次に Play したときは、始まりからやり直す
		return;
	}

	// ===== 1. 消えた・外した・無効になった AudioSource の音を止めて、一覧から外す =====
	const auto isGone = [](const SourceState& source) { return EntityManager::Get<AudioSourceComponent>(source.entity) == nullptr || !EntityManager::IsActiveInHierarchy(source.entity); };
	for (SourceState& source : sources) {
		if (isGone(source)) {
			StopSource(source);
		}
	}
	std::erase_if(sources, isGone);

	// ===== 2. 初めて見つけた AudioSource は一覧に足して、Play On Start なら鳴らす。見つけ済みの物は変更を伝える =====
	EntityManager::ForEach<AudioSourceComponent>([&sources](Handle<Entity> entity, const AudioSourceComponent& component) {
		if (!EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		if (SourceState* source = FindSource(entity)) {
			ApplyChanges(*source, component);
			return;
		}
		sources.push_back({entity});
		if (component.playOnStart) {
			StartSource(sources.back(), component);
		}
	});
}

//=============================================================================
// ゲームから
//=============================================================================
bool AudioSystem::Play(Handle<Entity> entity) {
	HTT_ASSERT_MSG(state != nullptr, "AudioSystem::Initialize()を先に呼んでください");
	const AudioSourceComponent* component = EntityManager::Get<AudioSourceComponent>(entity);
	// AudioSource が無い（RequestAdd した直後は、次のフレームまで付いていない）・無効なEntity
	if (component == nullptr || !EntityManager::IsActiveInHierarchy(entity)) {
		return false;
	}
	// 一覧に入れるので、この後の Update が Play On Start でもう一度鳴らすことはない
	return StartSource(FindOrAddSource(entity), *component);
}

void AudioSystem::Stop(Handle<Entity> entity) {
	HTT_ASSERT_MSG(state != nullptr, "AudioSystem::Initialize()を先に呼んでください");
	if (SourceState* source = FindSource(entity)) {
		StopSource(*source);
	}
}

bool AudioSystem::IsPlaying(Handle<Entity> entity) {
	HTT_ASSERT_MSG(state != nullptr, "AudioSystem::Initialize()を先に呼んでください");
	const SourceState* source = FindSource(entity);
	return source != nullptr && SoundManager::IsPlaying(source->playHandle);
}

bool AudioSystem::PlayOneShot(Handle<Entity> entity) {
	HTT_ASSERT_MSG(state != nullptr, "AudioSystem::Initialize()を先に呼んでください");
	const AudioSourceComponent* component = EntityManager::Get<AudioSourceComponent>(entity);
	if (component == nullptr || component->soundHandle == 0) {
		return false;
	}
	// 一覧には入れない（持ち主が消えても止めない）。ループはしない（止める手段が StopAll しか無いので）
	const uint32_t playHandle = SoundManager::Play(component->soundHandle, false, ClampVolume(component->volume), ClampPitch(component->pitch), component->bus);
	// 3Dの音は、鳴らした瞬間の位置で大きさと左右を決める（一覧に無いので、この後は動かない）
	float gain = 1.0f;
	float pan = 0.0f;
	CalcSpatial(entity, *component, gain, pan);
	if (component->spatial) {
		SoundManager::SetSpatial(playHandle, gain, pan);
	}
	return playHandle != 0;
}

//=============================================================================
// 種類ごとの音量
//=============================================================================
// Parameters の値を書き換える（SoundManager へは次の Update で伝わる）
void AudioSystem::SetBusVolume(AudioBus bus, float volume) { GlobalVariables::GetInstance()->Set<float>(kVolumeGroup, kVolumeCategory, BusName(bus), volume); }
float AudioSystem::GetBusVolume(AudioBus bus) { return GlobalVariables::GetInstance()->Get<float>(kVolumeGroup, kVolumeCategory, BusName(bus)); }
void AudioSystem::SetMasterVolume(float volume) { GlobalVariables::GetInstance()->Set<float>(kVolumeGroup, kVolumeCategory, kMasterName, volume); }
float AudioSystem::GetMasterVolume() { return GlobalVariables::GetInstance()->Get<float>(kVolumeGroup, kVolumeCategory, kMasterName); }

//=============================================================================
// エディタの再生コントロールから
//=============================================================================
void AudioSystem::StopAll() {
	HTT_ASSERT_MSG(state != nullptr, "AudioSystem::Initialize()を先に呼んでください");
	SoundManager::StopAll();
	state->sources.clear(); // 作り直した後に見つけた AudioSource は「始まったばかり」になる（Play On Start がまた効く）
	state->previewHandle = 0;
}

void AudioSystem::SetPaused(bool paused) {
	HTT_ASSERT_MSG(state != nullptr, "AudioSystem::Initialize()を先に呼んでください");
	SoundManager::SetPaused(paused);
}

//=============================================================================
// 試し聞き
//=============================================================================
void AudioSystem::Preview(const AudioSourceComponent& source) {
	HTT_ASSERT_MSG(state != nullptr, "AudioSystem::Initialize()を先に呼んでください");
	StopPreview();
	if (source.soundHandle == 0) {
		return;
	}
	state->previewHandle = SoundManager::Play(source.soundHandle, false, ClampVolume(source.volume), ClampPitch(source.pitch), source.bus);
}

void AudioSystem::StopPreview() {
	HTT_ASSERT_MSG(state != nullptr, "AudioSystem::Initialize()を先に呼んでください");
	if (SoundManager::IsPlaying(state->previewHandle)) {
		SoundManager::Stop(state->previewHandle);
	}
	state->previewHandle = 0;
}

bool AudioSystem::IsPreviewing() {
	HTT_ASSERT_MSG(state != nullptr, "AudioSystem::Initialize()を先に呼んでください");
	return SoundManager::IsPlaying(state->previewHandle);
}