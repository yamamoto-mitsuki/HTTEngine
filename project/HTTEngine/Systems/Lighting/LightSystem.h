#pragma once
#include <memory>

// 前方宣言
class Camera;


/// <summary>
/// ライトのComponent（Directional / Point / Spot）を持つEntityを集めて、GPU用にまとめる
/// <para>ライトの実体はEntityManagerが型別に持つ。ここはComponentを登録して、毎フレーム読むだけ</para>
/// <para>GPUバッファは持たない。GPUへ送るのはRenderContextの役目</para>
/// </summary>
class LightSystem {
public:
	// ===== Passkey Idiom =====
	class ConstructorKey {
	private:
		ConstructorKey() = default;
		friend class LightSystem;
	};
	explicit LightSystem(ConstructorKey) {} // 鍵を受け取るコンストラクタ

	// EntityManagerにライトのComponentを登録する（EntityManager::Initializeの後に呼ぶ）
	static void Initialize();
	static void Release();

	/// <summary>
	/// 更新の最後（EntityManager::UpdateTransformsの後）に呼ぶ。
	/// このフレームのライトをGPU用の形にまとめてRendererへ渡す
	/// </summary>
	static void Update();

	/// <summary>
	/// 全ライトのギズモを描く（エディタ用）
	/// </summary>
	static void DrawGizmos(Camera* camera);

private:
	// シングルトン
	friend struct std::default_delete<LightSystem>;
	~LightSystem() = default;
	LightSystem(const LightSystem&) = delete;
	LightSystem& operator=(const LightSystem&) = delete;
	static std::unique_ptr<LightSystem> instance_;

	void CollectForGPU(); // Component → GPU用データにまとめてRendererへ渡す

	// 上限を超えたときの警告は1回だけ出す
	bool hasWarnedDirectionalLightLimit_ = false;
	bool hasWarnedPointLightLimit_ = false;
	bool hasWarnedSpotLightLimit_ = false;
};