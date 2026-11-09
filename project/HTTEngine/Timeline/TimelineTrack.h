#pragma once
#include <algorithm>
#include <cstddef>
#include <memory>
#include <set>
#include <span>
#include <vector>
#include <string>
#include <string_view>
#include <type_traits>

#include <externals/nlohmann/json.hpp>

#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/Curve/Curve.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Math/Vector4.h"
#include "HTTEngine/Time/EngineTime.h"
#include "HTTEngine/Timeline/TimelineEvent.h"
#include "HTTEngine/Timeline/TimelinePlayhead.h"

// Timeline のトラックを書くための基底クラス。ゲーム側で新しい種類を足すときは、このヘッダ1つをインクルードする
//
//   // FlashTrack.cpp
//   struct FlashClip : TimelineClip {
//       float strength = 1.0f;
//       void Fields(ComponentUI& ui) { TimelineClip::Fields(ui); ui.Field("Strength###strength", strength); }
//   };
//   class FlashTrack : public TimelineTrackOf<FlashClip> {
//   public:
//       void Evaluate(const TimelineFrame& frame, TimelineTrackState& state) const override { ... }
//   };
//   TIMELINE_TRACK(FlashTrack, "画面を光らせる"); // これでファイルの "type": "FlashTrack" から作れる
//
// トラックは Timeline のファイル1つにつき1つで、再生している全部の Entity が同じ物を見る。Evaluate は const で、再生中に書き換えない
// Entity ごとに変わる値は TimelineTrackState を継承して持つ

/// <summary>
/// 実行中に渡した値1つ（TimelineSystem::SetParameter。Tween の Parameter が名前で読む）
/// </summary>
struct TimelineParameter {
	std::string name;
	Vector4 value;
};

/// <summary>
/// 実行中に差し替えた対象1つ（TimelineSystem::SetTargetOverride）
/// </summary>
struct TimelineTargetOverride {
	std::string path;      // トラックの Target に書いてある文字
	Handle<Entity> target; // 代わりに動かす Entity
};

/// <summary>
/// 再生する Entity に、実行中に渡した値・差し替えた対象（同じ Entity の全部の層の再生が見る）
/// </summary>
struct TimelineOverrides {
	Handle<Entity> owner;                        // 渡した先の、再生する Entity（対象の差し替えは、この Entity を再生する Entity にしたトラックだけに効く）
	std::vector<TimelineParameter> parameters;   // 渡した値
	std::vector<TimelineTargetOverride> targets; // 差し替えた対象

	// 名前の値（無ければ nullptr）
	const Vector4* FindParameter(std::string_view name) const;
	// path の代わりの Entity（無ければ nullptr）
	const Handle<Entity>* FindTarget(std::string_view path) const;
};

/// <summary>
/// 1フレーム分の再生の様子（トラックの Evaluate に渡す）
/// </summary>
struct TimelineFrame {
	Handle<Entity> owner;                         // 再生している Entity
	float time = 0.0f;                            // 今の時間（秒）
	std::span<const TimelineRange> ranges;        // このフレームに通った時間の幅（シーク・停止で値を書き直すだけのときは空）
	std::vector<TimelineEvent>* events = nullptr; // 呼ぶイベントを足す場所（全部の値を書いた後にまとめて呼ばれる）
	TimeMode timeMode = TimeMode::Game;           // 再生に使っている時間（Timeline Component の Time Mode）
	bool isPreview = false;                       // エディタの停止中のプレビュー
	const TimelineOverrides* overrides = nullptr; // 実行中に渡した値・差し替えた対象（無ければ nullptr。プレビューも nullptr）
};

/// <summary>
/// Component の1つの項目の指し方（値のトラック・Tween のトラックが持つ。Timeline ウィンドウの Component・Property の欄に使う）
/// </summary>
struct TimelinePropertyBinding {
	std::string component = "Transform";  // Component の名前（Inspector の見出しと同じ。Script の名前でもよい）
	std::string property = "translation"; // 項目の名前

	void Fields(ComponentUI& ui);
};

/// <summary>
/// クリップの両端の補間の長さ（Timeline ウィンドウの列に斜めの線で見せる）
/// </summary>
struct TimelineClipFade {
	float in = 0.0f;  // 始まりの補間の秒
	float out = 0.0f; // 終わった後の補間の秒
};


