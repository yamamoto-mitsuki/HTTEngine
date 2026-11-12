#ifdef USE_IMGUI
#include "HTTEngine/Editor/ImGuiManager.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <externals/imgui/imgui.h>
#include <externals/imgui/imgui_impl_dx12.h>
#include <externals/imgui/imgui_impl_win32.h>

#include "HTTEngine/Systems/Animation/AnimationSystem.h"
#include "HTTEngine/Systems/Combat/HitSystem.h"
#include "HTTEngine/Systems/Effects/ParticleSystem.h"
#include "HTTEngine/Systems/Physics/CollisionSystem.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Editor/Profiler.h"
#include "HTTEngine/Editor/Assets/AssetIcons.h"
#include "HTTEngine/Editor/Assets/AssetThumbnails.h"
#include "HTTEngine/Editor/Gizmos/LightGizmo.h"
#include "HTTEngine/Editor/Gizmos/CameraGizmo.h"
#include "HTTEngine/Editor/History/EditorHistory.h"
#include "HTTEngine/Editor/Keymap/EditorKeymap.h"
#include "HTTEngine/Editor/Viewport/EditorViewport.h"
// Timeline
#include "HTTEngine/Editor/Timeline/TimelineWindow.h"
// Windows
#include "HTTEngine/Editor/Windows/CodeWindow.h"
#include "HTTEngine/Editor/Windows/AttackTableWindow.h"
#include "HTTEngine/Editor/Windows/HierarchyWindow.h"
#include "HTTEngine/Editor/Windows/InspectorWindow.h"
#include "HTTEngine/Editor/Windows/ProjectWindow.h"
#include "HTTEngine/Editor/Windows/SceneDialogs.h"
#include "HTTEngine/Editor/Windows/SettingsWindow.h"
// Widgets
#include "HTTEngine/Editor/Widgets/ImeInput.h"
#include "HTTEngine/Editor/Widgets/FontPreview.h"
#include "HTTEngine/Graphics/GPU/DirectXCommon.h"
#include "HTTEngine/Window/Win32Window.h"

// 静的メンバ変数
std::unique_ptr<ImGuiManager> ImGuiManager::instance_ = nullptr;

