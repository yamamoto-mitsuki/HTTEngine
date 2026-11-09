#pragma once
#include "HTTEngine/Graphics/Pipeline/ShaderConstants.h"
#include "HTTEngine/Graphics/PostEffect/PostProcess.h"
#include "HTTEngine/Scene/Skybox.h"
#include <cstdint>
#include <d3d12.h>
#include <memory>
#include <string>
#include <wrl.h>

// 前方宣言
class Skybox;
class Camera;
class RenderWindow;
class RenderTexture;

// ビューごとに切れる物（エディタの View メニュー）。SceneRenderer::RenderToTexture に渡す
// クラスの外に置くのは、引数の既定値（= {}）に使うため（クラスの中の struct の初期値は、クラスを書き終わるまで使えない）
struct SceneViewOptions {
	bool fog = true;          // 霧（Fog Component）を掛ける
	bool depthOfField = true; // 被写界深度（Depth Of Field Component）を掛ける（ポストエフェクトを掛けるときだけ）
	bool editorGrid = true;   // エディタのグリッド（DrawLayer::EditorGrid）を描く
	bool editorGizmo = false; // エディタのギズモ（DrawLayer::EditorGizmo）を描く（Scene ビューだけ true）
	bool sceneHidden = true;  // Hierarchy の目で隠した物（DrawTarget::isSceneHidden）を描く（Scene ビューだけ false）
	bool cameraVolume = true; // カメラごとのポストエフェクト（Camera Volume）を掛ける（Scene ビューだけ false。別のカメラから見るので、ピントが合わない）
};

/// <summary>
/// 1カメラ分のシーンを RenderTextureに描くクラス
/// </summary>
class SceneRenderer {
public:
	// ===== Passkey Idiom =====
	class ConstructorKey {
	private:
		ConstructorKey() = default;
		friend class SceneRenderer;
	};
	explicit SceneRenderer(ConstructorKey) {} // 鍵を受け取るコンストラクタ

	SceneRenderer(const SceneRenderer&) = delete;
	SceneRenderer& operator=(const SceneRenderer&) = delete;

	static void Initialize();
	static void Release();
	static void InitializeSkybox();
	static void InitializePost(uint32_t width, uint32_t height);

	// --- camera視点で targetにシーン1回分を描く ---
	// Textureに描く（options：このビューだけ霧・被写界深度・グリッドを切るとき）
	static void RenderToTexture(const Camera* camera, RenderTexture* target, const std::wstring& windowTitle, PostEffectMode mode = PostEffectMode::All, const SceneViewOptions& options = {});
	// Windowに描く
	static void RenderToWindow(const Camera* camera, RenderWindow* window, const std::wstring& windowTitle, float windowWidth, float windowHeight);

	// カメラのリングバッファ参照位置リセット
	static void ResetViewIndex() {
		instance_->cameraSlot_ = 0;
		instance_->skybox_->ResetSlot();
		if (instance_->postProcess_) {
			instance_->postProcess_->ResetSlot();
		}
	}

	static Skybox* GetSkybox() { return instance_->skybox_.get(); }
	static PostProcess* GetPost() { return instance_->postProcess_.get(); }

	// --- 霧（カメラの定数バッファに入れて、3D のシェーダーが色に混ぜる）---
	// 霧の値（FogSystem が、シーンの Fog Component から毎フレーム書く。Fog Component が無ければ「霧なし」を書く）
	static void SetFog(const FogData& fog) { instance_->fog_ = fog; }

	// --- 被写界深度（ポストエフェクトで、ピントの前後を深度でぼかす）---
	// 被写界深度の値（DepthOfFieldSystem が、シーンの Depth Of Field Component から毎フレーム書く。無ければ「ぼかさない」を書く）
	static void SetDepthOfField(const DepthOfFieldSettings& settings) { instance_->depthOfField_ = settings; }

	// --- カメラごとのポストエフェクト ---
	// メインカメラの上書き（CameraSystem が毎フレーム書く。Camera Volume が無ければ「上書きなし」を書く）
	static void SetCameraPostOverrides(const PostOverrides& overrides) { instance_->cameraPostOverrides_ = overrides; }

private:
	~SceneRenderer() = default;
	// このビューだけの設定を覚える（霧はカメラの定数バッファ、被写界深度はポストエフェクト、グリッドは RenderQueue が見る）
	static void ApplyViewOptions(const SceneViewOptions& options);
	// このビューに掛けるカメラの上書き（SceneViewOptions で切ってあれば「上書きなし」）
	static PostOverrides GetViewPostOverrides();
	// このビューに掛ける被写界深度（View メニューで切ってあれば「ぼかさない」）
	static DepthOfFieldSettings GetViewDepthOfField();
	static void SetViewportAndScissor(const ViewRect& rect);
	static RenderTexture* RenderWorldForPost(const Camera* camera, const std::wstring& windowTitle);
	static void RenderOpaque(const Camera* camera, D3D12_GPU_VIRTUAL_ADDRESS camCB, const std::wstring& windowTitle, RenderTexture& work);
	static void RenderTransparent(D3D12_GPU_VIRTUAL_ADDRESS camCB, const std::wstring& windowTitle);
	static void RenderUI(const std::wstring& windowTitle, const ViewRect& rect);
	static D3D12_GPU_VIRTUAL_ADDRESS UploadCameraCB(const Camera* camera);

	// シングルトン
	friend struct std::default_delete<SceneRenderer>;
	static std::unique_ptr<SceneRenderer> instance_;
	std::unique_ptr<Skybox> skybox_;
	std::unique_ptr<RenderTexture> postRT_;    // 3D を描く置き場（HDR・深度を読める）
	std::unique_ptr<PostProcess> postProcess_; // ポストエフェクトと、HDR の絵 → 画面（トーンマップ）
	Microsoft::WRL::ComPtr<ID3D12Resource> cameraCB_;
	uint8_t* cameraCBMapped_ = nullptr;
	uint32_t cameraSlot_ = 0;

	FogData fog_;                       // 霧の値（初期値は霧なし）
	DepthOfFieldSettings depthOfField_; // 被写界深度の値（初期値はぼかさない）
	PostOverrides cameraPostOverrides_; // カメラごとのポストエフェクトの上書き（初期値は上書きなし）
	SceneViewOptions viewOptions_;      // 今描いているビューの設定（RenderToTexture・RenderToWindow が毎回決める）
};