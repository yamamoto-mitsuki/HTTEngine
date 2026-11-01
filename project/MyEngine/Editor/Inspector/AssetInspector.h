#pragma once
#include <filesystem>
#include <memory>
#include <string_view>

// 前方宣言
class ComponentUI;


/// <summary>
/// ファイル（アセット）1種類の中身を Inspector で直す係（Physic Material など）
/// <para>描くのは ComponentUI（Component の Inspector と同じ道具）なので、ここに ImGui は出てこない</para>
/// </summary>
class AssetEditor {
public:
	virtual ~AssetEditor() = default;

	// 受け持つファイルの拡張子（".physmat"。小文字で）
	virtual std::string_view GetExtension() const = 0;
	// Inspector の見出しに出す種類の名前（"Physic Material"）
	virtual std::string_view GetTypeName() const = 0;

	// ファイルを読めるか調べる（クリックで選ばれたときに1回だけ呼ばれる）。読めないと Draw は呼ばれない。ここで中身を読まない種類は書かなくてよい
	virtual bool CanLoad(const std::filesystem::path& filePath) {
		(void)filePath;
		return true;
	}

	/// <summary>
	/// 中身を描いて、直す（毎フレーム）
	/// </summary>
	/// <returns>中身を変えたか（変えたら、つまむ・打つのが終わったときに Save が呼ばれる）</returns>
	virtual bool Draw(const std::filesystem::path& filePath, ComponentUI& ui) = 0;

	// ファイルに書く
	virtual void Save(const std::filesystem::path& filePath) = 0;
};


/// <summary>
/// Project ウィンドウで選んだファイル（アセット）を、Inspector に出す（Unity と同じ）
/// <para>Hierarchy で Entity を選んでいるときは Entity が先。Project で直せるファイルを選ぶと、Entity の選択を外す</para>
/// <para>直せる種類は Register で足す（今は Physic Material だけ）。Release でも Register は呼べる（何も描かない）</para>
/// </summary>
class AssetInspector {
public:
	// ファイルの種類を足す（各 Manager の Initialize から）
	static void Register(std::unique_ptr<AssetEditor> editor);

	// そのファイルを直せる係がいるか（拡張子で見る）
	static bool CanEdit(const std::filesystem::path& filePath);

	// ファイルを選ぶ（Project ウィンドウから。直している途中の物があれば、先に保存する）
	static void Select(const std::filesystem::path& filePath);

	// 選んでいるファイルを、直せるか（Inspector がどちらを出すか決めるのに使う）
	static bool HasSelection();

	// Inspector の中身を描く（InspectorWindow から。エディタだけ）
	static void Draw();
};