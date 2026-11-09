#include "HTTEngine/Graphics/Diagnostics/GraphicsDebug.h"

#include <array>
#include <format>
#include <stacktrace>
#include <string>
#include <string_view>

#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <wrl.h>

#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Graphics/Renderer/RenderContext.h"
#include "HTTEngine/String/ConvertString.h"

namespace {
// Breadcrumbの名前を分かりやすいようにする
std::string BreadcrumbOpName(D3D12_AUTO_BREADCRUMB_OP op) {
	switch (op) {
	case D3D12_AUTO_BREADCRUMB_OP_BEGINEVENT:
		return "BeginEvent";
	case D3D12_AUTO_BREADCRUMB_OP_ENDEVENT:
		return "EndEvent";
	case D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED:
		return "DrawInstanced";
	case D3D12_AUTO_BREADCRUMB_OP_DRAWINDEXEDINSTANCED:
		return "DrawIndexedInstanced";
	case D3D12_AUTO_BREADCRUMB_OP_DISPATCH:
		return "Dispatch";
	case D3D12_AUTO_BREADCRUMB_OP_COPYRESOURCE:
		return "CopyResource";
	case D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER:
		return "ResourceBarrier";
	case D3D12_AUTO_BREADCRUMB_OP_CLEARRENDERTARGETVIEW:
		return "ClearRTV";
	case D3D12_AUTO_BREADCRUMB_OP_CLEARDEPTHSTENCILVIEW:
		return "ClearDSV";
	case D3D12_AUTO_BREADCRUMB_OP_PRESENT:
		return "Present";
	case D3D12_AUTO_BREADCRUMB_OP_SETMARKER:
		return "SetMarker";
	default:
		return std::format("Op({})", static_cast<int>(op));
	}
}

// .source_file()のファイルパスを省略する
std::string ShortenPath(std::string_view path) {
	constexpr std::string_view marker = "repos\\"; // ここより前を捨てる
	auto pos = path.rfind(marker);
	return (pos == std::string_view::npos) ? std::string(path) : std::string(path.substr(pos + marker.size()));
}

// スタックの1段の説明（"モジュール!関数名+0x12"）から、関数名だけを取り出す
std::string FuncOf(const std::stacktrace_entry& f) {
	std::string d = f.description();
	auto bang = d.find('!');
	auto plus = d.rfind('+');
	size_t start = (bang == std::string::npos) ? 0 : bang + 1;
	size_t end = (plus == std::string::npos) ? d.size() : plus;
	return d.substr(start, end - start);
}

// D3D12のデバックメッセージをLogに出力する（SetupInfoQueue で登録する）
void CALLBACK D3D12DebugMessageCallback(D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID, LPCSTR description, void*) {
	// エラーメッセージ末尾の改行を削除する
	std::string text = description ? description : "";
	while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
		text.pop_back();
	}
	std::string msg = std::string("[D3D12] ") + text;
	// CPU検証かGBVか確認
	bool isGBV = (text.rfind("GPU-BASED VALIDATION", 0) == 0);
	if (isGBV) {
		msg = "<GBV: 実行時報告。このメッセージを呼んでください。>" + msg;
	} else {
		msg = "<CPU検証: 同期報告。スタックを見てください。>" + msg;
	}

	//  情報の重要度によってLog出力を変える
	switch (severity) {
	// --- やばい ---
	case D3D12_MESSAGE_SEVERITY_CORRUPTION:
	case D3D12_MESSAGE_SEVERITY_ERROR:
		LogManager::Error(msg);
		// 呼び出し元のスタックを取得してlogに出す
		for (const std::stacktrace_entry& frame : std::stacktrace::current()) {
			std::string file = frame.source_file();
			// 自分のコード以外スキップ
			if (file.empty() || file.find("repos\\") == std::string::npos) {
				continue;
			}

			// D3D12内部などソースファイルがないフレームは飛ばす
			if (!frame.source_file().empty()) {
				LogManager::Error(std::format(" at {}:{} {}", ShortenPath(file), frame.source_line(), FuncOf(frame)));
			}
		}
		HTT_ASSERT_MSG(false, "D3D12DebugLayerがエラーを検出しました");
		break;
	// --- 警告 ---
	case D3D12_MESSAGE_SEVERITY_WARNING:
		LogManager::Warning(msg);
		break;
	}
}
} // namespace


