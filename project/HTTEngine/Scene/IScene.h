#pragma once
#include <functional>
#include <memory>
#include <string>

#include "HTTEngine/Systems/Camera/CameraSystem.h"

// 前方宣言
class Camera;
class IBLEnvironment;

// シーンファイルの置き場所と、名前の後ろに付く部分（"resources/scenes/Title.scene.json" のシーンの名前は "Title"）
inline constexpr const char* kSceneFolder = "resources/scenes/";
inline constexpr const char* kSceneFileExtension = ".scene.json";
// 名前からシーンファイルの場所を作る（"Title" → "resources/scenes/Title.scene.json"）
inline std::string MakeSceneFilePath(const std::string& name) { return std::string(kSceneFolder) + name + kSceneFileExtension; }


/// <summary>
/// シーン基底クラス
/// <para>シーンファイルを使うシーン（GetSceneFile が nullptr 以外）では、Entityはエンジンが作って消す。
/// 呼ばれる順番は「前のシーンの Finalize → 全Entityを消す → Entityを作る（Playの退避 / シーンファイル / CreateDefaultEntities）→ Initialize」</para>
/// <para>名前で作ったシーン（Engine::Initialize・SceneManager::LoadScene に名前を渡す）は、名前からシーンファイルが決まる</para>
/// </summary>
class IScene {
public:
	virtual ~IScene() = default;
	// Entityがそろった後に呼ばれる。カメラ・IBLなど、Entityではない物を作る
	virtual void Initialize() = 0;
	virtual void Update() = 0;
	virtual void Draw() = 0;
	// Initializeで作った物（Entityではない物）を片付ける。Entityはエンジンが消すので、ここでは消さなくてよい
	virtual void Finalize() = 0;
	// ゲームの絵を撮るカメラ。ふつうは上書きしない（Hierarchy に置いた Camera Component のカメラになる。CameraSystem）
	// shared_ptr で返すのは、受け取った側が weak_ptr で覚えておけるように（ParticleManager）
	virtual std::shared_ptr<Camera> GetCamera() { return CameraSystem::GetSharedMainCamera(); }
	// PBR の環境光（IBL）。持っているシーンは上書きして返す（DefaultScene は持っている）
	virtual IBLEnvironment* GetEnvironment() { return nullptr; }
	// 前の書き方の切り替え（次のシーンを返す）。新しく書くときは SceneManager::LoadScene("名前")
	virtual std::unique_ptr<IScene> NextScene() { return nullptr; }

	// ===== シーンファイル =====
	// Entityを保存するファイル。名前で作ったシーンは「resources/scenes/<名前>.scene.json」になるので、上書きしなくてよい
	// nullptr ならファイルを使わない（今まで通り、Initializeで作ってFinalizeで消す）
	virtual const char* GetSceneFile() const { return sceneFile_.empty() ? nullptr : sceneFile_.c_str(); }
	// シーンファイルがまだ無いときだけ、Initialize の前に呼ばれる。最初の配置をコードで作る
	// （Saveするとファイルができるので、次からは呼ばれない。ファイルを消すと、またこの配置から始まる）
	// ここで作ったEntityのHandleをメンバに覚えない（Play・Stop・次の起動ではこの関数が呼ばれず、Handleも変わる）。
	// 覚えたいときは Initialize の中で EntityManager::FindByName などで探す
	virtual void CreateDefaultEntities() {}

	// ===== 名前 =====
	// シーンの名前（シーンファイルの名前。"Title" など）。名前の無い前の書き方のシーンは、シーンファイルの名前になる（ファイルも無ければ空）
	const std::string& GetSceneName() const { return sceneName_; }

	// ===== エンジンが自動でセットする =====
	// WindowManager がセットする
	void SetWindowTitle(const std::wstring& title) { windowTitle_ = title; }
	const std::wstring& GetWindowTitle() const { return windowTitle_; }
	// SceneManager が作った直後にセットする。シーンファイルの場所も名前から決まる
	void SetSceneName(const std::string& name) {
		sceneName_ = name;
		sceneFile_ = MakeSceneFilePath(name);
	}

protected:
	std::wstring windowTitle_ = L"Title";

private:
	std::string sceneName_; // シーンの名前（空なら名前が無い）
	std::string sceneFile_; // 名前から決めたシーンファイルの場所（GetSceneFile を上書きしたシーンでは使われない）
};

// シーンの作り方。Stop / Restart で作り直すときにも使う
using SceneFactory = std::function<std::unique_ptr<IScene>()>;

//=============================================================================
// シーンのクラスを名前で登録する（REGISTER_SCENE）
//=============================================================================
namespace SceneRegistry {
// 名前と作り方を登録する（REGISTER_SCENE が main より前に呼ぶ）
void Register(const char* name, SceneFactory factory);
// 名前から作り方を探す（登録していなければ nullptr）
SceneFactory Find(const std::string& name);
} // namespace SceneRegistry

/// <summary>
/// REGISTER_SCENE(...) が1つ作る物。作られた時点で、名前と作り方が表に入る
/// </summary>
template<class T> class SceneRegistrar {
public:
	explicit SceneRegistrar(const char* name) {
		SceneRegistry::Register(name, [] { return std::make_unique<T>(); });
	}
};

/// <summary>
/// シーンのクラスを登録する。REGISTER_SCENE(GameScene) と書くと、"GameScene" のシーン（resources/scenes/GameScene.scene.json）を開くときにこのクラスが使われる
/// <para>登録していない名前のシーンは、エンジンの DefaultScene（Entity だけで動くシーン）で開く</para>
/// </summary>
#define REGISTER_SCENE(Type) inline const SceneRegistrar<Type> sceneRegistrarOf_##Type(#Type)