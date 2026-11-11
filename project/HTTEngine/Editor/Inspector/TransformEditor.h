#pragma once
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Components/Core/TransformComponent.h"


/// <summary>
/// TransformComponentのInspector（全Entityが必ず持つので、外せない）
/// <para>Position・Rotation・Scale の左の「XYZ」ボタンを押すと、X・Y・Z を一緒に変える（Scale は形を保ったまま大きさだけ変わる）</para>
/// </summary>
class TransformEditor : public TypedComponentEditor<TransformComponent> {
public:
	const char* GetName() const override { return "Transform"; }
	const char* GetCategory() const override { return "Core"; }
	const char* GetDescription() const override { return "位置・回転・大きさ。どの Entity も必ず持つ（外せない）。親子にすると、親の動きが子にも掛かる"; }

protected:
	void DrawComponent(TransformComponent& transform) const override;

private:
	// Position・Rotation・Scale の順に、X・Y・Z を一緒に変えるか（「XYZ」ボタンで切り替える）
	// 全部の Entity で共通。エディタの見せ方なので、シーンには保存しない（DrawComponent は const なので mutable）
	mutable std::array<bool, 3> isLinked_ = {false, false, false};
};