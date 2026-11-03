#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <functional>

#include "MyEngine/Core/Handle.h"
#include "MyEngine/ECS/ComponentStorage.h"
#include "MyEngine/ECS/Entity.h"

// 前方宣言
class ComponentEditor;


/// <summary>
/// エディタの操作の履歴（Undo・Redo）と、コピー・貼り付け
/// <para>操作はRequestで予約だけして、次のフレームの最初（Flush）に順番どおり実行する（UIを描いている途中でEntityを増減させないため）</para>
/// <para>相手はHandleではなくEntityIdで覚える（消して戻すとHandleは変わるが、EntityIdは変わらない）</para>
/// <para>仕組みの解説は Docs/Tasks/Editor.md の「Undoの仕組み」</para>
/// </summary>
class EditorHistory {
public:
	// ===== 毎フレーム =====
	// フレームの最初に1回呼ぶ。予約された操作を、頼まれた順に実行する
	static void Flush();
	// シーンの切り替え・終了で呼ぶ。履歴を捨てる（コピーした物は残すので、Stopの後にも貼れる）
	static void Clear();
	// 今の履歴を脇へよけて、空の履歴から始める（画面（UI Screen）の編集に入るとき。シーンの操作を Ctrl+Z で戻さないように）
	static void Suspend();
	// 脇へよけた履歴に戻す（今の履歴は捨てる。画面の編集を終えたとき。よけていなければ何もしない）
	static void Resume();
	// 履歴が動いた回数（積んだ・戻した・やり直した・捨てた）。前に見たときと同じなら、Entity は変わっていない
	// SceneManager が「保存していない変更があるか（*）」を調べ直すかどうかに使う
	static uint64_t GetChangeCount();

	// ===== Undo / Redo =====
	static bool CanUndo();
	static bool CanRedo();
	static void RequestUndo();
	static void RequestRedo();

	// ===== Entityの操作 =====
	// parentが無効ならroot。componentsを渡すと、それを付けた状態で作る（Transformを渡すと初期位置・向きになる）
	static void RequestCreate(const std::string& name, Handle<Entity> parent, std::vector<ComponentSnapshot> components = {});
	static void RequestDestroy(Handle<Entity> handle);                         // 子も一緒に消える
	static void RequestRename(Handle<Entity> handle, const std::string& name); // 空の名前にはしない
	static void RequestSetActive(Handle<Entity> handle, bool isActive);
	static void RequestReparent(Handle<Entity> child, Handle<Entity> parent); // parentが無効ならrootへ。見た目の位置は変えない

	// ===== コピー・貼り付け =====
	static bool CanPaste();
	static void Copy(Handle<Entity> handle);             // その場で写す（何も変えないので予約しない）
	static void RequestPaste(Handle<Entity> parent);     // parentの子として貼る（無効ならroot）
	static void RequestDuplicate(Handle<Entity> handle); // 同じ親の下に複製する

	// ===== Prefab =====
	// Prefab から置いた物を、Prefab のとおりに作り直す（置いた所ごとの上書きと、あとから足した子が消える。一番上の名前・有効・親・Transform は残す）
	static void RequestRevertPrefab(Handle<Entity> root);

	// ===== Component =====
	static void RequestAddComponent(Handle<Entity> handle, const ComponentEditor& editor);
	static void RequestRemoveComponent(Handle<Entity> handle, const ComponentEditor& editor);
	// Inspectorが描く前と描いた後の写し（ComponentEditor::Save）を渡す。変わっていたら、Component全体の「編集前」と「編集後」を覚える
	static void RecordComponentChange(Handle<Entity> handle, const ComponentEditor& editor, const std::vector<std::byte>& before, const std::vector<std::byte>& after);
	// 操作の区切り（マウスを離した・入力を確定した）で呼ぶ。ここまでの変更を1件の履歴にまとめる
	static void CommitComponentChanges();
	
	// ===== Entity の外の値（Parameters ウィドウの調整項目など）
	// もう変えた値を、1件の履歴として積む。undo は「前の値に戻す」、redo は「変えた後の値にする」処理
	// 相手が見つからないなどで戻せなかったら false を返す（履歴が消える）
	static void Record(std::function<bool()> undo, std::function<bool()> redo);
};