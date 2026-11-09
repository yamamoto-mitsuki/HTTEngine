#pragma once
#include <d3d12.h>


/// <summary>
/// D3D12 のデバッグ・調査用の関数（デバッグレイヤー・メッセージをログへ・DRED）
/// <para>使うデバイスは引数で受け取る</para>
/// <para>Debug 以外（Development・Release）では、ほとんど何もしない</para>
/// </summary>
namespace GraphicsDebug {

/// <summary>
/// デバッグレイヤー・GPU側の検証・DREDを有効にする。デバイスを作る前に呼ぶ
/// <para>PIX でキャプチャしているときは、デバッグレイヤーを有効にしない</para>
/// </summary>
/// <returns>DXGIファクトリーを作るときのフラグ（DXGIのデバッグを使うなら DXGI_CREATE_FACTORY_DEBUG）</returns>
UINT EnableDebugLayer();

/// <summary>
/// エラー・警告で止まる・ログに出すように設定する。デバイスを作った後に呼ぶ
/// </summary>
void SetupInfoQueue(ID3D12Device* device);

/// <summary>
/// デバイスが削除された（GPUが落ちた）ときに呼ぶ。DRED の記録（どこで止まったか・不正なアクセス）をログに出して、assert で止める
/// </summary>
/// <param name="hr">GetDeviceRemovedReason の戻り値</param>
void ReportDeviceRemoved(ID3D12Device* device, HRESULT hr);

} // namespace GraphicsDebug