namespace {
// エディタ全体で効くショートカット（どのウィンドウを触っていても効く。キーは EditorKeymap）
// 文字の入力中・ドラッグ中は何もしない（入力欄の中のCtrl+Zは、ImGuiが文字のUndoに使う）
void HandleGlobalShortcuts() {
	if (ImGui::GetIO().WantTextInput || ImGui::IsAnyItemActive()) {
		return;
	}
	if (EditorKeymap::IsTriggered(EditorAction::Undo)) {
		EditorHistory::RequestUndo();
	}
	if (EditorKeymap::IsTriggered(EditorAction::Redo)) {
		EditorHistory::RequestRedo();
	}
}

// ===== ImGuiが使うSRV（フォントの画像など）の割り当て =====
// ImGui 1.92は、必要になった文字をその場でフォントの画像に書き足す（最初に全部の文字を作らない）。
// そのため画像が作り直されることがあり、SRVをもらったり返したりできる必要がある
std::vector<uint32_t> imguiFreeSrvSlots = {0}; // 返してもらったスロット（0番は最初からImGui用に空けてある）

void AllocImGuiSrv(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu) {
	uint32_t slot = 0;
	if (!imguiFreeSrvSlots.empty()) {
		slot = imguiFreeSrvSlots.back(); // 返してもらったスロットを使い回す
		imguiFreeSrvSlots.pop_back();
	} else {
		slot = DirectXCommon::AllocateSRVSlot();
	}
	ID3D12DescriptorHeap* heap = DirectXCommon::GetSRVDescriptorHeap();
	*outCpu = DirectXCommon::GetCPUDescriptorHandle(heap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, slot);
	*outGpu = DirectXCommon::GetGPUDescriptorHandle(heap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, slot);
}

void FreeImGuiSrv(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE) {
	// CPUハンドルがヒープの先頭から何個目かを逆算して、使い回しの列に戻す
	SIZE_T start = DirectXCommon::GetSRVDescriptorHeap()->GetCPUDescriptorHandleForHeapStart().ptr;
	uint32_t slot = static_cast<uint32_t>((cpu.ptr - start) / DirectXCommon::GetDescriptorSizeSRV());
	imguiFreeSrvSlots.push_back(slot);
}

// File メニュー
void DrawFileMenu() {
	if (ImGui::BeginMenu("File")) {
		// シーン（New / Open / Save / Save As）
		SceneDialogs::DrawFileMenuItems();
		ImGui::Separator();
		// ImGuiウィンドウ配置をimgui.iniに書き出し
		if (ImGui::MenuItem("Save Layout")) {
			ImGui::SaveIniSettingsToDisk("imgui.ini");
		}
		ImGui::EndMenu();
	}
}

// Edit メニュー
void DrawEditMenu() {
	if (ImGui::BeginMenu("Edit")) {
		if (ImGui::MenuItem("Undo", EditorKeymap::GetShortcutText(EditorAction::Undo).c_str(), false, EditorHistory::CanUndo())) {
			EditorHistory::RequestUndo();
		}
		if (ImGui::MenuItem("Redo", EditorKeymap::GetShortcutText(EditorAction::Redo).c_str(), false, EditorHistory::CanRedo())) {
			EditorHistory::RequestRedo();
		}
		ImGui::Separator();
		// ゲーム全体・シーン・エディタの設定（Unity の Edit → Project Settings と同じ場所）
		if (ImGui::MenuItem("Settings")) {
			SettingsWindow::Focus();
		}
		ImGui::EndMenu();
	}
}

// View メニュー
void DrawViewMenu() {
	if (ImGui::BeginMenu("View")) {
		// ライトのギズモ（全部まとめてON / OFF。1つずつはInspectorのライトの区画で）
		LightGizmoFlags& lightGizmo = LightGizmo::GetGlobalFlags();
		ImGui::SeparatorText("Gizmos");
		ImGui::MenuItem("Light Icons", nullptr, &lightGizmo.showIcon);
		ImGui::MenuItem("Light Ranges", nullptr, &lightGizmo.showRange);
		ImGui::MenuItem("Camera Icons", nullptr, &CameraGizmo::GetShowIcons());
		ImGui::MenuItem("Camera Paths", nullptr, &CameraGizmo::GetShowPaths());
		ImGui::MenuItem("Camera Follow Zones", nullptr, &CameraGizmo::GetShowFollowZones()); // Dead Zone・Soft Zone（Game ビューにも出る。1つずつは Inspector の Show Gizmo）
		ImGui::MenuItem("Camera Orbits", nullptr, &CameraGizmo::GetShowOrbits());           // 回る輪・回る中心（Scene ビューだけ。1つずつは Inspector の Show Gizmo）
		ImGui::MenuItem("Particle Shapes", nullptr, &ParticleSystem::GetShowGizmos());      // 粒の出る形（Scene ビューだけ。1つずつは Inspector の Show Gizmo）
		ImGui::MenuItem("Colliders", nullptr, &CollisionSystem::GetShowGizmos());
		ImGui::MenuItem("Hit Shapes", nullptr, &HitSystem::GetShowGizmos()); // 攻撃の形・ジャスト判定の範囲・Hurtbox
		ImGui::MenuItem("Skeletons", nullptr, &AnimationSystem::GetShowGizmos());
		// Scene ビューの絵（ブルームなど。レンズは Scene では掛けない）
		EditorViewport::ViewFlags& viewFlags = EditorViewport::GetViewFlags();
		ImGui::SeparatorText("Scene View");
		ImGui::MenuItem("Post Effects", nullptr, &viewFlags.sceneEffects);
		ImGui::MenuItem("Fog", nullptr, &viewFlags.sceneFog);
		ImGui::MenuItem("Skybox", nullptr, &viewFlags.sceneSkybox);
		ImGui::MenuItem("Depth of Field", nullptr, &viewFlags.sceneDepthOfField, viewFlags.sceneEffects); // Post Effects を切っていると押せない
		ImGui::MenuItem("Grid###sceneGrid", nullptr, &viewFlags.sceneGrid);
		ImGui::MenuItem("Pick UI", nullptr, &viewFlags.scenePickUI);
		ImGui::SetItemTooltip("Image・Text をクリックで選ぶ・ドラッグで動かす（外すと、UI の後ろの 3D の物を選べる）");
		// Game ビューの絵
		ImGui::SeparatorText("Game View");
		ImGui::MenuItem("Grid###gameGrid", nullptr, &viewFlags.gameGrid);
		ImGui::EndMenu();
	}
}
} // namespace

