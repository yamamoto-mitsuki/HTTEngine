#include "SceneManager.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <string_view>
#include <utility>

#include <externals/nlohmann/json.hpp>

#include "MyEngine/Core/AssetMoveRegistry.h"
#include "MyEngine/Diagnostics/LogManager.h"
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/ECS/GameComponent.h"
#include "MyEngine/Particle/ParticleManager.h"
#include "MyEngine/Scene/DefaultScene.h"
#include "MyEngine/Scene/SceneSerializer.h"
#include "MyEngine/UI/UIScreen.h"
#include "MyEngine/String/FileNameRule.h"
#include "MyEngine/Systems/Audio/AudioSystem.h"
#include "MyEngine/Systems/Camera/CameraSystem.h"
#include "MyEngine/Systems/Gameplay/ScriptSystem.h"
#include "MyEngine/Time/EngineTime.h"
#ifdef USE_IMGUI
#include "MyEngine/Editor/History/EditorHistory.h"
#include "MyEngine/Editor/Viewport/SceneVisibility.h"
#include "MyEngine/Editor/Windows/HierarchyWindow.h"
#include "MyEngine/Editor/Windows/UIScreenEditor.h"
#endif

// 静的メンバ変数
SceneManager* SceneManager::updating_ = nullptr;
SceneManager* SceneManager::main_ = nullptr;

namespace {
// エディタで最後に開いていたシーンを覚えておくファイル（エディタだけの設定。製品版では読まない）
constexpr const char* kLastSceneFile = "Parameters/Editor/LastScene.json";

// 名前 → 作り方 の表
// REGISTER_SCENE は main より前（static 変数が作られるとき）に呼ばれる。表をふつうの static 変数にすると、
// 表の方がまだ作られていないことがあるので、関数の中の static にする（初めて呼ばれたときに必ず作られる）
std::map<std::string, SceneFactory>& SceneTable() {
	static std::map<std::string, SceneFactory> table;
	return table;
}

// シーンファイルの場所から名前を作る（"resources/scenes/Title.scene.json" → "Title"）
std::string SceneNameFromFile(std::string_view file) {
	const size_t slash = file.find_last_of("/\\");
	std::string_view name = (slash == std::string_view::npos) ? file : file.substr(slash + 1);
	const std::string_view extension = kSceneFileExtension;
	if (name.ends_with(extension)) {
		name.remove_suffix(extension.size());
	}
	return std::string(name);
}

// 最後に開いていたシーンの名前を書く（次の起動で GetLastOpenedScene が読む）
void SaveLastOpenedScene(const std::string& name) {
#ifdef USE_IMGUI
	if (name.empty()) {
		return; // 名前の無い前の書き方のシーンは覚えない
	}
	std::error_code error;
	std::filesystem::create_directories(std::filesystem::path(kLastSceneFile).parent_path(), error);
	std::ofstream stream(kLastSceneFile);
	stream << nlohmann::json{{"scene", name}}.dump(4) << '\n';
#else
	(void)name; // 製品版は覚えない
#endif
}
} // namespace

//======================================================================================================
// シーンの登録（REGISTER_SCENE）
//======================================================================================================
void SceneRegistry::Register(const char* name, SceneFactory factory) { SceneTable()[name] = std::move(factory); }

SceneFactory SceneRegistry::Find(const std::string& name) {
	const auto it = SceneTable().find(name);
	return (it != SceneTable().end()) ? it->second : nullptr;
}

//======================================================================================================
// 初期化・更新・描画・終了
//======================================================================================================
// ===== 初期化 =====
void SceneManager::Initialize(SceneFactory openScene) {
	MY_ASSERT_MSG(openScene != nullptr, "最初のシーンが渡されていません（Engine::Initialize の2つ目の引数）");
	// Update の外で LoadScene を呼ばれたときは、最初のウィンドウのシーンを切り替える
	if (main_ == nullptr) {
		main_ = this;
	}
	// シーンのファイルは中に Component のアセット・置いた Prefab のパスを書く。名前を変えたときに、中のパスも書き換えてもらう（同じ物は1回だけ入る）
	AssetMoveRegistry::AddReferenceFileType(kSceneFileExtension);
	openSceneFactory_ = std::move(openScene);
	ChangeScene(openSceneFactory_(), false);
	openSceneName_ = currentScene_->GetSceneName();
	MarkSaved();
	SaveLastOpenedScene(openSceneName_);
}

