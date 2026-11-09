#pragma once
#include <memory>
#ifdef USE_IMGUI
#include <vector>
#include <string>
#include <externals/imgui/imgui.h>
#include <externals/imgui/ImGuizmo.h>
#include "HTTEngine/Math/MathIncludes.h"

// 前方宣言
class Camera;


/// <summary>
/// Window上にカメラ軸などを表示するクラス
/// </summary>
class EditorOverlay {
public:
	// ===== Passkey Idiom =====
	class ConstructorKey {
	private:
		ConstructorKey() = default;
		friend class EditorOverlay;
	};
	explicit EditorOverlay(ConstructorKey) {} // 鍵を受け取るコンストラクタ

	EditorOverlay(const EditorOverlay&) = delete;
	EditorOverlay& operator=(const EditorOverlay&) = delete;

	static void Initialize();
	static void Release();

	/// <summary>
	/// オーバーレイを描画する。
	/// </summary>
	static void Draw(ImVec2 imageMin, ImVec2 imageMax, ImVec2 imageSize);

	/// <summary>
	/// 任意のEditorを描画するか確認するクラス
	/// </summary>
	static void DrawSettings();

	/// <summary>
	/// カメラを登録する（weak_ptr で覚えるだけ。持ち主は EditorViewport）
	/// </summary>
	static void SetActiveCamera(const std::shared_ptr<Camera>& camera) { instance_->activeCamera_ = camera; }
	// 登録したカメラ。もう消えている・未登録なら空の shared_ptr
	static std::shared_ptr<Camera> GetActiveCamera() { return instance_ ? instance_->activeCamera_.lock() : nullptr; }



private:
	~EditorOverlay() = default;

	// 内部ヘルパー
	static void DrawAxisGizmo(ImDrawList* dl, const Camera* cam, ImVec2 imageMin, ImVec2 imageMax, ImVec2 imageSize);

	// シングルトン
	friend struct std::default_delete<EditorOverlay>;
	static std::unique_ptr<EditorOverlay> instance_;
	std::weak_ptr<Camera> activeCamera_; // 見ているだけ（EditorViewport が消えても lock() が空を返す）
	bool isShowAxis_ = true;
};
#else
class Camera;

class EditorOverlay {
public:
	static void Initialize() {}
	static void Release() {}
	static void DrawSettings() {}
	static void SetActiveCamera(const std::shared_ptr<Camera>&) {}
	static std::shared_ptr<Camera> GetActiveCamera() { return nullptr; }
};
#endif