//=============================================================================
// 初期化
//=============================================================================
void ImGuiManager::Initialize(Win32Window* window) {
	HTT_ASSERT_MSG(instance_ == nullptr, "Initialize()を2回呼んでいます");
	instance_ = std::make_unique<ImGuiManager>(ConstructorKey());
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGuiIO& io = ImGui::GetIO();
	// Docking有効化
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	// アプリ外にマウスカーソルを飛ばさない
	io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
	// ウィンドウ配置をimgui.iniに自動保存
	io.IniFilename = "imgui.ini";

	// 日本語フォントの読み込み
	// 文字の範囲は指定しない。ImGui 1.92は、使われた文字をその場でフォントの画像に書き足す（珍しい漢字や ★♪① も出る）
	io.Fonts->Clear();
	io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\meiryo.ttc", 18.0f);
	// Code ウィンドウ用の等幅フォント（ふだんのフォントは、最初に足したメイリオのまま）
	CodeWindow::Initialize();
	ImGui_ImplWin32_Init(window->GetHWND());
	// DirectX12側の初期化。SRVは必要な分だけコールバックで渡す（フォントの画像が作り直されても大丈夫なように）
	ImGui_ImplDX12_InitInfo initInfo;
	initInfo.Device = DirectXCommon::GetDevice();
	initInfo.CommandQueue = DirectXCommon::GetCommandQueue(); // 書き足したフォントの画像をGPUへ送るのに使う
	initInfo.NumFramesInFlight = DirectXCommon::kSwapChainBufferCount;
	initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
	initInfo.SrvDescriptorHeap = DirectXCommon::GetSRVDescriptorHeap();
	initInfo.SrvDescriptorAllocFn = AllocImGuiSrv;
	initInfo.SrvDescriptorFreeFn = FreeImGuiSrv;
	ImGui_ImplDX12_Init(&initInfo);
	// 日本語入力：候補の一覧を、入力欄の行を隠さない位置に出す
	ImeInput::Initialize();

	// 前回保存したスタイルを自動で読み込む（ファイルがなければスキップ）
	instance_->LoadStyle("imgui_style.ini");

	// InspectorにエンジンのComponent（Transform・ModelRenderer）を登録する
	InspectorWindow::Initialize();
	// エディタの操作の割り当て（保存した人ごとの割り当てを読み、Settings に Keymap のページを足す）
	EditorKeymap::Initialize();
	// Project ウィンドウのアイコンの画像（HTTEngine/Resources/Editor/Icons/）を読む
	AssetIcons::Initialize();

	LogManager::Log("Initialized");
}

//=============================================================================
// 解放
//=============================================================================
void ImGuiManager::Release() {
	HTT_ASSERT_MSG(instance_ != nullptr, "Initialize()より先にRelease()が呼ばれています");
	EditorHistory::Clear();
	AssetThumbnails::Release(); // Project ウィンドウの小さい絵（GPU の物なので、DirectX より先に捨てる）
	FontPreview::Clear();
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	instance_.reset();
	LogManager::Log("Released");
}

