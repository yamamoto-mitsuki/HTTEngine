#pragma once
#include "HTTEngine/Graphics/IBL/IBLEnvironment.h"
#include "HTTEngine/Scene/IScene.h"

/// <summary>
/// シーンのクラスを書いていないシーン（REGISTER_SCENE していない名前のシーン）で使う、エンジンのシーン
/// <para>Entity だけで動く。毎フレームの処理は SYSTEM(...)、描画は ModelRenderSystem がする</para>
/// <para>IBL（PBR のモデルの環境光）は、天球と同じエンジンの既定の画像から作る</para>
/// </summary>
class DefaultScene final : public IScene {
public:
	void Initialize() override;
	void Update() override {}
	void Draw() override;
	void Finalize() override {}
	// シーンファイルがまだ無い（新しいシーン）ときの最初の配置：カメラと平行光源（Unity の新しいシーンと同じ）
	void CreateDefaultEntities() override;
	// 環境光（Primitive Renderer・Elastic Surface の PBR にも使ってもらう）
	IBLEnvironment* GetEnvironment() override { return &environment_; }

private:
	IBLEnvironment environment_; // 環境光。PBR のモデルはこれが無いと描かれない
};