// ===== 更新 =====
void SceneManager::Update() {
	MY_ASSERT_MSG(currentScene_ != nullptr, "シーンが登録されておらず、更新できませんでした");
	// この Update の間に呼ばれた LoadScene（シーンの Update・SYSTEM）は、このウィンドウのシーンを切り替える
	updating_ = this;

	// ===== フレームの頭：Entityを作り直す・保存する・開くのはここだけ =====
	// 前のフレームの予約（Componentの追加・Undoなど）は反映済みで、まだ誰も ForEach で回していない
	if (isReloadRequested_) {
		isReloadRequested_ = false;
		ReloadImmediate();
	}
	if (nextScene_) {
		ChangeScene(std::move(nextScene_), false); // 前のフレームで NextScene() が返したシーン
	}
	if (!nextSceneName_.empty()) {
		// 前のフレームで LoadScene に頼まれたシーン（頼まれた名前は空に戻す）
		ChangeScene(CreateScene(std::exchange(nextSceneName_, std::string())), false);
	}
	// 保存は作り直しの後（Stop と Save を同じフレームで頼まれても、戻した後の状態を書く）
	bool isSaveFailed = false;
	if (isSaveRequested_) {
		isSaveRequested_ = false;
		isSaveFailed = !SaveImmediate();
	}
	// 名前を付けて保存・開くは、保存の後（「保存してから開く」を同じフレームで頼まれたら、保存した後に開く）
	if (!saveAsRequestName_.empty()) {
		SaveAsImmediate(std::exchange(saveAsRequestName_, std::string()));
	}
	if (!openRequestName_.empty()) {
		const std::string name = std::exchange(openRequestName_, std::string());
		if (isSaveFailed) {
			// 保存できなかったのに開くと、編集が消えてしまう
			LogManager::Warning(std::format("保存できなかったので、{} を開くのをやめました", name));
		} else {
			OpenImmediate(name);
		}
	}
#ifdef USE_IMGUI
	UpdateDirty();
#endif

	// ===== フレームの頭の最後：ワールド行列を、今の Transform から作り直す =====
	// 決まり：Script・SYSTEM が回る前には、全部の Entity のワールド行列が、そのときの Transform から作られている
	// フレームの頭（ここまでと EngineSystems::FlushPending）で Entity を作った・戻した（シーンを読む・作り直す・開く・Undo・エディタから Prefab を置く）ので、
	// 作り直さないと、その Entity の行列はまだ無い（単位行列＝原点）か、古いまま
	EntityManager::UpdateTransforms();

	// 一時停止は時間を0にする。Updateは走るので ApplyGV が効く
	// ゲームのスロー（Time::SetGameTimeScale）は別に持っているので、ここで書き直しても消えない
	// 停止中も書く（一時停止のまま Stop したとき、停止中の時間が0のまま残らないように）
	Time::SetPauseScale(playState_ == PlayState::Paused ? 0.0f : 1.0f);

	// 停止中は更新しない（Drawは回るので画面は出たまま）
	if (playState_ != PlayState::Editing) {
		currentScene_->Update();
		// Script は一時停止中は呼ばない（SYSTEM は時間0で回る）。ゲームのポーズ中は、開いている画面（UI Screen）の中の Script だけ呼ぶ
		const bool isRunning = (playState_ == PlayState::Playing);
		const ScriptSystem::EntityFilter filter = Time::IsGamePaused() ? &UIScreen::IsInsideOpenScreen : nullptr;
		if (isRunning) {
			ScriptSystem::Update(filter); // Script の Awake・Start・当たり・Update（シーンのUpdateの後、SYSTEMの前）
		}
		GameComponentRegistry::UpdateAll(Time::GetDeltaTime()); // SYSTEM(...) で書いた処理（シーンのUpdateの後、ワールド行列の計算の前）
		if (isRunning) {
			ScriptSystem::LateUpdate(filter); // Script の LateUpdate（SYSTEMの後）
		}

		// シーン遷移は予約だけ（Entityの入れ替えは、次のフレームの頭で行う）
		if (std::unique_ptr<IScene> next = currentScene_->NextScene()) {
			nextScene_ = std::move(next);
		}
	}
	updating_ = nullptr;
}

// ===== 描画 =====
void SceneManager::Draw() {
	MY_ASSERT_MSG(currentScene_ != nullptr, "シーンが登録されておらず、描画できませんでした");
	currentScene_->Draw();
}