//=============================================================================
// フレームの開始処理
//=============================================================================
// ===== 1フレームの始まり =====
void ImGuiManager::Begin() {
	HTT_ASSERT_MSG(instance_, "Initialize()を先に呼んでください");
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	ImeInput::NewFrame(); // 日本語の変換中の文字を読む

	instance_->DrawDockSpace();

	// --- Logの出力 ---
	LogManager::DrawImGuiWindow();

	// --- 設定（調整項目。前の Parameters ウィンドウ） ---
	SettingsWindow::Draw();

	// --- プロファイラの描画 ---
	Profiler::Draw();

	// --- ヒエラルキー ---
	HierarchyWindow::Draw();

	// --- インスペクター（ヒエラルキーで選んだEntityを編集する） ---
	InspectorWindow::Draw();

	// --- プロジェクト（resources/ と Source/ のファイル） ---
	ProjectWindow::Draw();

	// --- コード（読むだけ。Inspector・Project ウィンドウから開く） ---
	CodeWindow::Draw();

	// --- 攻撃表の AI の様子（再生中の状態・抽選確率） ---
	AttackTableWindow::Draw();

	// --- シーンのファイルの確認（保存しますか？・名前を付けて保存など） ---
	SceneDialogs::Draw();

	// --- Timeline を直す ---
	TimelineWindow::Draw();

	// --- Undo・Redoのショートカット ---
	HandleGlobalShortcuts();
}

// ===== 画面全体のウィンドウ（メニューバーとドッキングの土台）=====
void ImGuiManager::DrawDockSpace() {
	// DockSpace自体がDockされるのを防ぐ
	ImGuiWindowFlags dockFlags = ImGuiWindowFlags_MenuBar |    // メニューバーを表示
	                             ImGuiWindowFlags_NoDocking |  // DockSpace自体はDockされない
	                             ImGuiWindowFlags_NoTitleBar | // タイトルバーなし
	                             ImGuiWindowFlags_NoCollapse | // 折りたたみなし
	                             ImGuiWindowFlags_NoResize |   // リサイズなし（画面全体に合わせる）
	                             ImGuiWindowFlags_NoMove |     // 移動なし
	                             ImGuiWindowFlags_NoBringToFrontOnFocus | // ドッキングしたウィンドウを触っても前に出てこない（浮いているウィンドウが後ろに隠れないように）
	                             ImGuiWindowFlags_NoNavFocus;  // ナビゲーションフォーカスなし
	// 画面全体に広げる
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->Pos);
	ImGui::SetNextWindowSize(viewport->Size);
	ImGui::SetNextWindowViewport(viewport->ID);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

	ImGui::Begin("DockSpaceWindow", nullptr, dockFlags);
	ImGui::PopStyleVar(3);

	DrawMenuBar();

	// DockSpaceを登録する
	ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");
	ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

	ImGui::End();
}

// ===== メニューバー =====
void ImGuiManager::DrawMenuBar() {
	if (ImGui::BeginMenuBar()) {
		DrawFileMenu();
		DrawEditMenu();
		DrawViewMenu();
		DrawSettingMenu();
		ImGui::EndMenuBar();
	}
}

// ===== Setting メニュー（SaveStyle・LoadStyle を呼ぶのでメンバ関数）=====
void ImGuiManager::DrawSettingMenu() {
	if (ImGui::BeginMenu("Setting")) {
		// 色・サイズ・角丸等を自由に調整できる。
		if (ImGui::BeginMenu("Style")) {
			ImGui::ShowStyleEditor();
			ImGui::EndMenu();
		}
		// フォントの切り替えとテーマプリセットの選択ができる。
		if (ImGui::BeginMenu("Font")) {
			ImGui::ShowFontSelector("Font");
			ImGui::ShowStyleSelector("Theme Preset");
			ImGui::EndMenu();
		}

		ImGui::Separator(); // 横線

		// 現在のスタイル（色・Alpha・サイズ系）を imgui_style.ini に保存
		if (ImGui::MenuItem("Save Style")) {
			SaveStyle("imgui_style.ini");
		}
		// imgui_style.ini からスタイルを読み込んで即時反映
		if (ImGui::MenuItem("Load Style")) {
			LoadStyle("imgui_style.ini");
		}
		ImGui::EndMenu();
	}
}

