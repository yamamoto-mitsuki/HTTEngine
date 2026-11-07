#pragma once
#include <vector>
#include <memory>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <optional>
#include <random>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <string>
#include <string_view>

#include "MyEngine/Core/SlotMap.h"
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/ECS/ComponentStorage.h"
#include "MyEngine/ECS/Entity.h"
#include "MyEngine/Components/Rendering3D/ModelRendererComponent.h"
#include "MyEngine/Components/Core/TransformComponent.h"


/// <summary>
/// Entityと、全種類のComponentの管理
/// <para>Componentは型ごとの ComponentStorage&lt;T&gt; に入れる。ゲーム固有の型も RegisterComponent&lt;T&gt;() すれば同じように使える</para>
/// <para>ARCHITECTURE.md の更新順序のうち、2（ワールド行列）と6（破棄）を担当する</para>
/// </summary>
class EntityManager {
public:
	// ===== Passkey Idiom =====
	class ConstructorKey {
	private:
		ConstructorKey() = default;
		friend class EntityManager;
	};
	explicit EntityManager(ConstructorKey) {} // 鍵を受け取るコンストラクタ

	static void Initialize();
	static void Release();

	// ===== 生成・破棄 =====
	// Entityを作る。TransformComponentはその場で付く（作った直後から Get<TransformComponent> で取れる）
	static Handle<Entity> Create(const std::string& name = "Entity", Handle<Entity> parent = {});
	// 番号（EntityId）を指定して作る。Undoで消したEntityを戻すとき・シーンや Prefab を読むとき
	static Handle<Entity> CreateWithId(EntityId id, const std::string& name, Handle<Entity> parent);
	// まだ誰も使っていない番号を1つもらう（でたらめな 64 ビットの数。チームの2人が別々に作っても、まずぶつからない）
	static EntityId NewId();
	// 破棄を予約する。実際に消えるのはフレームの最後（子も一緒に消える）
	static void Destroy(Handle<Entity> handle);

	// ===== Entityの取得（受け取ったポインタは使い捨てにする。ARCHITECTURE.md 原則1）=====
	static Entity* Get(Handle<Entity> handle);
	static bool IsAlive(Handle<Entity> handle);
	static Handle<Entity> FindById(EntityId id); // 無ければ無効なHandle（0を渡しても無効なHandle＝root扱いにできる）
	static Handle<Entity> Find(EntityRef ref) { return FindById(ref.id); } // 参照から今のHandleを引く（居なければ無効なHandle）
	static EntityRef RefOf(Handle<Entity> handle);                         // Handleから参照を作る（Componentに覚えさせるとき）
	// 名前で探す。同じ名前が複数あれば最初の1つ、無ければ無効なHandle
	// シーンの Initialize で、シーンファイルから作られたEntityを探すのに使う（Handleは Play / Stop のたびに変わる）
	static Handle<Entity> FindByName(const std::string& name);
	static size_t GetCount();
	static SlotMap<Entity>& GetAll() { return instance_->entities_; }
	// 自分と、親を全部たどって全部が有効ならtrue（UnityのactiveInHierarchy）。Systemはこれがfalseなら処理しない
	static bool IsActiveInHierarchy(Handle<Entity> handle);

