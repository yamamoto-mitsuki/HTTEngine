#ifdef USE_IMGUI
#include "ViewportWindow.h"
#include "HTTEngine/Graphics/RenderTarget/RenderTexture.h"
#include <algorithm>

void ViewportWindow::Draw(RenderTexture* rt, const std::function<void()>& overlay) {
	//name_のImGUiWindowを作成
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.02f, 0.02f, 1.0f));
	ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
	// 絵の上でマウスを押しても、ウィンドウごと動かないようにする（ギズモのドラッグ・クリックで選ぶのに使うので）
	// 前のフレームでマウスが絵の上にあったかで決める（タブ・枠をつかめば、今までどおり動かせる）
	if (hovered_) {
		flags |= ImGuiWindowFlags_NoMove;
	}
	visible_ = ImGui::Begin(name_.c_str(), nullptr, flags); // ほかのタブの裏・たたんでいると false（中身は描かれない）
	ImGui::PopStyleColor();
	// アスペクト維持
	ImVec2 available = ImGui::GetContentRegionAvail();
	available.x = std::max(available.x, 1.0f);
	available.y = std::max(available.y, 1.0f);
	const float aspect = static_cast<float>(rt->GetWidth()) / static_cast<float>(rt->GetHeight());
	ImVec2 imageSize;
	if (available.x / available.y > aspect) {
		imageSize.y = available.y;
		imageSize.x = available.y * aspect;
	} else {
		imageSize.x = available.x;
		imageSize.y = available.x / aspect;
	}
	// 中央寄せ
	ImVec2 contentMin = ImGui::GetCursorScreenPos();
	ImVec2 contentMax = {contentMin.x + available.x, contentMin.y + available.y};
	ImVec2 imageMin = {contentMin.x + (available.x - imageSize.x) * 0.5f, contentMin.y + (available.y - imageSize.y) * 0.5f};
	ImVec2 imageMax = {imageMin.x + imageSize.x, imageMin.y + imageSize.y};
	// 余白塗り
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImU32 m = IM_COL32(5, 5, 5, 255);
	dl->AddRectFilled({contentMin.x - 15, contentMin.y - 15}, {contentMax.x + 15, imageMin.y}, m); // 上
	dl->AddRectFilled({contentMin.x - 15, imageMax.y}, {contentMax.x + 15, contentMax.y + 15}, m); // 下
	dl->AddRectFilled({contentMin.x - 15, imageMin.y}, {imageMin.x, imageMax.y}, m);               // 左
	dl->AddRectFilled({imageMax.x, imageMin.y}, {contentMax.x + 15, imageMax.y}, m);               // 右
	// ImGui::Imageで貼り付ける
	ImGui::SetCursorScreenPos(imageMin);
	ImGui::Image(static_cast<ImTextureID>(rt->GetSRVGPUHandle().ptr), imageSize);
	hovered_ = ImGui::IsItemHovered();
	// 矩形キャッシュ（overlay から GetImageMin() 等で参照できるよう先に確定）
	imageMin_ = imageMin;
	imageMax_ = imageMax;
	imageSize_ = imageSize;

	// ギズモ等のオーバーレイを描画をImGuiWindowが生存している間に表示
	if (overlay) {
		overlay();
	}

	ImGui::End();
}
#endif