//=============================================================================
// 描画リクエストの追加
//=============================================================================
void ImGuiManager::AddDrawRequest(std::function<void()> drawFunc) {
	HTT_ASSERT_MSG(instance_, "Initialize()を先に呼んでください");
	instance_->requests_.push_back(std::move(drawFunc));
}

//=============================================================================
// 積まれた描画リクエストを全て実行する
//=============================================================================
void ImGuiManager::ProcessRequests() {
	HTT_ASSERT_MSG(instance_, "Initialize()を先に呼んでください");
	for (const auto& func : instance_->requests_) {
		func();
	}
}

//=============================================================================
// 描画データを確定させる
//=============================================================================
void ImGuiManager::Render() {
	HTT_ASSERT_MSG(instance_, "Initialize()を先に呼んでください");
	ImeInput::DrawComposition(); // 全部のウィンドウを描き終わってから、変換中の文字を入力欄の上に描く
	ImGui::Render();
}

//=============================================================================
// 確定した描画データをコマンドリストに積む
//=============================================================================
void ImGuiManager::RenderDrawData() {
	HTT_ASSERT_MSG(instance_, "Initialize()を先に呼んでください");
	DirectXCommon::BindSRVHeap(DirectXCommon::GetCommandList());
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), DirectXCommon::GetCommandList());
}

//=============================================================================
// 全リクエストをクリアする
//=============================================================================
void ImGuiManager::ClearRequests() {
	HTT_ASSERT_MSG(instance_, "Initialize()を先に呼んでください");
	instance_->requests_.clear();
}

//=============================================================================
// マウスを見るか
//=============================================================================
void ImGuiManager::SetMouseEnabled(bool enabled) {
	HTT_ASSERT_MSG(instance_, "Initialize()を先に呼んでください");
	// NoMouse の間、ImGui はどの窓もホバーしない（マウスの位置は覚え続けるので、戻したフレームから今の位置で動く）
	ImGuiIO& io = ImGui::GetIO();
	if (enabled) {
		io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
	} else {
		io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
	}
}

