#pragma once
#include "MyEngine/ECS/ComponentSerializer.h"


/// <summary>
/// 2つの JSON の違いを取る（置いた所ごとの上書きを作るため）。JSON だけを見る（Entity は見ない）
/// <para>差分は merge_patch（RFC 7386）の形。before.merge_patch(差分) で after になる</para>
/// </summary>
class PrefabDiff {
public:
	// 2つの値が同じか。小数は、とても小さな違い（丸め）なら同じと見る。整数（Entity の番号など）は、そのまま比べる
	static bool IsSameValue(const SceneJson& a, const SceneJson& b);
	// {...} の before を after にするための差分。同じなら空の {}。{...} どうしは項目ごと、それ以外は違えば after を丸ごと
	static SceneJson MakePatch(const SceneJson& before, const SceneJson& after);
	// Component の違い（{"型の名前": {...}} どうし）。patches に上書き（before に無い型は丸ごと）、removed に外した型の名前を足す
	static void DiffComponents(const SceneJson& before, const SceneJson& after, SceneJson& patches, SceneJson& removed);
};