//=============================================================================
// デバッグレイヤー・DREDを有効にする（デバイスを作る前）
//=============================================================================
UINT GraphicsDebug::EnableDebugLayer() {
	// Graphics Toolsがある場合はDXGIのデバッグを有効化するため、フラグを立てる
	UINT dxgiFactoryFlags = 0;

#ifdef _DEBUG
	// ===== PIXアタッチ検出 =====
	// PIXのキャプチャDLLが注入されている場合、PIX自身がバックバッファをコピーするため
	// デバッグレイヤーの状態追跡と食い違い、偽のResourceBarrierエラーが出る。
	// （例: PRESENT→RENDER_TARGETのバリアで「現在COPY_SOURCE」と報告される）
	// PIXでのキャプチャ中はデバッグレイヤーを無効化する。
	const bool isPIXAttached = (GetModuleHandleW(L"WinPixGpuCapturer.dll") != nullptr);

	// ===== D3D12デバッグレイヤー =====
	Microsoft::WRL::ComPtr<ID3D12Debug1> debugController = nullptr;
	if (!isPIXAttached && SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
		// デバックレイヤーを有効化
		debugController->EnableDebugLayer();
		// GPU側でもチェックを行う
		debugController->SetEnableGPUBasedValidation(TRUE);
		// Graphics ToolsがあるのでDXGIのデバッグも有効化する
		dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
	}

	if (isPIXAttached) {
		LogManager::Warning("PIX（WinPixGpuCapturer.dll）を検出したためD3D12デバッグレイヤーを無効化します");
	}

	// ===== DRED（Device Removed Extended Data） =====
	Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dredSettings = nullptr;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings)))) {
		// 最後に実行したGPU処理を記録
		dredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
		// ページフォルト情報：不正アクセスしたアドレス/リソースを記録
		dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
	}
#endif

	return dxgiFactoryFlags;
}

//=============================================================================
// エラー・警告で止まる・ログに出すように設定する（デバイスを作った後）
//=============================================================================
void GraphicsDebug::SetupInfoQueue([[maybe_unused]] ID3D12Device* device) {
#ifdef _DEBUG
	// --- デバッグ時にエラー・警告で止まるよう設定（ID3D12） ---
	Microsoft::WRL::ComPtr<ID3D12InfoQueue1> infoQueue = nullptr;
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
		// エラー・警告でも止まるようにする
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
		// Logにも出力するようにする
		DWORD cookie = 0;
		infoQueue->RegisterMessageCallback(D3D12DebugMessageCallback, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &cookie);
		// 抑制するメッセージ
		std::array<D3D12_MESSAGE_ID, 1> denyIds = {D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE};
		std::array<D3D12_MESSAGE_SEVERITY, 1> severities = {D3D12_MESSAGE_SEVERITY_INFO};
		D3D12_INFO_QUEUE_FILTER filter = {};
		filter.DenyList.NumIDs = static_cast<UINT>(denyIds.size());
		filter.DenyList.pIDList = denyIds.data();
		filter.DenyList.NumSeverities = static_cast<UINT>(severities.size());
		filter.DenyList.pSeverityList = severities.data();
		// 指定したメッセージの表示を抑制する
		infoQueue->PushStorageFilter(&filter);
	}
	// --- DXGI版 ---
	Microsoft::WRL::ComPtr<IDXGIInfoQueue> dxgiInfoQueue = nullptr;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiInfoQueue)))) {
		// エラー・警告で止まるようにする
		dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);
		dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
		dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING, true);
	}
#endif
}

