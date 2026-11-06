#include "Source/Scripts/ClipCycler.h"

#include <format>
#include <string>
#include <string_view>

SCRIPT(ClipCycler, "Test");

//=============================================================================
// 毎フレーム
//=============================================================================
// ===== 始め =====
// 最後の欄を今の物にしておくと、PlayNext が最初の欄から流す
void ClipCycler::Start() {
	current_ = kClipCount - 1;
	PlayNext();
}

// ===== 毎フレーム =====
void ClipCycler::Update() {
	timer_ -= Time::GetDeltaTime();
	if (timer_ <= 0.0f) {
		PlayNext();
	}
}

// ===== 次のアニメ =====
void ClipCycler::PlayNext() {
	timer_ = secondsPerClip_;
	for (size_t step = 0; step < kClipCount; ++step) {
		current_ = (current_ + 1) % kClipCount;
		const std::string_view clip = clips_[current_].View();
		if (clip.empty()) {
			continue; // 空の欄は飛ばす（全部空なら何もしない）
		}
		// 長さが違うアニメも、ちょうど Seconds Per Clip 秒で流しきる（名前が Animator のファイルに無ければ、Play が警告して false）
		const float speed = AnimationSystem::CalcSpeedToFit(gameObject.handle, clip, secondsPerClip_);
		AnimationSystem::Play(gameObject.handle, clip, false, speed);
		return;
	}
}


//=============================================================================
// Inspector と保存
//=============================================================================
void ClipCycler::Fields(ComponentUI& ui) {
	for (size_t i = 0; i < kClipCount; ++i) {
		const std::string label = std::format("Clip {}###clip{}", i + 1, i); // 保存の名前は clip0・clip1・clip2
		ui.ClipField(label.c_str(), clips_[i], Tip("順に流すアニメの名前（同じ Entity の Animator のファイルの中の名前から選ぶ。空なら飛ばす）"));
	}
	ui.Field("Seconds Per Clip###secondsPerClip", secondsPerClip_, AtLeast(0.01f).Tip("1つのアニメを流しきる秒（長さが違うアニメも、この秒に合わせる）"));
}
