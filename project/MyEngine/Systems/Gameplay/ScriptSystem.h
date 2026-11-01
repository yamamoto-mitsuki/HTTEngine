#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

#include "MyEngine/Core/Handle.h"
#include "MyEngine/ECS/Entity.h"

// 前方宣言
class Script;
class ComponentUI;
struct TimelineEvent;


/// <summary>
/// Script（Unity の MonoBehaviour と同じ形の、ゲーム固有の振る舞い）を持って、Awake・Start・Update などを呼ぶ係
/// <para>Script は仮想関数や std::vector を持つので、データの Component の置き場所（ComponentStorage）には入れられない。
/// ここが Entity ごとに unique_ptr で持つ（AudioSource と鳴っている音、Trail と点の列と同じ「System が実体を持つ」形）</para>
/// <para>Undo・保存・Play / Stop・コピーは、Script::Fields に書いた項目を JSON の文字にして写す</para>
/// <para>呼ぶのは再生中だけ（停止中・一時停止中は呼ばない。Unity と同じ）</para>
/// </summary>
class ScriptSystem {
public:
	// Script の作り方（SCRIPT(...) が型ごとに1つ作る）
	using CreateFunction = std::unique_ptr<Script> (*)();

	// ===== 初期化・終了 =====
	// EntityManager に「Entity が消える直前の知らせ」を頼む。EntityManager::Initialize の後に呼ぶ
	static void Initialize();
	// 全部の Script を消す（動いている物には OnDisable・OnDestroy を呼ぶ）。ほかの物を片付ける前に呼ぶ
	static void Release();

	// ===== 種類の登録（SCRIPT(...) から。ゲームは直接呼ばない）=====
	// 保存の表（ComponentSerializer）と Inspector の表（ComponentEditorRegistry）にも載せる
	// category は Add Component のカテゴリ（"Gameplay" か "Gameplay/サブカテゴリ"）
	static void RegisterType(const char* name, std::type_index type, CreateFunction create, const char* sourceFile, std::string category);

	// ===== 毎フレーム（SceneManager が再生中だけ呼ぶ）=====
	// 呼ぶ Script を選ぶ関数（Script が付いている Entity を受け取って、呼ぶなら true）
	using EntityFilter = bool (*)(Handle<Entity> entity);
	// Awake・OnEnable → Start → 当たり（OnTrigger〜・OnCollision〜）→ Update。SYSTEM(...) より前
	// filter を渡すと、選んだ Script だけ呼ぶ（ゲームのポーズ中に、開いている画面の中の Script だけ動かす）。nullptr なら全部
	static void Update(EntityFilter filter = nullptr);
	// LateUpdate。SYSTEM(...) の後（filter は Update と同じ）
	static void LateUpdate(EntityFilter filter = nullptr);
	// Destroy(Script) の予約を反映する（OnDisable・OnDestroy を呼んでから消す）。EntityManager::FlushDestroy の前に呼ぶ
	static void FlushDestroy();

	// ===== 付ける・探す・外す =====
	// その場で付けて返す（すでに付いていればそれを返す）
	// 登録されていない種類・居ないEntityなら nullptr。Awake・Start は、次のUpdate（再生中だけ）
	static Script* Add(Handle<Entity> entity, std::type_index type);
	// その種類の Script（ちょうどその型。Destroy を頼んだ物は見つからない）。無ければ nullptr。ポインタは使い捨て
	static Script* Get(Handle<Entity> entity, std::type_index type);
	// matchがtrue を返す最初の Script（GameObject::GetComponent が、親クラスの型でも探せるように使う）
	static Script* Find(Handle<Entity> entity, bool (*match)(const Script& script));
	// matchがtrue を返す Script を全部outに入れる（有効な Entity に付いている物だけ。outは最初に空にする）
	static void FindAll(bool (*match)(const Script& script), std::vector<Script*>& out);
	// entity に付いている動いている Script 全部の OnTimelineEvent を呼ぶ（TimelineSystem から）
	static void SendTimelineEvent(Handle<Entity> entity, const TimelineEvent& event);
	// 消す予約（フレームの終わりに消える。Script::Destroy から）
	static void RequestDestroy(Script* script);
	// その場で外す（エディタの Remove Component・Undo から。フレームの境目でだけ呼ぶ）
	static void RemoveNow(Handle<Entity> entity, std::type_index type);

	// ===== 写し（Undo・コピー用。シーンファイルと同じ JSON の文字）=====
	// Entity に付いている Script を全部、{"型の名前": {項目...}, ...} の文字にする（無ければ空の文字）
	static std::string Capture(Handle<Entity> entity);
	// Capture の文字から Script を付け直す（フレームの境目でだけ呼ぶ）
	static void Restore(Handle<Entity> entity, std::string_view text);
	// Script 1つ分の項目を、{項目...} の文字にする / 文字から戻す（Inspector の Undo 用）
	static std::string SaveFields(Script& script);
	static void LoadFields(Script& script, std::string_view text);

	// ===== 見せ方（Inspector・保存・読み込みで共通）=====
	// 「Enabled」→ Script::Fields の順に、ui に項目を渡す
	static void Describe(Script& script, ComponentUI& ui);
	// 種類の名前（SCRIPT(...) に書いた型の名前）。登録されていなければ "?"
	static const char* GetTypeName(const Script& script);
	// 付いている数（確認用）
	static size_t GetCount();

private:
	// Entity が消える直前（EntityManager から呼ばれる）。その Entity の Script を全部消す
	static void OnEntityDestroyed(Handle<Entity> entity);
	// 「OnEnable を呼んだ状態」を「今あるべき状態（Awake 済み・enabled・Entity が有効）」にそろえる
	static void SyncEnabled(Script& script);
	// Update などを呼んでよいか（Start 済み・有効・消す予約なし）
	static bool IsRunning(const Script& script);
	// 消す前に OnDisable・OnDestroy を呼ぶ（呼んでよい物にだけ）
	static void CallDestroyCallbacks(Script& script);
};