//=============================================================================
// Deviceロスト時に呼ぶ。DRED（Device Removed Extended Data）の記録をLogに出す
//=============================================================================
void GraphicsDebug::ReportDeviceRemoved([[maybe_unused]] ID3D12Device* device, HRESULT hr) {
	LogManager::Error(std::format("[DRED] Device removed! reason = 0x{:08X}", uint32_t(hr)));

#ifdef _DEBUG
	// DREDの読み出し
	Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData2> dred;
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&dred)))) {
		// エラーの名前を安全に取り出す
		auto nameOf = [](const char* a, const wchar_t* w) -> std::string {
			if (w) {
				return ConvertString(std::wstring(w));
			}
			if (a) {
				return a;
			}
			return "(no name)";
		};

		// ===== 1.Auto-Breadcrumbs（GPUがどこまで処理を実行して、止まってしまったか） =====
		D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 bc{};
		if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput1(&bc))) {
			// コマンドリストの数分ループする
			for (const D3D12_AUTO_BREADCRUMB_NODE1* cmdListNode = bc.pHeadAutoBreadcrumbNode; cmdListNode != nullptr; cmdListNode = cmdListNode->pNext) {
				// GPUが完了した数を取得する。
				UINT32 done = cmdListNode->pLastBreadcrumbValue ? *cmdListNode->pLastBreadcrumbValue : 0;
				// 全部完了していたら原因のコマンドリストではないではない
				if (done >= cmdListNode->BreadcrumbCount) {
					continue;
				}

				// 止まった命令（done）を囲んでいるマーカー名を探す
				// BreadcrumbIndex が done 以下で最大のものがいた区間
				std::string marker = "(no marker)";
				UINT best = 0;
				bool found = false;
				for (UINT i = 0; i < cmdListNode->BreadcrumbContextsCount; ++i) { // BreadcrumbContextsCount は SetMarker/BeginEvent/EndEvent の数
					const auto& c = cmdListNode->pBreadcrumbContexts[i];
					if (c.BreadcrumbIndex <= done && (!found || c.BreadcrumbIndex >= best)) {
						best = c.BreadcrumbIndex;
						marker = c.pContextString ? ConvertString(std::wstring(c.pContextString)) : "(no marker)";
						found = true;
					}
				}
				// コマンドリストの名前と、止まった命令の名前をLogに出す
				std::string clName = nameOf(cmdListNode->pCommandListDebugNameA, cmdListNode->pCommandListDebugNameW);
				std::string op = cmdListNode->pCommandHistory ? BreadcrumbOpName(cmdListNode->pCommandHistory[done]) : "?";
				LogManager::Error(std::format("[DRED] CmdList '{}' stopped at {}/{}  marker='{}'  operation={}", clName, done, cmdListNode->BreadcrumbCount, marker, op));
			}
		}

		// ===== 2.Page Fault（不正アクセス） =====
		D3D12_DRED_PAGE_FAULT_OUTPUT2 pageFault{};
		// ノードが ALLOCATION_NODE1 になる
		auto dumpAllocations = [&](const char* title, const D3D12_DRED_ALLOCATION_NODE1* head) {
			for (const D3D12_DRED_ALLOCATION_NODE1* node = head; node != nullptr; node = node->pNext) {
				std::string name = nameOf(node->ObjectNameA, node->ObjectNameW);
				LogManager::Error(std::format("[DRED] {} : {}", title, name));
			}
		};

		if (SUCCEEDED(dred->GetPageFaultAllocationOutput2(&pageFault)) && pageFault.PageFaultVA != 0) {
			LogManager::Error(std::format("[DRED] PageFault address = 0x{:016X} flags = 0x{:X}", (uint64_t)pageFault.PageFaultVA, (uint32_t)pageFault.PageFaultFlags));
			RenderContext::LogFaultResource(pageFault.PageFaultVA);
			dumpAllocations("alive（範囲外/状態不正）", pageFault.pHeadExistingAllocationNode);
			dumpAllocations("freed（解放後使用）", pageFault.pHeadRecentFreedAllocationNode);
		}

	} else {
		// DREDが有効でないことをLogに書く
		LogManager::Error("[DRED] ID3D12DeviceRemovedExtendedData2 is not available");
	}
#endif

	// Assertで止めとく
	HTT_ASSERT_MSG(false, "GPUがクラッシュしました。DREDのlogを確認してください");
}