#pragma once
#include <externals/imgui/imgui.h>

// Project ウィンドウなどに出すアイコンの種類
// 画像は HTTEngine/Resources/Editor/Icons/<名前>.png（名前はこの並びの名前そのまま。Folder.png など）
enum class AssetIcon {
	Folder,     // フォルダ
	FolderOpen, // 中を見ているフォルダ（左の木。画像が無ければ Folder で描く）
	Scene,      // シーン（.scene.json）
	Model,      // モデル（.obj .gltf .glb .fbx）
	Texture,    // 画像（小さい絵を作るまでの間・作れなかったとき）
	Sound,      // 音（.wav .mp3）
	Code,       // コード（.cpp .h .hlsl など。ダブルクリックで Code ウィンドウ）
	File,       // それ以外
};

/// <summary>
/// エディタのアイコン
/// <para>画像が無い種類は、色の付いた形と頭文字で代わりに描く（画像を置くだけで差し替わる）</para>
/// </summary>
namespace AssetIcons {
// アイコンの画像を読む（ImGuiManager::Initialize から1回）。置いていない画像は飛ばす
void Initialize();
// min〜max の四角いっぱいに icon を描く
void Draw(ImDrawList* drawList, AssetIcon icon, const ImVec2& min, const ImVec2& max);
} // namespace AssetIcons