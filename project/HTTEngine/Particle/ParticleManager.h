#pragma once
#include <memory>
#include <list>

#include <d3d12.h>
#include <wrl.h>

#include "HTTEngine/Graphics/Pipeline/ShaderConstants.h"
#include "HTTEngine/Graphics/Pipeline/RenderStates.h"
#include "HTTEngine/Math/MathIncludes.h"

// 前方宣言
class Camera;

// 1個分のデータ
struct Particle {
	Transform transform;
	Vector3 velocity;
	Vector4 color;
	float lifeTime = 0.0f;
	float currentTime = 0.0f;
	Matrix4x4 world; // ビルボード込みのワールド行列
	bool isBillboard = true; // ビルボード
};

// エミッターごとに設定する項目
struct ParticleGroupSetting {
	uint32_t textureHandle = 0; // 使用するTexture
	Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f}; // 全体の色
	BlendMode blendMode = BlendMode::Add; // ブレンドモード
};

/// <summary>
/// バッファ / SRV / 描画を持つシングルトン
/// </summary>
class ParticleManager {
public:
	// ===== Passkey Idiom =====
	class ConstructorKey {
	private:
		ConstructorKey() = default;
		friend class ParticleManager;
	};
	explicit ParticleManager(ConstructorKey) {} // 鍵を受け取るコンストラクタ

	static constexpr uint32_t kMaxParticles = 4096;// 1グループの最大数
	static constexpr uint32_t kInvalidGroup = 0xFFFFFFFF; // 無効なグループハンドル

	static void Initialize();
	static void Release();
	static void Update();
	static void Draw();

	/// <summary>
	/// グループ（エミッター単位の描画設定 + パーティクルリスト）を作成し、ハンドルを返す
	/// </summary>
	static uint32_t CreateGroup(const ParticleGroupSetting& setting);

	/// <summary>
	/// グループの描画設定を変更する（エミッターで変更した値を反映したいとき）
	/// </summary>
	/// <param name="group"></param>
	/// <param name="setting"></param>
	static void SetGroupSetting(uint32_t group, const ParticleGroupSetting& setting);

	/// <summary>
	/// パーティクルを1個グループに登録する
	/// </summary>
	static void Register(uint32_t group, const Particle& p);

	/// <summary>
	/// 全部のグループを、中の粒ごと消す（SceneManager がシーンを入れ替えるときに呼ぶ）
	/// <para>グループはエミッター（シーンが持つ）が初めて Emit したときに作るので、古いシーンのエミッターが消えた後なら、全部捨ててよい</para>
	/// <para>捨てないと、シーンを読み直す・Play / Stop するたびに、使われないグループが増えていく</para>
	/// </summary>
	static void ClearGroups() {
		if (instance_) {
			instance_->groups_.clear();
		}
	}

	// セッター（weak_ptr で覚えるだけ。カメラの持ち主にはならない）
	static void SetCamera(const std::shared_ptr<Camera>& camera) { instance_->camera_ = camera; }

private:
	// グループ
	struct ParticleGroup {
		ParticleGroupSetting settings;
		std::list<Particle> particles;
	};

	// シングルトン
	friend struct std::default_delete<ParticleManager>;
	~ParticleManager() = default;
	ParticleManager(const ParticleManager&) = delete;
	ParticleManager& operator=(const ParticleManager&) = delete;
	static std::unique_ptr<ParticleManager> instance_;

	// ビルボードに使うカメラ。持ち主は CameraSystem（またはシーン）なので weak_ptr で「見ているだけ」にする
	// カメラが先に消えても、lock() が空を返すので、消えたカメラを触って落ちることがない
	std::weak_ptr<Camera> camera_;
	std::vector<ParticleGroup> groups_;
};