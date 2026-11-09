#pragma once

// 前方宣言
class Camera;

/// <summary>
/// カメラをエディタの Scene ビューで見せるためのギズモ（Game ビューには出さない）
/// <para>Camera Component の置いてある所のアイコンと、Camera Path の線を描く</para>
/// </summary>
class CameraGizmo {
public:
	static void Initialize();

	// 描く物を積む（1フレームに1回）
	static void Draw(Camera* camera);

	// アイコンを出すか（View メニューから切り替える。クリックで選べるかも、これで決まる）
	static bool& GetShowIcons();
	// Camera Path の線を出すか（View メニューから切り替える）
	static bool& GetShowPaths();
};