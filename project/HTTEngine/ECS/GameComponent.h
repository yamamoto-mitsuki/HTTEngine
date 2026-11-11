#pragma once
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <source_location>

#include "HTTEngine/Components/Core/TransformComponent.h"
#include "HTTEngine/ECS/ComponentSerializer.h"
#include "HTTEngine/ECS/ComponentUI.h"
#include "HTTEngine/ECS/EntityManager.h"
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Scene/SceneManager.h"
#include "HTTEngine/Scene/SceneTransition.h"
#include "HTTEngine/UI/UIScreen.h"
// Audio
#include "HTTEngine/Systems/Audio/AudioSystem.h"
// Camera
#include "HTTEngine/Systems/Camera/CameraSystem.h"
// Combat
#include "HTTEngine/Combat/DamageTypes.h"
#include "HTTEngine/ECS/Layers.h"
#include "HTTEngine/Systems/Combat/DamageSystem.h"
#include "HTTEngine/Systems/Combat/HitSystem.h"
// Physics
#include "HTTEngine/Systems/Physics/CollisionSystem.h"
#include "HTTEngine/Systems/Physics/PhysicsSystem.h"
#include "HTTEngine/Systems/Physics/ElasticSurfaceSystem.h"
// Rendering2D
#include "HTTEngine/Systems/Rendering2D/SpriteRenderSystem.h"
#include "HTTEngine/Systems/Gameplay/AttackTableSystem.h"
#include "HTTEngine/Systems/Effects/TrailSystem.h"
#include "HTTEngine/Systems/Effects/ParticleSystem.h"
// UI
#include "HTTEngine/Systems/UI/PolylineSystem.h"
#include "HTTEngine/Systems/UI/TextRenderSystem.h"
#include "HTTEngine/Systems/UI/VectorShapeSystem.h"
#include "HTTEngine/Components/Rendering3D/PrimitiveRendererComponent.h"
#include "HTTEngine/Math/MathUtility.h"

// ゲーム固有Componentを書くための道具。ゲーム側はこのヘッダ1つだけをインクルードする
//
//   1ファイル＝1Component。書くのは「① データ ② 見せ方 ③ 処理」の3つだけ
//
//   struct Spin { Vector3 speed = {0.0f, 90.0f, 0.0f}; };            // ① データ
//   COMPONENT(Spin, "Movement") { ui.Field("Speed", value.speed); }  // ② 見せ方
//   void SpinUpdate(Spin& spin, TransformComponent& transform, float deltaTime) { ... }  // ③ 処理
//   SYSTEM(SpinUpdate);
//
//   当たったかどうかは、③の中で CollisionSystem::GetHits(entity) を読む（Collider は Add Component → Physics で付ける）
//   ぶつかったら止まる物は Rigidbody（Add Component → Physics）。③の中で EntityManager::Get<RigidbodyComponent>(entity)->velocity を書いて動かす
//   歩くキャラは Character Controller（坂で滑らない・段を上る）。同じく velocity を書いて動かす
//   画像は Sprite Renderer（3D空間の板）・Image（画面に重ねる絵）。ボタンは SpriteRenderSystem::IsMouseOver(entity)
//   音は、③の中で AudioSystem::Play(entity) / PlayOneShot(entity) を呼ぶ（Audio Source は Add Component → Audio で付ける）
//   カメラは Entity に付ける Camera Component。追いかけるなら、同じ Entity に Camera Follow（Add Component → Camera）を付けて Target を入れる（まわりを回って後ろに付くなら Camera Orbit）
//   シーンを替えるときは、③の中で SceneManager::LoadScene("Result") を呼ぶ（切り替わるのは次のフレームの頭）。やり直しは SceneManager::ReloadScene()

// 度 ↔ ラジアン（std::sin・std::cos に渡すとき。SetEulerAngles・Rotate などの Transform の角度は、度のまま渡す）
// 値は MathUtility の物。ゲームのスクリプトから MathUtility:: を付けずに書けるように、同じ名前で置いておく
constexpr float kDegToRad = MathUtility::kDegToRad;
constexpr float kRadToDeg = MathUtility::kRadToDeg;