	// ===== 親子 =====
	// 親を付け替える。自分の子孫を親にしようとしたときは何もしない（輪になるのを防ぐ）
	// translation / rotation / scale はそのまま＝「新しい親から見て同じ位置」になるので、見た目の位置は変わる
	static void SetParent(Handle<Entity> child, Handle<Entity> parent);
	// 親を付け替える。見た目の位置・向き・大きさは変えない（新しい親から見た値に直す。Hierarchy のドラッグ＆ドロップはこちら）
	static void SetParentKeepWorld(Handle<Entity> child, Handle<Entity> parent);
	// 親まで掛けたワールド行列を、今の translation / rotation / scale から計算する（UpdateTransforms を待たずに正しい値が欲しいとき）
	static Matrix4x4 CalcWorldMatrix(Handle<Entity> handle);
	// ワールド座標で worldMove だけ動かす（translation は親から見た位置なので、親の下なら、親から見た量に直して足す）
	// 親のワールド行列は UpdateTransforms の結果を使う。物理の押し戻し・カメラの追いかけが使う
	static void TranslateWorld(Handle<Entity> handle, const Vector3& worldMove);
	// 親をたどって、handleがancestorの子孫かを調べる（handle自身は含まない）
	static bool IsDescendantOf(Handle<Entity> handle, Handle<Entity> ancestor);
	// 同じEntityか、どちらかがどちらかの先祖（親・親の親…）か。自分の子に付けた Collider と当たらないようにするのに使う
	static bool IsRelated(Handle<Entity> a, Handle<Entity> b) { return a == b || IsDescendantOf(a, b) || IsDescendantOf(b, a); }
	// 子のHandleを out に詰める（outは使い回す）
	static void GetChildren(Handle<Entity> handle, std::vector<Handle<Entity>>& out);
	// root から子の名前を / でつないだパス（"Panel/Title"）で探す。空なら root。同じ名前の子が2つあれば先の物。無ければ無効なHandle
	// 先頭が / なら root を見ず、シーンの一番上から探す（"/Cameras/Close"）
	// Prefab・画面（UI Screen）から作るたびに EntityId が変わる物を、作り直しても同じ相手として指すのに使う。全部の Entity を調べるので、毎フレームは呼ばない
	static Handle<Entity> FindByPath(Handle<Entity> root, std::string_view path);
	// FindByPath の逆。root から entity までの名前のパス（entity が root なら空。root の子孫でなければ std::nullopt）
	static std::optional<std::string> MakePath(Handle<Entity> root, Handle<Entity> entity);
	// root から entity を指すパス。子孫なら root からのパス、それ以外はシーンの一番上からのパス（/ で始まる）。どちらも FindByPath で同じ Entity に戻る
	static std::string MakeBindingPath(Handle<Entity> root, Handle<Entity> entity);
	// 親を持たないEntity（一番上）のHandleを out に詰める
	static void GetRoots(std::vector<Handle<Entity>>& out);

	// ===== Component（型ごと）=====
	// 型を登録する。Initializeの後、その型を使う前に呼ぶ。同じ型の再登録は何もしない
	// Editorの無いReleaseでも呼ぶ（Inspectorへの登録＝ComponentEditorRegistryとは別）
	template<class T> static void RegisterComponent() {
		static_assert(std::is_same_v<T, std::remove_cvref_t<T>>);
		EnsureInitialized();
		const std::type_index key(typeid(T));
		if (!instance_->components_.contains(key)) {
			instance_->components_.emplace(key, std::make_unique<ComponentStorage<T>>());
		}
	}

	// 反映済みの実体。予約中・未登録・未追加・無効なEntityならnullptr
	template<class T> static T* Get(Handle<Entity> handle) {
		if (!IsAlive(handle)) {
			return nullptr;
		}
		auto* storage = FindStorage<T>();
		return storage ? storage->Get(handle) : nullptr;
	}

	// 参照（EntityRef）から取る。相手が居なければnullptr
	template<class T> static T* Get(EntityRef ref) { return Get<T>(Find(ref)); }

	// 追加を予約する。実際に付くのは次の FlushComponentChanges（それまで Get はnullptr）
	template<class T> static bool RequestAdd(Handle<Entity> handle, const T& initial = {}) {
		if (!IsAlive(handle)) {
			return false;
		}
		return RequireStorage<T>().RequestAdd(handle, initial);
	}

	// 必須Transformの削除をRuntime側でも防ぐ。UIだけで禁止して終わりにしない。
	template<class T> static constexpr bool IsRemovable() { return !std::is_same_v<T, TransformComponent>; }

	// 取り外しを予約する。実際に外れるのは次の FlushComponentChanges
	template<class T> static bool RequestRemove(Handle<Entity> handle) {
		if constexpr (!IsRemovable<T>()) {
			return false;
		} else {
			if (!IsAlive(handle)) {
				return false;
			}
			return RequireStorage<T>().RequestRemove(handle);
		}
	}

	template<class T> static bool IsAddPending(Handle<Entity> handle) {
		auto* storage = FindStorage<T>();
		return IsAlive(handle) && storage && storage->IsAddPending(handle);
	}

	// Tを持っているEntityだけを回す。function(Handle<Entity> entity, T& component)
	// 中でしてよいこと：値の変更、追加・取り外し・破棄の予約、Create（ただしT＝Transformのときは不可）
	// 中でしてはいけないこと：Tの配列が増減する操作（Flush系）。受け取った参照を外へ持ち出さない
	template<class T, class F> static void ForEach(F&& function) {
		if (auto* storage = FindStorage<T>()) {
			storage->ForEach(std::forward<F>(function));
		}
	}

	// ===== Componentの写し（Undo・コピー用。登録されている全部の型を、型を知らずに扱う）=====
	// handleが持っているComponentを全部 out に写す
	static void CaptureComponents(Handle<Entity> handle, std::vector<ComponentSnapshot>& out);
	// 写しを戻す。持っていない物は足し、持っている物（Transformなど）は上書きする。フレームの境目で呼ぶ
	static void RestoreComponents(Handle<Entity> handle, const std::vector<ComponentSnapshot>& components);

