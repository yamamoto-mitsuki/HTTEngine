#pragma once
#include <set>
#include<memory>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "MyEngine/ECS/ComponentSerializer.h"
#include "MyEngine/Timeline/TimelineTrack.h"

/// <summary>
/// Timeline（ファイル1つの中身）。トラックの数が変わるので Component には入れられず、ここに持つ
/// </summary>
struct Timeline {
	float length = 1.0f;                                // 長さ（秒）
	std::vector<std::unique_ptr<TimelineTrack>> tracks; // トラック（上から順に動かす）
	std::vector<std::string> unknownTracks;             // 知らない種類のトラック（JSON の文字のまま持ち、保存で消さない）

	// 再生する長さ（length と、一番遅いクリップの終わりの大きい方。クリップが切れないように）
	float GetDuration() const;
};

/// <summary>
/// Timeline のファイル（.timeline）を読む・持つ・保存する（CurveManager と同じ形）
/// <para>同じファイルを再生する Entity はみんな同じ Timeline を見る。Entity ごとの値は TimelineSystem が持つ</para>
/// </summary>
class TimelineManager {
public:
	static constexpr std::string_view kFileExtension = ".timeline"; // Timeline のファイルの拡張子

	static void Initialize();
	static void Release();

	/// <summary>
	/// ファイルを読む。同じファイルなら、前に読んだ物の番号を返す
	/// </summary>
	/// <returns>番号（1から）。読めなければ 0</returns>
	static uint32_t Load(const std::filesystem::path& filePath);

	// 番号からパスを引く（知らない番号なら空）
	static const std::string& GetPath(uint32_t handle);

	// 中身（0・知らない番号なら nullptr）。ポインタは使い捨て
	static const Timeline* Get(uint32_t handle);

	// 直すための中身（エディタから。直したら MarkChanged を呼ぶ）
	static Timeline* GetForEdit(uint32_t handle);
	// 中身を直したと知らせる。版が1つ増えて、再生中の Entity がトラックの値を作り直す
	static void MarkChanged(uint32_t handle);

	// 中身を JSON の文字にする（Undo の前後の値に使う。Timeline は unique_ptr を持っていて写せないので、文字で写す）
	static std::string ToText(uint32_t handle);
	// JSON の文字から中身を作り直す（ToText の逆。版も1つ増える。読めなければ何も変えず false）
	static bool ApplyText(uint32_t handle, const std::string& text);
	// 中身の版（TimelineSystem が、作り直すかを決めるのに使う）
	static uint32_t GetRevision(uint32_t handle);

	// ファイルに書く（前のファイルは .bak として1つ残る）
	static bool Save(uint32_t handle);

	// 新しいファイルを、既定の中身で作る（Project ウィンドウの Create → Timeline）。同じ名前のファイルがあれば作らない
	static bool CreateNewFile(const std::filesystem::path& filePath);

	// ===== JSON（ファイルの形は { "settings": {...}, "tracks": [{ "type", "settings", "clips" }] }）=====
	static SceneJson ToJson(const Timeline& timeline);
	// 読めない項目は warnings に書いて、既定の値のままにする
	static Timeline FromJson(const SceneJson& json, std::set<std::string>& warnings);

	// ===== 見せ方（Inspector とファイルの両方がこの並びを使う）=====
	static void DescribeLength(ComponentUI& ui, float& length);
};