//=============================================================================
// スタイルの保存項目
//=============================================================================
namespace {
constexpr std::string_view kColorPrefix = "Color_"; // 色の行の先頭（Color_番号=r,g,b,a）

// ini の1項目（キーと、ImGuiStyle の中の float）
struct StyleField {
	std::string_view key;
	float* value;
};

/// <summary>
/// ini に保存する大きさの項目の一覧。保存も読み込みもこの表を回す（項目を足すのはここだけでよい）
/// </summary>
auto GetStyleFields(ImGuiStyle& s) {
	return std::to_array<StyleField>({
	    {"Alpha", &s.Alpha},
	    {"WindowPaddingX", &s.WindowPadding.x},
	    {"WindowPaddingY", &s.WindowPadding.y},
	    {"WindowRounding", &s.WindowRounding},
	    {"WindowBorderSize", &s.WindowBorderSize},
	    {"FramePaddingX", &s.FramePadding.x},
	    {"FramePaddingY", &s.FramePadding.y},
	    {"FrameRounding", &s.FrameRounding},
	    {"FrameBorderSize", &s.FrameBorderSize},
	    {"ItemSpacingX", &s.ItemSpacing.x},
	    {"ItemSpacingY", &s.ItemSpacing.y},
	    {"ItemInnerSpacingX", &s.ItemInnerSpacing.x},
	    {"ItemInnerSpacingY", &s.ItemInnerSpacing.y},
	    {"IndentSpacing", &s.IndentSpacing},
	    {"ScrollbarSize", &s.ScrollbarSize},
	    {"ScrollbarRounding", &s.ScrollbarRounding},
	    {"GrabMinSize", &s.GrabMinSize},
	    {"GrabRounding", &s.GrabRounding},
	    {"TabRounding", &s.TabRounding},
	    {"TabBarOverlineSize", &s.TabBarOverlineSize},
	    {"TabBarBorderSize", &s.TabBarBorderSize},
	    {"PopupRounding", &s.PopupRounding},
	    {"ChildRounding", &s.ChildRounding},
	});
}

/// <summary>
/// "1.000,0.500" のようなカンマ区切りの数を読む
/// </summary>
/// <returns>out の数だけ読めたら true。読めなかった数は書き換えない</returns>
bool ParseFloats(std::string_view text, std::span<float> out) {
	for (size_t i = 0; i < out.size(); ++i) {
		// --- 数を1つ読む ---
		const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out[i]);
		if (error != std::errc{}) {
			return false;
		}
		text.remove_prefix(end - text.data());
		// --- 最後の数でなければ、次はカンマ ---
		if (i + 1 < out.size()) {
			if (!text.starts_with(',')) {
				return false;
			}
			text.remove_prefix(1);
		}
	}
	return true;
}
} // namespace


//=============================================================================
// スタイルをiniファイルに保存
//=============================================================================
void ImGuiManager::SaveStyle(const std::string& path) {
	ImGuiStyle& s = ImGui::GetStyle();
	std::ofstream file(path);
	if (!file) {
		LogManager::Warning("ファイルを開けませんでした: " + path);
		return;
	}
	// 大きさ
	for (const StyleField& field : GetStyleFields(s)) {
		file << std::format("{}={:.3f}\n", field.key, *field.value);
	}
	// 色（全ImGuiCol_COUNT個）
	for (int i = 0; i < ImGuiCol_COUNT; i++) {
		const ImVec4& c = s.Colors[i];
		file << std::format("{}{}={:.3f},{:.3f},{:.3f},{:.3f}\n", kColorPrefix, i, c.x, c.y, c.z, c.w);
	}

	LogManager::Log("SaveStyle: " + path);
}

//=============================================================================
// スタイルをiniファイルから読み込む
//=============================================================================
void ImGuiManager::LoadStyle(const std::string& path) {
	ImGuiStyle& s = ImGui::GetStyle();
	std::ifstream file(path);
	if (!file) {
		// 初回起動時はファイルがなくてもOK
		LogManager::Warning("File not found (first launch): " + path);
		return;
	}

	const auto fields = GetStyleFields(s);
	std::string line;
	while (std::getline(file, line)) {
		// --- 「キー=値」に分ける ---
		const size_t equal = line.find('=');
		if (equal == std::string::npos) {
			continue;
		}
		const std::string_view key = std::string_view(line).substr(0, equal);
		const std::string_view value = std::string_view(line).substr(equal + 1);

		// --- 色 ---
		if (key.starts_with(kColorPrefix)) {
			const std::string_view number = key.substr(kColorPrefix.size());
			int index = -1; // 読めなければ -1 のままで、下の範囲の確認で外れる
			std::from_chars(number.data(), number.data() + number.size(), index);
			std::array<float, 4> color{};
			if (index >= 0 && index < ImGuiCol_COUNT && ParseFloats(value, color)) {
				s.Colors[index] = ImVec4(color[0], color[1], color[2], color[3]);
			}
			continue;
		}

		// --- 大きさ ---
		const auto field = std::ranges::find(fields, key, &StyleField::key);
		if (field != fields.end()) {
			ParseFloats(value, std::span(field->value, 1));
		}
	}

	LogManager::Log("LoadStyle: " + path);
}
#endif