//=============================================================================
// COMPONENT(...) の中身を Inspector の区画につなぐ
//=============================================================================

/// <summary>
/// COMPONENT(...) に書いた「見せ方」を使うComponentEditor。ゲームは直接触らない
/// </summary>
template<class T> class DescribedComponentEditor final : public TypedComponentEditor<T> {
public:
	using DescribeFunction = void (*)(ComponentUI& ui, T& value);

	// description は何をする Component かの説明、sourceFile は書いてあるファイル（どちらも Inspector の見出しにマウスを乗せると出る）。無ければ nullptr
	DescribedComponentEditor(const char* name, std::string category, DescribeFunction describe, const char* description = nullptr, const char* sourceFile = nullptr)
	    : name_(name), category_(std::move(category)), describe_(describe), description_(description), sourceFile_(sourceFile) {}

	const char* GetName() const override { return name_; }
	const char* GetCategory() const override { return category_.c_str(); }
	const char* GetSourceFile() const override { return sourceFile_; }
	const char* GetDescription() const override { return description_; }

protected:
	void DrawComponent(T& value) const override { describe_(GetInspectorUI(), value); }

private:
	const char* name_;          // COMPONENT(型名) の型名がそのまま表示名になる
	std::string category_;      // "Gameplay" か "Gameplay/サブカテゴリ"
	DescribeFunction describe_; // ②に書いた見せ方
	const char* description_;   // 何をする Component かの説明（文字列リテラルなので、プログラムの終わりまで消えない）
	const char* sourceFile_;    // COMPONENT(...) を書いたファイル（コンパイラが決めた文字列なので、プログラムの終わりまで消えない）
};

// "Movement" から "Gameplay/Movement" を作る（空なら "Gameplay"）
inline std::string MakeGameplayCategory(const char* subCategory) {
	std::string path = "Gameplay";
	if (subCategory != nullptr && subCategory[0] != 0) {
		path += "/";
		path += subCategory;
	}
	return path;
}

//=============================================================================
// 登録待ちの表（static変数が作られるのはmainより前なので、その場では登録できない）
//=============================================================================
namespace ComponentRegistryDetail {
// Componentの登録を1つ予約する
void AddPending(std::function<void()> apply);
// Systemを1つ登録する
void AddSystem(const char* name, std::function<void(float)> run);
} // namespace ComponentRegistryDetail

/// <summary>
/// COMPONENT(...) が1つ作る物。作られた時点では「登録待ちの表」に並ぶだけ
/// </summary>
template<class T> class ComponentRegistrar {
public:
	// 引数の並びは COMPONENT(型名, "サブカテゴリ", "説明") の並び（後ろの2つは書かなくてよいので、見せ方を先頭に置く）
	// description は Inspector の見出し・Add Component の項目にマウスを乗せると出る説明（書かなければ nullptr＝説明なし）
	// where は書かなくてよい。既定の引数は「呼んだ側」で作られるので、COMPONENT(...) を書いたファイルと行が入る
	ComponentRegistrar(void (*describe)(ComponentUI&, T&), const char* name, const char* subCategory, const char* description = nullptr,
	                   std::source_location where = std::source_location::current()) {
		const char* sourceFile = where.file_name();
		ComponentRegistryDetail::AddPending([describe, name, subCategory, description, sourceFile]() {
			EntityManager::RegisterComponent<T>();            // 置き場所
			ComponentSerializer::Register<T>(name, describe); // 保存・読み込み（Releaseでも要る）
			ComponentEditorRegistry::Register(                // Inspector
			    std::make_unique<DescribedComponentEditor<T>>(name, MakeGameplayCategory(subCategory), describe, description, sourceFile));
		});
	}
};

