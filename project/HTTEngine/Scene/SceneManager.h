#pragma once
#include "HTTEngine/Camera/Camera.h"
#include "HTTEngine/Scene/IScene.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// 実行状態
enum class PlayState {
	Editing, // 停止中。Updateを回さない
	Playing, // 再生中
	Paused,  // 一時停止
};


/// <summary>
/// シーン一括管理クラス
/// <para>Entityを作り直す・シーンを切り替える・保存する・開くのは、全部 Update の頭（フレームの境目）で行う</para>
/// <para>「開いているシーン」＝エディタで編集しているシーン。Play はここから始まり、Stop でここ（Playを押した瞬間の状態）へ戻る</para>
/// </summary>
class SceneManager {
public:
	// 最初のシーンを作る。openScene は「開いているシーン」の作り方（Stop・Restart で作り直すときにも使う）
	void Initialize(SceneFactory openScene);
	void Update();
	void Draw();
	void Finalize(); // アプリの終了時に1回。今のシーンの Finalize を呼ぶ

	// ===== シーンの切り替え（ゲームのコードから。どこからでも呼べる）=====
	// 次のフレームの頭で、name のシーン（resources/scenes/<name>.scene.json）へ切り替える。切り替わるのは今 Update しているウィンドウ
	// 再生中だけ効く（停止中は何もしない）。シーンが無ければ Log に警告して false（切り替えない）
	static bool LoadScene(const std::string& name);
	// 今のシーンを最初からやり直す（次のフレームの頭で。再生中だけ効く）。ゲームの「やり直し」用
	// Play を押したシーンなら、Play を押した瞬間の状態に戻す（エディタで保存していない編集も、そのまま残る）
	// LoadScene で切り替えた先なら、そのシーンファイルから読み直す
	static bool ReloadScene();
	// name のシーンがあるか（シーンファイルがある、または REGISTER_SCENE で登録した名前）
	static bool HasScene(const std::string& name);
	// name のシーンを作る（REGISTER_SCENE した名前ならそのクラス、無ければ DefaultScene）。Entity はまだ作らない
	static std::unique_ptr<IScene> CreateScene(const std::string& name);
	// resources/scenes/ にあるシーンの名前（名前の順）
	static std::vector<std::string> GetSceneNames();

	// ===== シーンファイルを作る・開く（エディタから。開く・名前を付けて保存は停止中だけ）=====
	// resources/scenes/<name>.scene.json を中身が空で作る（開くと CreateDefaultEntities のカメラと平行光源から始まる）
	// 名前に使えない文字がある（FileNameRule.h）・同じ名前がもうある・作れなかったら、Log に出して false
	static bool CreateSceneFile(const std::string& name);
	// 次のフレームの頭で、name のシーンを開く（保存していない変更は消える。先に聞くのはエディタの SceneDialogs）
	void RequestOpenScene(const std::string& name) { openRequestName_ = name; }
	// 次のフレームの頭で、今の Entity を name のシーンとして保存し、そのシーンを開き直す（名前を付けて保存）
	void RequestSaveAs(const std::string& name) { saveAsRequestName_ = name; }
	// 保存した（開いた）時点から Entity が変わっているか（Control・Project ウィンドウの *）
	bool IsDirty() const { return isDirty_; }

	// ===== 再生コントロール =====
	void Play();  // 停止中なら、今のEntityを退避してから再生
	void Pause(); // 一時停止 / 再開
	void Stop();  // 停止して、開いているシーンの「Playを押した瞬間の状態」へ戻す
	// 作り直す。再生中はPlayを押した瞬間の状態から、停止中はシーンファイルから（保存していない編集は消える）
	void RequestReload() { isReloadRequested_ = true; }
	// シーンファイルに保存する（停止中だけ。実際に書くのは次のフレームの頭）
	void RequestSave() { isSaveRequested_ = true; }