// ===== 終了 =====
void SceneManager::Finalize() {
	if (currentScene_) {
		currentScene_->Finalize();
		currentScene_.reset(); // 2回 Finalize しないように、ここで捨てる
	}
	// 閉じたウィンドウへ LoadScene を送らないように
	if (main_ == this) {
		main_ = nullptr;
	}
}

//======================================================================================================
// シーンの切り替え（ゲームのコードから）
//======================================================================================================
bool SceneManager::LoadScene(const std::string& name) {
	SceneManager* target = (updating_ != nullptr) ? updating_ : main_;
	if (target == nullptr) {
		LogManager::Warning(std::format("LoadScene(\"{}\")：ウィンドウがまだ無いので切り替えられません", name));
		return false;
	}
	// 停止中はゲームのコードが動かないので、ふつうは来ない（来るのはシーンの Initialize から呼んだとき）
	// エディタで開いているシーンを、ゲームのコードが勝手に替えないようにする
	if (target->playState_ == PlayState::Editing) {
		LogManager::Log(std::format("LoadScene(\"{}\")：停止中なので切り替えません（Play 中だけ効きます）", name));
		return false;
	}
	if (!HasScene(name)) {
		LogManager::Warning(std::format("LoadScene(\"{}\")：シーンがありません（{} も REGISTER_SCENE({}) もありません）", name, MakeSceneFilePath(name), name));
		return false;
	}
	target->nextSceneName_ = name; // 同じフレームに何回も呼ばれたら、最後に頼まれたシーンへ
	return true;
}

// ===== 今のシーンを最初からやり直す =====
bool SceneManager::ReloadScene() {
	SceneManager* target = (updating_ != nullptr) ? updating_ : main_;
	if (target == nullptr || target->currentScene_ == nullptr) {
		LogManager::Warning("ReloadScene()：シーンがまだ無いので、やり直せません");
		return false;
	}
	if (target->playState_ == PlayState::Editing) {
		LogManager::Log("ReloadScene()：停止中なのでやり直しません（Play 中だけ効きます）");
		return false;
	}
	const std::string& name = target->currentScene_->GetSceneName();
	// --- Play を押したシーン：Stop と同じく、Play を押した瞬間の状態へ（エディタで保存していない編集も残る。製品版はシーンファイルから）---
	if (name == target->openSceneName_) {
		target->isReloadRequested_ = true;
		return true;
	}
	// --- LoadScene で切り替えた先：そのシーンファイルから読み直す ---
	return LoadScene(name);
}

// ===== nameのシーンがあるか =====
bool SceneManager::HasScene(const std::string& name) {
	if (name.empty()) {
		return false;
	}
	if (SceneRegistry::Find(name) != nullptr) {
		return true; // クラスがあれば、ファイルが無くても CreateDefaultEntities から作れる
	}
	std::error_code error; // 調べられなかったときも例外を出さず「無い」にする
	return std::filesystem::exists(MakeSceneFilePath(name), error);
}

// ===== nameのシーンを作る =====
std::unique_ptr<IScene> SceneManager::CreateScene(const std::string& name) {
	// REGISTER_SCENE したクラスがあればそれ、無ければ Entity だけで動くエンジンのシーン
	const SceneFactory factory = SceneRegistry::Find(name);
	std::unique_ptr<IScene> scene = factory ? factory() : std::make_unique<DefaultScene>();
	scene->SetSceneName(name);
	return scene;
}

// ===== resources/sceneの中にあるシーンをすべて取得 =====
std::vector<std::string> SceneManager::GetSceneNames() {
	std::vector<std::string> names;
	std::error_code error;
	for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(kSceneFolder, error)) {
		const std::string file = entry.path().filename().string();
		if (entry.is_regular_file(error) && file.ends_with(kSceneFileExtension)) {
			names.push_back(SceneNameFromFile(file));
		}
	}
	std::sort(names.begin(), names.end());
	return names;
}