/// <summary>
/// トラック1つの、Entity ごとの値。増やしたいときは継承して、CreateState で作る
/// </summary>
class TimelineTrackState {
public:
	virtual ~TimelineTrackState() = default;

	Handle<Entity> target;       // 見つけた対象（FindTarget が覚えておく）
	size_t searchedEntities = 0; // 対象を探したときの Entity の数（増減していなければ、探し直さない）
	bool isSearched = false;     // 1回でも探した
	bool isWarned = false;       // 対象についての警告を出した（1回だけ出す）
};


/// <summary>
/// トラック（時間に沿って1つの物を動かす列）の基底クラス
/// <para>持つ物：名前・対象・クリップ。動かし方は種類ごとに Evaluate へ書く</para>
/// </summary>
class TimelineTrack {
public:
	virtual ~TimelineTrack() = default;

	std::string typeName;       // 種類の名前（TimelineTrackRegistry::Create が入れる。ファイルの "type"）
	std::string name = "Track"; // トラックの名前（確認の画面・警告に出す）
	std::string target;         // 対象。再生する Entity から子の名前を / でつないだパス（空なら再生する Entity 自身。先頭が / ならシーンの一番上から）
	bool isMuted = false;       // true の間は動かさない

	// ===== 見せ方（Inspector とファイルの両方がこの並びを使う）=====
	void DescribeCommon(ComponentUI& ui);
	// 種類ごとの設定（使わなければ書かなくてよい）
	virtual void Fields(ComponentUI& /*ui*/) {}

	// ===== クリップ（TimelineTrackOf が書いてくれる）=====
	virtual void WriteClips(SceneJson& out, std::set<std::string>& warnings) const = 0;
	virtual void ReadClips(const SceneJson& in, std::set<std::string>& warnings) = 0;
	virtual size_t GetClipCount() const = 0;
	virtual float GetEndTime() const = 0; // 一番遅いクリップの終わり（秒）

	// ===== クリップを直す（Timeline ウィンドウ。クリップの型を知らずに直せるように、TimelineTrackOf が書いてくれる）=====
	virtual float GetClipStart(size_t index) const = 0;
	virtual float GetClipDuration(size_t index) const = 0;
	// クリップに長さがあるか（false はイベントのような1点）
	virtual bool HasClipDuration() const = 0;
	// 足す。並びが変わるので、足した場所の番号を返す
	virtual size_t AddClip(float start, float duration) = 0;
	virtual void RemoveClip(size_t index) = 0;
	// 時間を変える。並びが変わるので、新しい番号を返す
	virtual size_t SetClipTime(size_t index, float start, float duration) = 0;
	// クリップの項目を描く（Fields を呼ぶ）。時間を打ち換えると並びが変わるので、新しい番号を返す
	virtual size_t DescribeClip(size_t index, ComponentUI& ui) = 0;
	// クリップの持つカーブ（カーブを持つ種類だけ書く）。書くと Timeline ウィンドウでキーを直せる
	virtual std::span<Curve> GetClipCurves(size_t /*index*/) { return {}; }

	// クリップの上に出す名前（無ければ空）
	virtual std::string GetClipLabel(size_t /*index*/) const { return {}; }
	// クリップの両端の補間（Timeline ウィンドウの見た目だけ）
	virtual TimelineClipFade GetClipFade(size_t /*index*/) const { return {}; }
	// 対象（Target の欄）を使うか。クリップごとに相手を持つ種類・相手の要らない種類は false にする
	virtual bool UsesTarget() const { return true; }
	// クリップの持つ相手の問題（無ければ空。Timeline ウィンドウの行に出す。UsesTarget が false の種類だけ呼ばれる）
	virtual std::string FindClipProblem(Handle<Entity> /*owner*/) const { return {}; }
	// プレビューで Evaluate を呼んでよいか。true にするなら、Component の値は ComponentSerializer::SetValue で書く（書いた値を元へ戻せるのは SetValue だけ）
	virtual bool CanPreview() const { return false; }
	// 時間が止まっている間も、毎フレーム Evaluate を呼ぶか（カメラの切り替えのように、毎フレーム頼む種類）。再生していない（Stopped）間は呼ばない
	virtual bool EvaluatesEveryFrame() const { return false; }
	// Component の1つの項目を動かす種類なら、その指し方（Timeline ウィンドウが Component・Property の欄を出す）。違えば nullptr
	virtual TimelinePropertyBinding* GetPropertyBinding() { return nullptr; }
	virtual const TimelinePropertyBinding* GetPropertyBinding() const { return nullptr; }