	// ===== ゲッター =====
	IScene* GetCurrentScene() { return currentScene_.get(); }
	PlayState GetPlayState() const { return playState_; }
	const char* GetSceneFile() const { return currentScene_ ? currentScene_->GetSceneFile() : nullptr; } // シーンファイルを使わないならnullptr
	// 開いているシーンの名前（Playを押したシーン。再生中に切り替わっても、これは変わらない）
	const std::string& GetOpenSceneName() const { return openSceneName_; }
	// エディタが編集しているシーンマネージャ（最初に作ったウィンドウの物）。Project ウィンドウ・File メニューから使う
	static SceneManager* GetMain() { return main_; }

	// ===== エディタで最後に開いていたシーン（次の起動でそれを開く。Unity と同じ）=====
	// 覚えているシーンがまだあればその名前、無ければ fallback
	static std::string GetLastOpenedScene(const std::string& fallback);

	// ===== セッター =====
	void RequestNextScene(std::unique_ptr<IScene> next) { nextScene_ = std::move(next); } // 次のフレームの頭で切り替える
	void SetWindowTitle(const std::wstring& title) { windowTitle_ = title; }

private:
	// 開いているシーンを作り直す（Play・Stop・Restart）
	void ReloadImmediate();
	// シーンを入れ替える。useSnapshot なら、Playを押した瞬間に退避したEntityを使う
	void ChangeScene(std::unique_ptr<IScene> next, bool useSnapshot);
	// 新しいシーンのEntityを作る（退避 → シーンファイル → CreateDefaultEntities の順に探す）
	void LoadEntities(bool useSnapshot);
	// シーンファイルに書く。書けたら true
	bool SaveImmediate();
	// 開いているシーンを name に替える（Stop・Restart でもこのシーンに戻るようになる）
	void OpenImmediate(const std::string& name);
	// 今の Entity を name のシーンファイルに書いて、そのシーンを開き直す
	void SaveAsImmediate(const std::string& name);
	// 停止中か（シーンファイルを書く・開くのは停止中だけ）。違えば what を Log に出して false
	bool CanEditFiles(const std::string& what) const;
	// 今の Entity を「保存した状態」として覚える（開いた・保存した直後）
	void MarkSaved();
	// 保存した状態と今の Entity を比べ直す（履歴が動いたフレームだけ）
	void UpdateDirty();

	std::unique_ptr<IScene> currentScene_;
	std::unique_ptr<IScene> nextScene_; // 次のフレームの頭で切り替えるシーン（NextScene() が返した物）
	std::string nextSceneName_;         // 次のフレームの頭で切り替えるシーンの名前（LoadScene で頼まれた物）
	SceneFactory openSceneFactory_;     // 開いているシーンの作り方（Stop・Restart で作り直す）
	std::string openSceneName_;         // 開いているシーンの名前
	std::wstring windowTitle_;
#ifdef USE_IMGUI
	PlayState playState_ = PlayState::Editing; // エディタでは停止状態から始める
#else
	PlayState playState_ = PlayState::Playing; // Release は即座に動かす
#endif
	bool isReloadRequested_ = false;
	bool isSnapshotRequested_ = false; // 次に作り直す前に、今のEntityを退避する（Playを押した）
	bool isSaveRequested_ = false;
	std::string playSnapshot_; // Playを押した瞬間の全Entity（シーンファイルと同じJSON）。空なら退避なし

	// ===== エディタから頼まれたこと（次のフレームの頭で行う）=====
	std::string openRequestName_;   // 開くシーン（RequestOpenScene）
	std::string saveAsRequestName_; // 名前を付けて保存するシーン（RequestSaveAs）

	// ===== 保存していない変更（*）=====
	std::string savedSceneText_;      // 保存した（開いた）時点の全 Entity（SaveToText の形）。今と比べる
	uint64_t checkedChangeCount_ = 0; // 最後に比べたときの EditorHistory の変更回数（同じなら比べ直さない）
	bool isDirty_ = false;

	// ===== LoadScene の行き先（static 関数からは this が無いので、ここで覚える）=====
	static SceneManager* updating_; // 今 Update しているシーンマネージャ（Update の中だけ入っている）
	static SceneManager* main_;     // 最初に作ったシーンマネージャ（Update の外で LoadScene を呼ばれたときの行き先）
};