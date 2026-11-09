#pragma once
#include <memory>

// 前方宣言
class Camera;
class IBLEnvironment;


/// <summary>
/// 全 System の呼び出しをまとめた順番表（初期化・終了・更新・描画）
/// <para>System を足したら、ここの必要な関数に1行足す</para>
/// <para>順番に意味があるので、並びを変えるときは各行のコメントを確かめる</para>
/// <para>状態は持たない（カメラ・再生中かどうかは WindowManager から受け取る）</para>
/// </summary>
namespace EngineSystems {

/// <summary>
/// EntityManager と全 System を初期化する（Component の登録もここ）
/// </summary>
void Initialize();

/// <summary>
/// 動いている Script を片付ける。ほかの物を片付ける前に呼ぶ
/// </summary>
void ReleaseScripts();

/// <summary>
/// 全 System と EntityManager を片付ける。シーン（ウィンドウ）の破棄より後に呼ぶ
/// </summary>
void Release();

/// <summary>
/// 前フレームの予約を反映する。シーンの更新より前に呼ぶ
/// </summary>
void FlushPending();

/// <summary>
/// 全 System を更新する。シーンの更新より後に呼ぶ
/// </summary>
/// <param name="isGameRunning">再生中・一時停止中なら true</param>
/// <param name="gameCamera">ゲームのカメラ（無ければ nullptr）</param>
void Update(bool isGameRunning, const std::shared_ptr<Camera>& gameCamera);

/// <summary>
/// 全 System の描画リクエストを積む
/// </summary>
/// <param name="gameCamera">ゲームのカメラ（借りるだけ。無ければ nullptr）</param>
/// <param name="gameEnvironment">シーンの環境光（借りるだけ。無ければ nullptr）</param>
void Draw(Camera* gameCamera, IBLEnvironment* gameEnvironment);

} // namespace EngineSystems