	// ===== 再生 =====
	// Entity ごとの値を作る（再生を始めるときに1回）
	virtual std::unique_ptr<TimelineTrackState> CreateState() const { return std::make_unique<TimelineTrackState>(); }
	/// <summary>
	/// 今の時間の分だけ動かす。再生中は毎フレーム、シーク・停止のときは ranges を空にして呼ばれる
	/// </summary>
	virtual void Evaluate(const TimelineFrame& frame, TimelineTrackState& state) const = 0;

protected:
	/// <summary>
	/// 対象の Entity を探す（見つけたら state に覚える）
	/// <para>見つからなければ、警告を1回出して無効な Handle を返す。Entity の数が変わったら探し直す</para>
	/// </summary>
	Handle<Entity> FindTarget(const TimelineFrame& frame, TimelineTrackState& state) const;
	// 警告を1回だけ出す（state.isWarned）
	void WarnOnce(TimelineTrackState& state, std::string_view message) const;
};

/// <summary>
/// クリップ（トラックの上に置く、時間の区間）の共通の値。継承して、種類ごとの値を足す
/// <para>Fields に足した値はファイルへ書かれる。ComponentUI で書けない値は WriteExtra・ReadExtra で書く</para>
/// </summary>
struct TimelineClip {
	static constexpr bool kHasDuration = true; // 長さがあるか（イベントのような1点なら、継承先で false にする）
	float start = 0.0f;    // 始まる時間（秒）
	float duration = 0.0f; // 長さ（秒）

	float GetEnd() const { return start + duration; }
	void Fields(ComponentUI& ui);
	void WriteExtra(SceneJson& /*out*/) const {}
	// 読んだ項目は in から消す（残すと「読む所がない」と警告が出る）
	void ReadExtra(SceneJson& /*in*/, std::set<std::string>& /*warnings*/) {}
};


/// <summary>
/// Clip の並びを持つトラック。クリップのファイルの読み書きと、時間からの検索を持っている
/// </summary>
template<class Clip> 
class TimelineTrackOf : public TimelineTrack {
	static_assert(std::is_base_of_v<TimelineClip, Clip>, "クリップは TimelineClip を継承してください");

public:
	std::vector<Clip> clips; // 始まる時間の順に並ぶ

	void WriteClips(SceneJson& out, std::set<std::string>& warnings) const override {
		out = SceneJson::array();
		for (Clip clip : clips) { // 見せ方の関数は書き換えられる参照を受け取るので、写しを渡す
			SceneJson item;
			ComponentSerializer::WriteFields(typeName, [&clip](ComponentUI& ui) { clip.Fields(ui); }, item, warnings);
			clip.WriteExtra(item);
			out.push_back(std::move(item));
		}
	}

	void ReadClips(const SceneJson& in, std::set<std::string>& warnings) override {
		clips.clear();
		for (const SceneJson& item : in) {
			if (!item.is_object()) {
				warnings.insert("\"clips\" の中に、クリップではない物があります（読み飛ばします）");
				continue;
			}
			SceneJson fields = item; // ReadExtra が読んだ項目を消すので写す
			Clip clip;
			clip.ReadExtra(fields, warnings);
			ComponentSerializer::ReadFields(typeName, [&clip](ComponentUI& ui) { clip.Fields(ui); }, fields, warnings);
			Normalize(clip);
			clips.push_back(std::move(clip));
		}
		std::ranges::stable_sort(clips, {}, &Clip::start);
	}

	size_t GetClipCount() const override { return clips.size(); }

	float GetEndTime() const override {
		float end = 0.0f;
		for (const Clip& clip : clips) {
			end = (std::max)(end, clip.GetEnd());
		}
		return end;
	}

	float GetClipStart(size_t index) const override { return clips[index].start; }
	float GetClipDuration(size_t index) const override { return clips[index].duration; }
	bool HasClipDuration() const override { return Clip::kHasDuration; }

	size_t AddClip(float start, float duration) override {
		Clip clip;
		clip.start = start;
		clip.duration = duration;
		return Insert(std::move(clip));
	}

	void RemoveClip(size_t index) override { clips.erase(clips.begin() + index); }

	size_t SetClipTime(size_t index, float start, float duration) override {
		Clip clip = std::move(clips[index]);
		clips.erase(clips.begin() + index);
		clip.start = start;
		clip.duration = duration;
		return Insert(std::move(clip));
	}