//======================================================================================================
// シーンファイルを作る（エディタから）
//======================================================================================================
bool SceneManager::CreateSceneFile(const std::string& name) {
	if (!IsValidFileName(name)) {
		LogManager::Warning(std::format("シーンの名前に使えるのは、英数字と _ - だけです: \"{}\"", name));
		return false;
	}
	const std::filesystem::path path = MakeSceneFilePath(name);
	std::error_code error;
	if (std::filesystem::exists(path, error)) {
		LogManager::Warning(std::format("同じ名前のシーンがもうあります: {}", path.generic_string()));
		return false;
	}
	std::filesystem::create_directories(path.parent_path(), error);
	// 中身が空のファイル＝まだ保存していない新しいシーン（開くと CreateDefaultEntities から始まる。SceneSerializer::LoadFile）
	std::ofstream stream(path);
	if (!stream) {
		LogManager::Error(std::format("シーンファイルを作れませんでした: {}", path.generic_string()));
		return false;
	}
	LogManager::Log(std::format("シーンを作りました: {}", path.generic_string()));
	return true;
}

//======================================================================================================
// 再生コントロール
//======================================================================================================
// ===== 再生 =====
void SceneManager::Play() {
	if (playState_ == PlayState::Editing) {
		isSnapshotRequested_ = true; // 作り直す直前に、編集した状態を退避する（Stopでここへ戻す）
		RequestReload();             // 退避した物から作り直して、最初から再生する
	}
	playState_ = PlayState::Playing;
	AudioSystem::SetPaused(false); // 一時停止中に Play を呼ばれても、音が止まったままにならないように
}

// ===== 一時停止 =====
void SceneManager::Pause() {
	switch (playState_) {
	case PlayState::Editing:
		break; // 停止中は一時停止しない
	case PlayState::Playing:
		playState_ = PlayState::Paused;
		break;
	case PlayState::Paused:
		playState_ = PlayState::Playing;
		break;
	}
	AudioSystem::SetPaused(playState_ == PlayState::Paused); // 時間と一緒に音も止める / 続きから鳴らす
}

// ===== 停止 =====
void SceneManager::Stop() {
	if (playState_ == PlayState::Editing) {
		return; // 停止中に押しても作り直さない（保存していない編集を消さないように）
	}
	playState_ = PlayState::Editing;
	// 音はすぐに止める（作り直しは次のフレームの頭。一時停止を先に解くと、止めるまでの1フレームだけ鳴ってしまう）
	AudioSystem::StopAll();
	AudioSystem::SetPaused(false);
	RequestReload(); // Playを押した瞬間の状態へ戻す
}

//======================================================================================================
// 作り直す・入れ替える（フレームの頭でだけ呼ぶ）
//======================================================================================================
void SceneManager::ReloadImmediate() {
	// 鳴っている音を全部止める（シーンの Initialize が鳴らした BGM が、Play / Stop / Restart のたびに重なっていかないように）
	AudioSystem::StopAll();
	// Playを押した直後なら、作り直す前に今のEntityを退避する（フレームの頭なので、見えている通りに写せる）
	if (isSnapshotRequested_) {
		isSnapshotRequested_ = false;
		if (GetSceneFile() != nullptr) {
			playSnapshot_ = SceneSerializer::SaveToText();
		}
	}
	// 再生中に頼まれたシーン遷移は捨てる（開いているシーンに戻るので、その遷移はもう関係ない）
	// これが残っていると、Stopを押したフレームにそのまま次のシーンへ飛んでしまう
	nextScene_.reset();
	nextSceneName_.clear();
	// Stop・Restartで開いているシーンに戻る（タイトル → ゲームと切り替わった後でも、Playを押したシーンへ戻る）
	ChangeScene(openSceneFactory_(), true);
	// 停止に戻ったら退避はもう要らない（次のPlayで取り直す）
	if (playState_ == PlayState::Editing) {
		playSnapshot_.clear();
	}
}

