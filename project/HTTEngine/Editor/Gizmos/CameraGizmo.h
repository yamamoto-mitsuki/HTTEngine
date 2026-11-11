#pragma once

// 前方宣言
class Camera;

/// <summary>
/// カメラをエディタで見せるためのギズモ
/// <para>Camera Component の置いてある所のアイコンと、Camera Path の線（Scene ビューだけ）</para>
/// <para>Camera Follow の Dead Zone・Soft Zone などの線（Game ビューでは、画面の上の範囲にぴったり重なる）</para>
/// <para>World の範囲・focus・Bounds は Scene・Game ビューの両方、Screen の範囲は Game ビューだけ（メインカメラの物だけ）</para>
/// <para>Camera Orbit の回る輪・回る中心・カメラを置く位置の線（Scene ビューだけ）</para>
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
	// Camera Follow の範囲の線を出すか（View メニューから全部まとめて切り替える。1つずつは Inspector の Show Gizmo）
	static bool& GetShowFollowZones();
	// Camera Orbit の線を出すか（View メニューから全部まとめて切り替える。1つずつは Inspector の Show Gizmo）
	static bool& GetShowOrbits();
};