	size_t DescribeClip(size_t index, ComponentUI& ui) override {
		clips[index].Fields(ui);
		return SetClipTime(index, clips[index].start, clips[index].duration);
	}

protected:
	// time 以前に始まる、最後のクリップ（無ければ nullptr）
	const Clip* FindClip(float time) const {
		const auto next = std::ranges::upper_bound(clips, time, {}, &Clip::start);
		return (next == clips.begin()) ? nullptr : &*(next - 1);
	}

	// このフレームに通った時間の幅の中で始まるクリップを、順に function へ渡す
	template<class Function> void ForEachStarted(std::span<const TimelineRange> ranges, Function&& function) const {
		for (const TimelineRange& range : ranges) {
			const auto first = range.includesStart ? std::ranges::lower_bound(clips, range.start, {}, &Clip::start) : std::ranges::upper_bound(clips, range.start, {}, &Clip::start);
			const auto last = std::ranges::upper_bound(clips, range.end, {}, &Clip::start);
			for (auto clip = first; clip < last; ++clip) {
				function(*clip);
			}
		}
	}

private:
	// 時間を 0 以上にする（比べ方を逆にしないのは、NaN もここで 0 にするため）
	static void Normalize(Clip& clip) {
		clip.start = (clip.start >= 0.0f) ? clip.start : 0.0f;
		clip.duration = (Clip::kHasDuration && clip.duration >= 0.0f) ? clip.duration : 0.0f;
	}

	// 時間の順を守って入れる（同じ時間のクリップの後ろ）
	size_t Insert(Clip clip) {
		Normalize(clip);
		const auto position = std::ranges::upper_bound(clips, clip.start, {}, &Clip::start);
		const auto inserted = clips.insert(position, std::move(clip));
		return static_cast<size_t>(inserted - clips.begin());
	}
};

/// <summary>
/// トラックの種類の表（名前 → 作り方）。TIMELINE_TRACK(...) とエンジンの TimelineSystem::Initialize が登録する
/// </summary>
class TimelineTrackRegistry {
public:
	using CreateFunction = std::unique_ptr<TimelineTrack> (*)();

	static void Register(const char* name, CreateFunction create, const char* description);
	// 名前から作る（知らない名前なら nullptr）
	static std::unique_ptr<TimelineTrack> Create(std::string_view name);
	// 名前の一覧（GetDisplayName の順）
	static std::vector<std::string> GetNames();
	// 説明（知らない名前・書いていなければ空）
	static const std::string& GetDescription(std::string_view name);
	// ウィンドウに出す名前（頭の "Timeline" を省く。"TimelineValueTrack" → "ValueTrack"。ファイルの "type" は元の名前のまま）
	static std::string_view GetDisplayName(std::string_view name);
};

/// <summary>
/// TIMELINE_TRACK(...) が1つ作る物。作られたときに表へ登録する
/// </summary>
template<class T> 
class TimelineTrackRegistrar {
	static_assert(std::is_base_of_v<TimelineTrack, T>, "TIMELINE_TRACK(...) に書けるのは、TimelineTrack を継承したクラスだけです");
	static_assert(std::is_default_constructible_v<T>, "トラックは引数の無いコンストラクタで作れるようにしてください（エンジンが名前から作るため）");

public:
	TimelineTrackRegistrar(const char* name, const char* description) { TimelineTrackRegistry::Register(name, &Create, description); }

private:
	static std::unique_ptr<TimelineTrack> Create() { return std::make_unique<T>(); }
};


/// <summary>
/// トラックの種類を登録する。.cpp に TIMELINE_TRACK(クラス名); か TIMELINE_TRACK(クラス名, "説明"); と1行書く
/// <para>クラス名がそのまま、ファイルの "type" になる（変えると、前に保存したファイルのトラックは読めなくなる）</para>
/// <para>必ずゲーム側の .cpp に書く（エンジンの .lib の中に書くと、誰も参照しないのでリンカに捨てられる）</para>
/// </summary>
// "" __VA_ARGS__ は、説明を書かなければ ""、書けば "" "説明" ＝ "説明"（並んだ文字列はつながる。ATTACK_PART と同じ）
#define TIMELINE_TRACK(Type, ...) inline const TimelineTrackRegistrar<Type> timelineTrackRegistrarOf_##Type(#Type, "" __VA_ARGS__)