// ===== シーンを入れ替える =====
void SceneManager::ChangeScene(std::unique_ptr<IScene> next, bool useSnapshot) {
#ifdef USE_IMGUI
	// 画面（UI Screen）を編集中なら、保存して閉じる（Play・シーンを開く・作り直すの前。画面の中身はシーンに入っていないので、このままだと消えてしまう）
	UIScreenEditor::EndNow(true);
	// 履歴が覚えている相手（EntityId）が作り直されるので、履歴は捨てる
	EditorHistory::Clear();
	// 選んでいたEntityを番号で覚えておく（作り直すとHandleは変わるが、番号は同じ）
	const Entity* selected = EntityManager::Get(HierarchyWindow::GetSelected());
	const EntityId selectedId = selected ? selected->id : 0;
#endif

	// ===== 1. 古いシーンの片付け（カメラ・IBLなど、Entityではない物）=====
	// スロー・ヒットストップの途中で Stop・シーン切り替えをしても、次のシーンへ持ち越さない
	Time::ResetGameTime();
	CameraSystem::ResetOutput(); // 前のシーンのカメラから補間しないように
	const bool oldUsesFile = currentScene_ && currentScene_->GetSceneFile() != nullptr;
	if (currentScene_) {
		currentScene_->Finalize();
	}
	currentScene_ = std::move(next);
	// 古いシーンのエミッター（ParticleEmitter）はここで消えたので、その粒のグループも捨てる（新しいシーンのエミッターは、初めて出すときに作り直す）
	ParticleManager::ClearGroups();
	currentScene_->SetWindowTitle(windowTitle_);
	// 名前の無い前の書き方のシーン（GetSceneFile を上書きしたシーン）は、シーンファイルの名前を名前にする（Control ウィンドウに出す）
	if (currentScene_->GetSceneName().empty() && currentScene_->GetSceneFile() != nullptr) {
		currentScene_->SetSceneName(SceneNameFromFile(currentScene_->GetSceneFile()));
	}

	// ===== 2. Entityを入れ替える =====
	// シーンファイルを使うシーンは、全Entityを自分の物として扱う（出るときも入るときも全部消す）
	// 使わないシーンは今まで通り、自分で作って自分で消す
	const bool newUsesFile = currentScene_->GetSceneFile() != nullptr;
	if (oldUsesFile || newUsesFile) {
		SceneSerializer::DestroyAllNow();
	}
	if (newUsesFile) {
		LoadEntities(useSnapshot);
	}
	EntityManager::FlushComponentChanges(); // CreateDefaultEntities で予約した分もここで付く＝Initialize の時点でそろっている

	// ===== 3. 新しいシーンの準備（Entityがそろった後）=====
	const size_t entityCount = EntityManager::GetCount();
	currentScene_->Initialize();
	// Initialize で Entity を作ると、ファイル・退避から作った物に毎回足されて、Play / Stop のたびに増えていく
	if (newUsesFile && EntityManager::GetCount() != entityCount) {
		LogManager::Warning("シーンファイルを使うシーンの Initialize で Entity を作っています。Play / Stop のたびに増えるので、CreateDefaultEntities へ移してください");
	}

#ifdef USE_IMGUI
	HierarchyWindow::SetSelected(EntityManager::FindById(selectedId)); // 同じ番号のEntityを選び直す（居なければ選択なし）
#endif
}

void SceneManager::LoadEntities(bool useSnapshot) {
	// --- Playを押した瞬間の状態に戻す（Stop・Restart）---
	if (useSnapshot && !playSnapshot_.empty()) {
		SceneSerializer::LoadFromText(playSnapshot_);
		return;
	}
	// --- シーンファイルから作る（起動・シーン切り替え）---
	const char* file = currentScene_->GetSceneFile();
	switch (SceneSerializer::LoadFile(file)) {
	case SceneLoadResult::Loaded:
		// ファイルがある間は、CreateDefaultEntities を直しても反映されない（気づけるようにログに出す）
		LogManager::Log(std::format("CreateDefaultEntities() は呼んでいません（{} を消すと、また呼ばれます）", file));
		break;
	case SceneLoadResult::NotFound:
#ifdef USE_IMGUI
		LogManager::Log(std::format("シーンファイルが無い（空）ので、CreateDefaultEntities() で作ります（Save するとファイルができます）: {}", file));
#else
		// 製品版でファイルが無いのは、入れ忘れの可能性が高い
		LogManager::Warning(std::format("シーンファイルが無い（空）ので、CreateDefaultEntities() で作ります（resources に入れ忘れていませんか）: {}", file));
#endif
		currentScene_->CreateDefaultEntities();
		break;
	case SceneLoadResult::Failed:
		// 空のシーンで始まる。読めなかったファイルは「〇〇.broken」に写してあるので、Saveで上書きしても残る
		LogManager::Error(std::format("シーンファイルを読めなかったので、空のシーンで始めます（元のファイルは .broken に残しました）: {}", file));
		break;
	}
}

//======================================================================================================
// 保存・開く（エディタから。フレームの頭でだけ呼ぶ）
//======================================================================================================
bool SceneManager::CanEditFiles(const std::string& what) const {
	// 再生中に保存すると、動いた後の状態がファイルに残ってしまう（Unityも再生中は保存できない）
	// 退避を持っている間＝まだ Play 中の Entity のまま（Stop は次の作り直しで戻る）なので、それも断る
	if (playState_ != PlayState::Editing || !playSnapshot_.empty()) {
		LogManager::Warning(std::format("再生中は{}できません。Stop してからにしてください", what));
		return false;
	}
	return true;
}

