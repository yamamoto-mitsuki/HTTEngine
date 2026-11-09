#pragma once
#ifdef USE_IMGUI
#include <string>
#include <utility>
#include <functional>
#include <externals/imgui/imgui.h>

// 前方宣言
class RenderTexture;


/// <summary> 
/// RenderTexture を1枚、ImGuiのImageパネルとして表示する（各ビュー1個） 
/// </summary>
class ViewportWindow {
public:
	explicit ViewportWindow(std::string name) : name_(std::move(name)) {}

	void Draw(RenderTexture* rt, const std::function<void()>& overlay = {});

	// ゲッター
	float GetWidth() const { return imageSize_.x; }
	float GetHeight() const { return imageSize_.y; }
	const ImVec2& GetImageMin() const { return imageMin_; }
	const ImVec2& GetImageMax() const { return imageMax_; }
	const ImVec2& GetImageSize() const { return imageSize_; }
	bool IsHovered() const { return hovered_; }
	// このフレームに絵を描いたか（ほかのタブの裏・たたんでいるときは false。そのときの GetImageMin などは当てにならない）
	bool IsVisible() const { return visible_; }

private:
	std::string name_;
	ImVec2 imageMin_{}, imageMax_{}, imageSize_{};
	bool hovered_ = false;
	bool visible_ = false; // ImGui::Begin が true を返したか（中身を描く所か）
};
#endif