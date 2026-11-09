#pragma once
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"

/// <summary>
/// Scene ビューだけで隠す印（Hierarchy の目のアイコン。Unity の Scene Visibility と同じ）
/// <para>隠した物は Scene ビューに描かない・クリックで選べない。Game ビューの絵と、ゲームの動き（当たり・Script）は何も変わらない</para>
/// <para>EntityId で覚えるので、Play / Stop で Entity を作り直しても隠したまま。シーンファイルには書かない（エディタを閉じる・別のシーンを開くと全部見える）</para>
/// <para>描く System が IsHidden を呼んで、Config の isSceneHidden（DrawTarget）に入れる。製品版にも入っている（製品版では誰も隠さないので、いつも false）</para>
/// </summary>
namespace SceneVisibility {

// 自分で隠したか（自分の目を閉じたか）
bool IsHiddenSelf(Handle<Entity> entity);

// 自分か、親・親の親…のどれかを隠しているか（親を隠すと、子も一緒に隠れる）
bool IsHidden(Handle<Entity> entity);

// 目を閉じる・開く（隠していれば見せる、見せていれば隠す）
void Toggle(Handle<Entity> entity);

// 全部見せる（別のシーンを開いたとき。前のシーンの EntityId で、別のシーンの物を隠さないように）
void Clear();

// ===== クリックで選べる物を絞る（画面（UI Screen）の編集中。絵はそのまま描く）=====
// root と、その子孫だけ選べるようにする（無効な Handle なら、絞らない）
void SetPickRoot(Handle<Entity> root);
// Scene ビューでクリックして選べるか（隠していない・絞っていればその中の物）
bool IsPickable(Handle<Entity> entity);
} // namespace SceneVisibility