bool SceneManager::SaveImmediate() {
	const char* file = GetSceneFile();
	if (file == nullptr) {
		LogManager::Warning("このシーンはシーンファイルを使っていないので保存できません（IScene::GetSceneFile）");
		return false;
	}
	if (!CanEditFiles("保存")) {
		return false;
	}
	if (!SceneSerializer::SaveFile(file)) {
		return false;
	}
#ifdef USE_IMGUI
	UIScreenEditor::Save(); // 画面（UI Screen）を編集中なら、その画面のファイルも保存する（Ctrl+S で両方）
#endif
	MarkSaved(); // ここが「保存した状態」になる（* が消える）
	return true;
}

void SceneManager::OpenImmediate(const std::string& name) {
	if (!CanEditFiles("シーンを開くことは")) {
		return;
	}
	if (!HasScene(name)) {
		LogManager::Warning(std::format("シーンがありません: {}", MakeSceneFilePath(name)));
		return;
	}
	AudioSystem::StopAll(); // Inspector で試し聞きしていた音を止める
#ifdef USE_IMGUI
	HierarchyWindow::SetSelected({}); // 前のシーンの Entity の番号で、別のシーンの Entity を選ばないように外しておく
	SceneVisibility::Clear();         // 目で隠した物も同じ（番号で覚えているので、別のシーンの物を隠さないように）
#endif
	// これからは Stop・Restart でもこのシーンを作り直す
	openSceneFactory_ = [name] { return CreateScene(name); };
	ChangeScene(openSceneFactory_(), false);
	openSceneName_ = name;
	MarkSaved();
	SaveLastOpenedScene(name);
	LogManager::Log(std::format("シーンを開きました: {}", name));
}

void SceneManager::SaveAsImmediate(const std::string& name) {
	if (!CanEditFiles("名前を付けて保存")) {
		return;
	}
	if (!IsValidFileName(name)) {
		LogManager::Warning(std::format("シーンの名前に使えるのは、英数字と _ - だけです: \"{}\"", name));
		return;
	}
	if (!SceneSerializer::SaveFile(MakeSceneFilePath(name))) {
		return;
	}
	// 書いたファイルから開き直す（クラスも名前で決まり直す。REGISTER_SCENE していない名前なら DefaultScene）
	OpenImmediate(name);
}

//======================================================================================================
// 保存していない変更（*）
//======================================================================================================
void SceneManager::MarkSaved() {
#ifdef USE_IMGUI
	savedSceneText_ = (GetSceneFile() != nullptr) ? SceneSerializer::SaveToText() : std::string();
	checkedChangeCount_ = EditorHistory::GetChangeCount();
	isDirty_ = false;
#endif
}

void SceneManager::UpdateDirty() {
#ifdef USE_IMGUI
	// 再生中は比べない（動いている Entity は保存する物ではない。Stop で戻った後に、また比べる）
	if (playState_ != PlayState::Editing || !playSnapshot_.empty() || GetSceneFile() == nullptr) {
		return;
	}
	// Entity を変える操作は全部 EditorHistory を通るので、履歴が動いていなければ Entity も変わっていない
	const uint64_t changeCount = EditorHistory::GetChangeCount();
	if (changeCount == checkedChangeCount_) {
		return;
	}
	checkedChangeCount_ = changeCount;
	// 保存した時点の中身と比べる（Undo で保存した時点まで戻したら、同じになって * が消える）
	isDirty_ = (SceneSerializer::SaveToText() != savedSceneText_);
#endif
}

//======================================================================================================
// 最後に開いていたシーン
//======================================================================================================
std::string SceneManager::GetLastOpenedScene(const std::string& fallback) {
	std::ifstream stream(kLastSceneFile);
	if (!stream) {
		return fallback; // まだ覚えていない（初めての起動）
	}
	const nlohmann::json json = nlohmann::json::parse(stream, nullptr, false); // false: 壊れていても例外を投げない
	const auto it = json.is_object() ? json.find("scene") : json.end();
	const std::string name = (it != json.end() && it->is_string()) ? it->get<std::string>() : std::string();
	// 消した・名前を変えたシーンなら、fallback から始める
	return HasScene(name) ? name : fallback;
}