//=============================================================================
// SYSTEM(...) の中身（関数の引数の型から、回すComponentを決める）
//=============================================================================
namespace ComponentSystemDetail {

// 型の並びを持ち運ぶための箱
template<class... Ts> struct TypeList {};

// 最後の引数の型（引数の無い関数は登録できない）
template<class... Args> struct LastArg;
template<class T> struct LastArg<T> {
	using type = T;
};
template<class T, class... Rest> struct LastArg<T, Rest...> : LastArg<Rest...> {};

// tupleの Offset 番目から Index... 個を取り出して TypeList にする
template<class Tuple, size_t Offset, class Sequence> struct MakeTypeList;
template<class Tuple, size_t Offset, size_t... Index> struct MakeTypeList<Tuple, Offset, std::index_sequence<Index...>> {
	using type = TypeList<std::tuple_element_t<Offset + Index, Tuple>...>;
};

/// <summary>
/// 関数の引数を「Entity」「必要なComponent」「deltaTime」に分ける
/// <para>先頭が Handle&lt;Entity&gt; ならEntityを渡す。最後が float なら deltaTime を渡す。間は全部Component</para>
/// </summary>
template<class... Args> struct SystemArgs {
	static constexpr size_t count = sizeof...(Args);
	static_assert(count >= 1, "SYSTEMの関数には、Componentの参照を1つ以上書いてください");

	// 番兵の int を足して、引数が少なくても添字がはみ出さないようにする
	using AllWithGuard = std::tuple<Args..., int>;
	static constexpr bool hasEntity = std::is_same_v<std::tuple_element_t<0, AllWithGuard>, Handle<Entity>>;
	static constexpr bool hasDeltaTime = std::is_same_v<typename LastArg<Args...>::type, float>;
	static constexpr size_t componentCount = count - (hasEntity ? 1 : 0) - (hasDeltaTime ? 1 : 0);
	static_assert(componentCount >= 1, "SYSTEMの関数には、Componentの参照を1つ以上書いてください（Handleとfloatだけでは回せません）");

	using Components = typename MakeTypeList<std::tuple<Args...>, hasEntity ? 1 : 0, std::make_index_sequence<componentCount>>::type;
};

/// <summary>
/// First と Rest... を全部持っていて、親までたどって有効なEntityだけを回す
/// </summary>
template<bool HasEntity, bool HasDeltaTime, class Function, class First, class... Rest> void RunSystem(Function function, float deltaTime) {
	using FirstComponent = std::remove_cvref_t<First>;
	EntityManager::ForEach<FirstComponent>([&](Handle<Entity> entity, FirstComponent& first) {
		if (!EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		// 残りのComponentを集める。1つでも持っていなければ呼ばない
		const std::tuple<std::remove_cvref_t<Rest>*...> rest(EntityManager::Get<std::remove_cvref_t<Rest>>(entity)...);
		const bool hasAll = std::apply([](auto*... component) { return ((component != nullptr) && ...); }, rest);
		if (!hasAll) {
			return;
		}
		std::apply(
		    [&](auto*... component) {
			    if constexpr (HasEntity && HasDeltaTime) {
				    function(entity, first, *component..., deltaTime);
			    } else if constexpr (HasEntity) {
				    function(entity, first, *component...);
			    } else if constexpr (HasDeltaTime) {
				    function(first, *component..., deltaTime);
			    } else {
				    function(first, *component...);
			    }
		    },
		    rest);
	});
}

// TypeList を開いて RunSystem に渡す
template<bool HasEntity, bool HasDeltaTime, class Function, class... Components> void RunSystemFromList(Function function, float deltaTime, TypeList<Components...>) {
	RunSystem<HasEntity, HasDeltaTime, Function, Components...>(function, deltaTime);
}

} // namespace ComponentSystemDetail

/// <summary>
/// SYSTEM(...) が1つ作る物。作られた時点では毎フレームの呼び出し表に並ぶだけ
/// </summary>
class SystemRegistrar {
public:
	template<class... Args> SystemRegistrar(const char* name, void (*function)(Args...)) {
		using Info = ComponentSystemDetail::SystemArgs<Args...>;
		ComponentRegistryDetail::AddSystem(
		    name, [function](float deltaTime) { ComponentSystemDetail::RunSystemFromList<Info::hasEntity, Info::hasDeltaTime>(function, deltaTime, typename Info::Components{}); });
	}
};

/// <summary>
/// COMPONENT / SYSTEM で書いた物をまとめて扱う。呼ぶのはエンジンだけ
/// </summary>
class GameComponentRegistry {
public:
	// 登録待ちを全部登録する。EntityManager::Initialize の後に1回呼ぶ
	static void ApplyAll();
	// SYSTEM(...) で登録した処理を全部呼ぶ。シーンのUpdateの後に呼ぶ
	static void UpdateAll(float deltaTime);
	// 登録された数（確認用）
	static size_t GetComponentCount();
	static size_t GetSystemCount();
};

//=============================================================================
// ゲーム側が書くのはこの2つだけ（カテゴリに説明を付けたいときは COMPONENT_CATEGORY も）
//=============================================================================

/// <summary>
/// Componentの見せ方を書く。COMPONENT(型名)・COMPONENT(型名, "サブカテゴリ")・COMPONENT(型名, "サブカテゴリ", "説明")
/// <para>中では ui（見せ方の道具）と value（Componentの中身）が使える。カテゴリは Gameplay で固定</para>
/// <para>説明は Inspector の見出し・Add Component の項目にマウスを乗せると出る（サブカテゴリ無しで説明だけなら COMPONENT(型名, "", "説明")）</para>
/// </summary>
// [[maybe_unused]] は「使わなくても警告しないで」の印。中身の無いComponent（印だけのタグ）でも警告が出ないようにしている
// "" __VA_ARGS__ は、何も書かなければ ""、書けば "" "Movement", "説明" ＝ "Movement", "説明"（並んだ文字列はつながる）
#define COMPONENT(Type, ...)                                                                                                                                                                           \
	inline void Describe_##Type([[maybe_unused]] ComponentUI& ui, [[maybe_unused]] Type& value);                                                                                                       \
	inline const ComponentRegistrar<Type> registrarOf_##Type(&Describe_##Type, #Type, "" __VA_ARGS__);                                                                                                 \
	inline void Describe_##Type([[maybe_unused]] ComponentUI& ui, [[maybe_unused]] Type& value)

/// <summary>
/// COMPONENT_CATEGORY(...) が1つ作る物。作られた時点では「登録待ちの表」に並ぶだけ
/// </summary>
class ComponentCategoryRegistrar {
public:
	// subCategory は COMPONENT・SCRIPT に書くサブカテゴリと同じ書き方（"Movement" なら Gameplay → Movement）
	ComponentCategoryRegistrar(const char* subCategory, const char* description) {
		ComponentRegistryDetail::AddPending([subCategory, description] { ComponentEditorRegistry::RegisterCategoryDescription(MakeGameplayCategory(subCategory), description); });
	}
};

// 2つの字句をつなぐ（__LINE__ を先に数字にしてからつなぐため、2段にしている）
#define HTT_CONCAT_INNER(a, b) a##b
#define HTT_CONCAT(a, b) HTT_CONCAT_INNER(a, b)

/// <summary>
/// Add Component の Gameplay の下のカテゴリに説明を付ける。.cpp に COMPONENT_CATEGORY("Movement", "動かす物"); と1行書く
/// <para>Add Component でカテゴリにマウスを乗せると出る。同じカテゴリの COMPONENT のファイルのどれか1つに書けばよい</para>
/// </summary>
// static：このファイルの中だけの変数にする（別のファイルの同じ行に書いても、名前がぶつからない）
#define COMPONENT_CATEGORY(subCategory, description) static const ComponentCategoryRegistrar HTT_CONCAT(componentCategoryRegistrar_, __LINE__)(subCategory, description)

/// <summary>
/// 毎フレームの処理を登録する。関数の引数に書いたComponentを全部持つEntityだけが呼ばれる
/// <para>先頭の引数を Handle&lt;Entity&gt; にするとそのEntityが入る（消したいとき用）。最後の引数を float にすると deltaTime が入る</para>
/// </summary>
#define SYSTEM(function) inline const SystemRegistrar registrarOfSystem_##function(#function, &function)