	// ===== フレーム更新 =====
	// 更新の最初に1回呼ぶ。Componentの追加・取り外しの予約をまとめて反映する
	static void FlushComponentChanges();
	// 更新順序2：ワールド行列を親→子の順で計算する
	// フレームの頭の最後（SceneManager::Update。Script・SYSTEM の前）と、Script・SYSTEM の後（EngineSystems::Update）に呼ぶ
	// なので Script・SYSTEM が読むワールド行列は、フレームの頭の時点の Transform から作った物（そのフレームの中で動かした分は、次に呼ぶまで入らない）
	static void UpdateTransforms();
	// 更新順序6：破棄予約をまとめて反映する。フレームの最後に呼ぶ
	static void FlushDestroy();

	// ===== 破棄の知らせ =====
	// Entityが消える直前に呼んでほしい関数を登録する（ScriptSystem が OnDestroy を呼ぶのに使う）
	// 呼ばれる時点では、そのEntityのComponentはまだ全部ある。子も一緒に消えるときは、親が先
	using DestroyListener = void (*)(Handle<Entity> handle);
	static void AddDestroyListener(DestroyListener listener);

	// ===== ワールド行列の手直し =====
	// UpdateTransforms が1つの Entity のワールド行列を作った直後（子に掛ける前）に呼んでほしい関数を登録する
	// ワールド行列を自分の計算で決める System（UI World Follow：UI を画面の上の決まった所に置く）が使う
	// parent は親の Transform（親が居なければ nullptr。親のワールド行列はもう決まっている）
	// UpdateTransforms は1フレームに何度も呼ばれるので、そのたびに同じ手直しが入る（後から呼んでも、手直しが消えない）
	using TransformAdjuster = void (*)(Handle<Entity> handle, TransformComponent& transform, const TransformComponent* parent);
	static void AddTransformAdjuster(TransformAdjuster adjuster);


private:
	static constexpr uint32_t kMaxParentDepth = 64; // 親をたどる回数の上限（万一輪になっても止まるように）
	// シングルトン
	friend struct std::default_delete<EntityManager>;
	~EntityManager() = default;
	EntityManager(const EntityManager&) = delete;
	EntityManager& operator=(const EntityManager&) = delete;
	static std::unique_ptr<EntityManager> instance_;

	static void EnsureInitialized();
	// 番号を配る乱数を作る（種は random_device から4回分。32 ビット1回分だけだと、別の PC と同じ並びになることがある）
	// ゲームの乱数（RandomEngine）とは分ける。ゲームの乱数の種を固定したときに番号まで毎回同じにならないように・Entity を作るたびにゲームの乱数の並びがずれないように
	static std::mt19937_64 MakeIdRandom();

	template<class T> static ComponentStorage<T>* FindStorage() {
		// const T等で同じtypeidから異なるStorageへキャストしない。
		static_assert(std::is_same_v<T, std::remove_cvref_t<T>>);
		return static_cast<ComponentStorage<T>*>(FindStorage(typeid(T)));
	}
	static IComponentStorage* FindStorage(std::type_index type);

	template<class T> static ComponentStorage<T>& RequireStorage() {
		auto* storage = FindStorage<T>();
		MY_ASSERT_MSG(storage != nullptr, "RegisterComponent<T>()を先に呼んでください");
		return *storage;
	}

	// 破棄予約されたEntityと、その子孫を outに集める
	void CollectDescendants(Handle<Entity> handle, std::vector<Handle<Entity>>& out);
	// 親を何回たどるとrootに着くか
	uint32_t CalcDepth(Handle<Entity> handle);

	SlotMap<Entity> entities_;
	std::unordered_map<std::type_index, std::unique_ptr<IComponentStorage>> components_; // 型 → その型の置き場所
	std::unordered_map<EntityId, Handle<Entity>> idToHandle_; // 番号 → Handle（生きているEntityだけ）
	std::mt19937_64 idRandom_ = MakeIdRandom(); // 番号を配る乱数（起動するたびに違う並びになる）
	std::vector<Handle<Entity>> pendingDestroy_;    // 破棄予約
	std::vector<DestroyListener> destroyListeners_; // 消える直前に呼ぶ関数（AddDestroyListener）
	std::vector<TransformAdjuster> transformAdjusters_; // ワールド行列を作った直後に呼ぶ関数（AddTransformAdjuster）
	// 毎フレーム使う作業用の配列（確保し直さないようにメンバで持つ）
	std::vector<std::pair<uint32_t, Handle<Entity>>> updateOrder_; // (深さ, Handle)
	std::vector<Handle<Entity>> destroyWork_;
};