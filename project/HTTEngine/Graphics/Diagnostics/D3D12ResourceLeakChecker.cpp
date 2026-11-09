#include "HTTEngine/Graphics/Diagnostics/D3D12ResourceLeakChecker.h"

#include <wrl.h>
#include <dxgi1_6.h>
#include <DXGIDebug.h>
#include <d3d12sdklayers.h>

#pragma comment(lib, "dxguid.lib")


D3D12ResourceLeakChecker::~D3D12ResourceLeakChecker() {
	// リソースリークチェック
	Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
		debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
	}
}