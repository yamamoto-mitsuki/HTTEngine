参考：
- `ID3D12GraphicsCommandList::RSSetViewports`：https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-rssetviewports
- `ID3D12GraphicsCommandList::ClearRenderTargetView`：https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-clearrendertargetview
- `std::round`・`std::lround`：https://en.cppreference.com/w/cpp/numeric/math/round

# ゲームの画面の比率と黒帯（GameScreen）

ゲームごとに画面の比率を決められるようにする。比率と違う大きさのウィンドウでは、絵を伸ばさず、上下か左右に黒帯を出す。

[VectorShape.md](VectorShape.md) の後に写す（検証はその順で行った）。パートは **A → B → C の順**。A の後でもビルドが通る。

ブランチは `feature/VectorShape` のままでよい。

## 0章 全体の形

### 0.1 使い方

ゲームの `main.cpp` で決める。横長の 16:9 のままなら、何も書かなくてよい。

```cpp
WindowConfig config;
config.width = 540;       // ウィンドウの大きさ
config.height = 960;
config.gameWidth = 720;   // ゲームの画面（比率と UI のピクセルの基準）
config.gameHeight = 1280;
```

| 値 | 意味 |
|---|---|
| `width`・`height` | ウィンドウの大きさ。ユーザーが変えられる |
| `gameWidth`・`gameHeight` | ゲームの画面。縦横比と UI のピクセルの基準。起動の後は変わらない |

`gameAspectRatio` はどこからも読まれていなかったので消して、この2つに置き換える。比率だけだと UI のピクセルの基準が決まらないため、幅と高さで持つ。

### 0.2 前の形と今の形

16:9 が5か所に別々に書いてあったので、`GameScreen` の1か所にまとめる。

| 場所 | 前 | 今 |
|---|---|---|
| UI の基準（`Renderer`） | `kSpriteBaseWidth = 1280`・`kSpriteBaseHeight = 720` | `GameScreen::GetWidth()`・`GetHeight()` |
| カメラ（`CameraSystem`） | `kAspectRatio = 1280.0f / 720.0f` | `GameScreen::GetAspectRatio()` |
| エディタの描画先（`EditorViewport`） | `kViewWidth = 1280`・`kViewHeight = 720` | ゲームの画面と同じ大きさ |
| デバッグカメラ | 引数の既定値 `1280.0f / 720.0f` | `GameScreen::GetAspectRatio()` |
| ポストエフェクトの描画先（`Engine`） | `1920, 1080` | 長い方の辺を 1920 にして、短い方は比率で決める |

### 0.3 黒帯の作り方

```
ウィンドウ（1000×900）
┌──────────────────────┐
│ 黒帯（ここだけ黒く） │
├──────────────────────┤ ← y = 168
│                      │
│ ゲームの絵（1000×563）│  ← ここだけに描く（ビューポートとシザー）
│                      │
├──────────────────────┤
│ 黒帯                 │
└──────────────────────┘
```

1. `RenderWindow::PreDraw` がウィンドウ全部を `kClearColor` でクリアする（今までと同じ）。
2. `GameScreen::CalcFitRect` が、比率を保って入る一番大きな四角を出す。
3. `RenderWindow::ClearBars` が、その四角の外だけを `kBarColor`（黒）でクリアする。
4. 最後の合成（`LensPass`）と UI を、その四角のビューポートとシザーで描く。

3D とポストエフェクトは元から別のテクスチャに描くので、変わらない。変えるのは最後にウィンドウへ写す所だけ。

エディタは `ViewportWindow` が元から比率を保って余白を塗っているので、描画先の大きさを変えるだけでよい。

### 0.4 決めたこと

| 項目 | 決めたこと | 理由 |
|---|---|---|
| 設定の場所 | `main.cpp` の `WindowConfig` | ゲームごとに1回決める値で、タイトルやウィンドウの大きさと同じ場所に書ける |
| 実行中の変更 | しない | 描画先のテクスチャの大きさが比率で決まるので、変えると全部作り直しになる |
| 黒帯の色 | `kClearColor` は変えず、帯の色 `kBarColor` を別に持つ | `kClearColor` は RenderTexture や IBL の焼き込みのクリアにも使われている。帯のためにこれを変えると、関係ない所まで変わる |
| 帯の塗り方 | `ClearRenderTargetView` に四角を渡す | 板を描かなくてよい。PSO も頂点も要らず、ビューポートやシザーにも影響されない |
| 四角の位置 | ピクセルの境目にそろえる | 端がぼやけない |
| マウス | 製品版は黒帯の内側をゲームの画面として数える | 黒帯の分だけずれないように。黒帯の上は `IsMouseInGame` が false |
| ウィンドウが2つ以上のとき | 最初の `Engine::Initialize` の値を全部のウィンドウで使う | ゲームの画面はゲーム全体で1つ |
| 被写界深度の Max Blur | 変えない（高さ 720 のときのピクセルのまま、絵の高さに比例する） | 比率の話とは別で、値の決め方を変えると今の絵が変わる |

### 0.5 解説

`CalcFitRect`：幅をそろえる倍率と高さをそろえる倍率を出して、小さい方を使う。大きい方を使うと、もう一方の辺がはみ出す。

`ClearBars`：`ClearRenderTargetView` の3つ目と4つ目の引数に四角の数と配列を渡すと、その四角の中だけがクリアされる。数を 0 にすると「全部」という意味になるので、帯が無いとき（ウィンドウの比率がゲームと同じとき）は呼ばずに帰る。呼ぶと絵の場所まで黒くなる（今は合成の前なので絵は描き直されるが、順番に頼らない）。

```
ウィンドウ 1000×900、ゲームの画面 1280×720
幅の倍率   1000 / 1280 = 0.781
高さの倍率  900 /  720 = 1.25
小さい方 0.781 → 1000×563
```

`CalcRenderSize`：長い方の辺を決めて、比率を掛ける。1280×720 なら今までと同じ 1920×1080、720×1280 なら 1080×1920。

`std::round`（`float` のまま一番近い整数へ）と `std::lround`（`long` の整数にして返す）はどちらも、ちょうど半分なら 0 から遠い方へ丸める。

---

## パートA 画面の大きさを1か所にまとめる

ここまでで動きは今までと同じ（16:9）。

### A-1 `Graphics/Renderer/GameScreen.h`（新しいファイル）

```cpp
#pragma once
#include <cstdint>

// ゲームの画面の大きさ。縦横比と、UI の位置・大きさのピクセルの基準を決める
// main.cpp の WindowConfig（gameWidth・gameHeight）で決めて、起動の後は変えない（描画先のテクスチャの大きさがこの比率で決まるので）
//   横長（既定）：config.gameWidth = 1280; config.gameHeight = 720;
//   縦長　　　　：config.gameWidth = 720;  config.gameHeight = 1280;
// ウィンドウの大きさ（width・height）とは別の値。比が違うウィンドウでは、製品版は上下か左右に黒帯が出る

/// <summary>
/// 画面の中の四角（左上の位置と大きさ。ピクセル。Y は下が＋）
/// </summary>
struct ViewRect {
	float x = 0.0f;      // 左の端
	float y = 0.0f;      // 上の端
	float width = 0.0f;  // 幅
	float height = 0.0f; // 高さ
};

/// <summary>
/// 描画先のテクスチャの大きさ（ピクセル）
/// </summary>
struct RenderSize {
	uint32_t width = 0;  // 幅
	uint32_t height = 0; // 高さ
};

/// <summary>
/// ゲームの画面の大きさ（ゲーム全体で1つ）
/// </summary>
class GameScreen {
public:
	// 画面の大きさを決める（Engine::Initialize の頭で1回。width・height は 1 以上）
	static void Initialize(int width, int height);

	static float GetWidth() { return width_; }                 // 幅（UI のピクセル）
	static float GetHeight() { return height_; }               // 高さ（UI のピクセル）
	static float GetAspectRatio() { return width_ / height_; } // 縦横比（幅 ÷ 高さ）

	/// <summary>
	/// 長い方の辺を longSide ピクセルにした、画面と同じ比率の大きさ（ポストエフェクトの描画先に使う）
	/// </summary>
	static RenderSize CalcRenderSize(uint32_t longSide);

	/// <summary>
	/// 幅 outerWidth・高さ outerHeight の四角の中に、画面の比率を保って一番大きく入れた四角（真ん中に寄せる。残りが黒帯）
	/// <para>比が同じなら、外の四角と同じ大きさになる。ピクセルの境目にそろえる</para>
	/// </summary>
	static ViewRect CalcFitRect(float outerWidth, float outerHeight);

private:
	static inline float width_ = 1280.0f;
	static inline float height_ = 720.0f;
};
```

### A-2 `Graphics/Renderer/GameScreen.cpp`（新しいファイル）

```cpp
#include "GameScreen.h"

#include <algorithm>
#include <cmath>

#include "HTTEngine/Diagnostics/HTTAssert.h"


//=============================================================================
// 初期化
//=============================================================================
void GameScreen::Initialize(int width, int height) {
	HTT_ASSERT_MSG(width > 0 && height > 0, "WindowConfig の gameWidth・gameHeight は 1 以上にしてください");
	width_ = static_cast<float>(width);
	height_ = static_cast<float>(height);
}


//=============================================================================
// 大きさの計算
//=============================================================================
// ===== 描画先のテクスチャの大きさ =====
RenderSize GameScreen::CalcRenderSize(uint32_t longSide) {
	const float scale = static_cast<float>(longSide) / (std::max)(width_, height_);
	return {static_cast<uint32_t>(std::lround(width_ * scale)), static_cast<uint32_t>(std::lround(height_ * scale))};
}

// ===== 黒帯の内側 =====
ViewRect GameScreen::CalcFitRect(float outerWidth, float outerHeight) {
	// 幅でそろえる倍率と、高さでそろえる倍率の小さい方なら、どちらの辺もはみ出さない
	const float scale = (std::min)(outerWidth / width_, outerHeight / height_);
	const float width = std::round(width_ * scale);
	const float height = std::round(height_ * scale);
	return {std::floor((outerWidth - width) * 0.5f), std::floor((outerHeight - height) * 0.5f), width, height};
}
```

### A-3 `Window/Win32Window.h`

変更前：

```cpp
	// ゲームビュー設定
	float gameAspectRatio = 16.0f / 9.0f; // ゲーム画面のアスペクト比
	bool isImGui = false;                 // ImGuiを使うか
```

変更後：

```cpp
	// ゲームの画面（縦横比と UI のピクセルの基準。ウィンドウの大きさとは別。比が違うウィンドウでは、製品版は黒帯が出る。GameScreen.h）
	int gameWidth = 1280;  // 画面の幅（縦長のゲームなら 720 など）
	int gameHeight = 720;  // 画面の高さ（縦長のゲームなら 1280 など）
	bool isImGui = false;  // ImGuiを使うか
```

### A-4 `Engine.cpp`

include を足す。

変更後：

```cpp
#include "HTTEngine/Graphics/RenderTarget/RenderTextureManager.h"
#include "HTTEngine/Graphics/Renderer/GameScreen.h"
```

初期化の見出しの前に足す。

変更後：

```cpp
namespace {
constexpr uint32_t kPostLongSide = 1920; // ポストエフェクトの描画先の、長い方の辺（ピクセル。短い方はゲームの画面の比率で決まる）
} // namespace
```

`LogManager::Initialize();` の下に足す。

変更後：

```cpp
	// ログ初期化
	LogManager::Initialize();
	// ゲームの画面の大きさ（カメラ・描画先・UI の基準が読むので、それらより先に）
	GameScreen::Initialize(config.gameWidth, config.gameHeight);
```

変更前：

```cpp
	SceneRenderer::InitializePost(1920, 1080); // ブルーム初期化
```

変更後：

```cpp
	const RenderSize postSize = GameScreen::CalcRenderSize(kPostLongSide);
	SceneRenderer::InitializePost(postSize.width, postSize.height); // ブルーム初期化
```

### A-5 `Graphics/Renderer/Renderer.h`

include を足す。

変更後：

```cpp
#include "HTTEngine/Graphics/Pipeline/ShaderConstants.h"
#include "HTTEngine/Graphics/Renderer/GameScreen.h"
```

次の所は全部消す。

変更前：

```cpp
	// スプライトの基準解像度
	// 大きさは「この解像度でのテクスチャ原寸 × scale」で決まるので、
	// 描画先(ウィンドウ / RenderTexture)の解像度が変わっても画面上の比率は変わらない。
	static constexpr float kSpriteBaseWidth = 1280.0f;
	static constexpr float kSpriteBaseHeight = 720.0f;
```

2つの関数を直す。

変更後：

```cpp
	static Vector2 NdcToBasePixel(const Vector2& ndc) { return {ndc.x * GameScreen::GetWidth() * 0.5f, ndc.y * GameScreen::GetHeight() * 0.5f}; }
```

変更後：

```cpp
	static Vector2 BasePixelToNdc(const Vector2& px) { return {px.x / (GameScreen::GetWidth() * 0.5f), px.y / (GameScreen::GetHeight() * 0.5f)}; }
```

### A-6 `Systems/Rendering2D/SpriteRenderSystem.cpp`

2か所。

変更後：

```cpp
	return {column * GameScreen::GetWidth() * 0.5f, row * GameScreen::GetHeight() * 0.5f};
```

変更後：

```cpp
	return {pixelX / (GameScreen::GetWidth() * 0.5f), pixelY / (GameScreen::GetHeight() * 0.5f)};
```

### A-7 `Systems/Camera/CameraSystem.cpp`

include を足して、定数 `kAspectRatio` の行を消す。

変更後：

```cpp
#include "HTTEngine/Editor/Inspector/ComponentEditor.h"
#include "HTTEngine/Graphics/Renderer/GameScreen.h"
```

変更後：

```cpp
	state->camera->Initialize(fovY, GameScreen::GetAspectRatio(), nearZ, farZ); // Initialize の中で行列も作り直す
```

変更後：

```cpp
		const float halfWidth = halfHeight * GameScreen::GetAspectRatio();
```

### A-8 `Editor/Viewport/EditorViewport.h`

変更後：

```cpp
	void Initialize();
```

### A-9 `Editor/Viewport/EditorViewport.cpp`

include を足して、定数 `kViewWidth`・`kViewHeight` の行を消す。

変更後：

```cpp
#include "HTTEngine/Graphics/RenderTarget/RenderWindow.h"
#include "HTTEngine/Graphics/Renderer/GameScreen.h"
#include "HTTEngine/Graphics/Renderer/SceneRenderer.h"
```

変更後：

```cpp
void EditorViewport::Initialize() {
	// Game / Scene それぞれの描画先（SDR・深度あり）。大きさはゲームの画面と同じ（UI の 1 ピクセルが絵の 1 ピクセル）
	const uint32_t viewWidth = static_cast<uint32_t>(GameScreen::GetWidth());
	const uint32_t viewHeight = static_cast<uint32_t>(GameScreen::GetHeight());
	gameRT_ = RenderTextureManager::Create(viewWidth, viewHeight, RenderTextureFormat::SDR);
	sceneRT_ = RenderTextureManager::Create(viewWidth, viewHeight, RenderTextureFormat::SDR);
	// デバッグカメラ（自由視点）
	debugCamera_ = std::make_shared<DebugCamera>();
	debugCamera_->Initialize(0.45f, GameScreen::GetAspectRatio(), 0.1f, 500.0f);
```

### A-10 `Editor/Gizmos/UIGizmo.cpp`

`Renderer` を使わなくなるので、include を置き換える。

変更前：

```cpp
#include "HTTEngine/Graphics/Renderer/Renderer.h"
```

変更後：

```cpp
#include "HTTEngine/Graphics/Renderer/GameScreen.h"
```

変更後：

```cpp
	    (mouse.x - drag.startMouse.x) / imageSize.x * GameScreen::GetWidth(),
	    -(mouse.y - drag.startMouse.y) / imageSize.y * GameScreen::GetHeight(),
```

ここでビルドが通る。絵は今までと同じ。

---

## パートB 黒帯

### B-1 `Graphics/RenderTarget/RenderWindow.h`・`.cpp`

`kClearColor` は変えない。その下に帯の色を足す。

変更後：

```cpp
	static constexpr std::array<float, 4> kClearColor = {0.1f, 0.25f, 0.5f, 1.0f};
	// ゲームの比率と違うウィンドウに出る帯の色（RGBAの順）
	static constexpr std::array<float, 4> kBarColor = {0.0f, 0.0f, 0.0f, 1.0f};
```

前方宣言を足す。

変更後：

```cpp
// 前方宣言
class Win32Window;
struct ViewRect;
```

`BindRenderTarget` の下に宣言を足す。

変更後：

```cpp
	void BindRenderTarget();

	/// <summary>
	/// ウィンドウのうち、inner の四角の外だけを帯の色でクリアする
	/// </summary>
	/// <param name="inner">ゲームの絵を描く四角（ピクセル）</param>
	/// <param name="windowWidth">ウィンドウの幅（ピクセル）</param>
	/// <param name="windowHeight">ウィンドウの高さ（ピクセル）</param>
	void ClearBars(const ViewRect& inner, float windowWidth, float windowHeight);
```

`RenderWindow.cpp` に include を足す。

変更後：

```cpp
#include "HTTEngine/Graphics/Renderer/GameScreen.h"
#include "HTTEngine/Graphics/Renderer/RenderContext.h"
```

`BindRenderTarget` の実装の下に足す。

変更後：

```cpp
//=============================================================================
// 四角の外だけを帯の色でクリア
//=============================================================================
void RenderWindow::ClearBars(const ViewRect& inner, float windowWidth, float windowHeight) {
	const LONG outerRight = static_cast<LONG>(windowWidth);
	const LONG outerBottom = static_cast<LONG>(windowHeight);
	const LONG left = static_cast<LONG>(inner.x);
	const LONG top = static_cast<LONG>(inner.y);
	const LONG right = left + static_cast<LONG>(inner.width);
	const LONG bottom = top + static_cast<LONG>(inner.height);

	// --- 上・下・左・右の順に、四角の外を4枚に分ける（CalcFitRect の四角なら、大きさがあるのは2枚まで）---
	const std::array<D3D12_RECT, 4> candidates = {{
	    {0, 0, outerRight, top},
	    {0, bottom, outerRight, outerBottom},
	    {0, top, left, bottom},
	    {right, top, outerRight, bottom},
	}};

	// --- 大きさのある物だけ集める ---
	std::array<D3D12_RECT, 4> bars{};
	UINT barCount = 0;
	for (const D3D12_RECT& candidate : candidates) {
		if (candidate.left < candidate.right && candidate.top < candidate.bottom) {
			bars[barCount] = candidate;
			++barCount;
		}
	}

	// 数を 0 のまま ClearRenderTargetView を呼ぶと、ウィンドウ全部がクリアされてしまう
	if (barCount == 0) {
		return;
	}
	DirectXCommon::GetCommandList()->ClearRenderTargetView(rtvHandles_[currentBackBufferIndex_], kBarColor.data(), barCount, bars.data());
}
```

### B-2 `Graphics/PostEffect/LensPass.h`

変更後：

```cpp
#include "HTTEngine/Graphics/Renderer/GameScreen.h"
#include "HTTEngine/Graphics/RenderTarget/RenderTexture.h"
```

変更後：

```cpp
	void Render(RenderTexture& source, const ViewRect& rect, bool enabled);
```

### B-3 `Graphics/PostEffect/LensPass.cpp`

変更前：

```cpp
void LensPass::Render(RenderTexture& source, float width, float height, bool enabled) {
```

変更後：

```cpp
void LensPass::Render(RenderTexture& source, const ViewRect& rect, bool enabled) {
```

変更後：

```cpp
	param.aspect = (rect.height > 0.0f) ? rect.width / rect.height : 1.0f;
```

変更前：

```cpp
	// --- ビューポートは描画先の大きさに合わせる ---
	D3D12_VIEWPORT viewport{0.0f, 0.0f, width, height, 0.0f, 1.0f};
	D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
```

変更後：

```cpp
	// --- ビューポートは描く四角に合わせる（黒帯がある画面では、ウィンドウの一部）---
	D3D12_VIEWPORT viewport{rect.x, rect.y, rect.width, rect.height, 0.0f, 1.0f};
	D3D12_RECT scissor{static_cast<LONG>(rect.x), static_cast<LONG>(rect.y), static_cast<LONG>(rect.x + rect.width), static_cast<LONG>(rect.y + rect.height)};
```

### B-4 `Graphics/PostEffect/PostProcess.h`・`PostProcess.cpp`

変更後：

```cpp
	void Composite(const ViewRect& rect, bool useLens = true); // useLens が false ならレンズを掛けずに、露出・トーンマップだけ掛けて通す
```

変更後：

```cpp
void PostProcess::Composite(const ViewRect& rect, bool useLens) {
	// 切っていても最終出力として通す（HDR の絵を、露出・トーンマップで画面の 0〜1 に収めるのもここ）
	lens_.Render(*source_, rect, useLens && kLensEnabled != 0);
}
```

### B-5 `Graphics/Renderer/SceneRenderer.h`

3つの宣言を直す。

変更後：

```cpp
	static void RenderToWindow(const Camera* camera, RenderWindow* window, const std::wstring& windowTitle, float windowWidth, float windowHeight);
```

変更後：

```cpp
	static void SetViewportAndScissor(const ViewRect& rect);
```

変更後：

```cpp
	static void RenderUI(const std::wstring& windowTitle, const ViewRect& rect);
```

### B-6 `Graphics/Renderer/SceneRenderer.cpp`

include を足す。

変更後：

```cpp
#include "HTTEngine/Graphics/RenderTarget/RenderWindow.h"
#include "HTTEngine/Graphics/Renderer/GameScreen.h"
```

`SetViewportAndScissor` の全体。

変更後：

```cpp
void SceneRenderer::SetViewportAndScissor(const ViewRect& rect) {
	D3D12_VIEWPORT vp{rect.x, rect.y, rect.width, rect.height, 0.0f, 1.0f};
	D3D12_RECT sc{static_cast<LONG>(rect.x), static_cast<LONG>(rect.y), static_cast<LONG>(rect.x + rect.width), static_cast<LONG>(rect.y + rect.height)};
	auto* cmdList = DirectXCommon::GetCommandList();
	cmdList->RSSetViewports(1, &vp);
	cmdList->RSSetScissorRects(1, &sc);
}
```

`RenderWorldForPost` の中を3か所。

変更前：

```cpp
	const float width = static_cast<float>(work->GetWidth());
	const float height = static_cast<float>(work->GetHeight());
	const DepthOfFieldSettings depthOfField = GetViewDepthOfField();
```

変更後：

```cpp
	const ViewRect workRect = {0.0f, 0.0f, static_cast<float>(work->GetWidth()), static_cast<float>(work->GetHeight())};
	const DepthOfFieldSettings depthOfField = GetViewDepthOfField();
```

変更後：

```cpp
	work->PreDraw();
	SetViewportAndScissor(workRect);
```

変更後：

```cpp
	RenderTexture* blurred = instance_->postProcess_->ApplyDepthOfField(*work, *camera, depthOfField);
	SetViewportAndScissor(workRect);
```

`RenderUI` の頭。

変更後：

```cpp
void SceneRenderer::RenderUI(const std::wstring& windowTitle, const ViewRect& rect) {
	auto* cmdList = DirectXCommon::GetCommandList();
	SetViewportAndScissor(rect);
```

`RenderToTexture`。

変更前：

```cpp
	float width = static_cast<float>(target->GetWidth());
	float height = static_cast<float>(target->GetHeight());
```

変更後：

```cpp
	const ViewRect rect = {0.0f, 0.0f, static_cast<float>(target->GetWidth()), static_cast<float>(target->GetHeight())};
```

変更後：

```cpp
	instance_->postProcess_->Composite(rect, mode == PostEffectMode::All);
	RenderUI(windowTitle, rect); // UIは効果の外側に重ねる
	target->PostDraw();
```

`RenderToWindow` の全体。

変更後：

```cpp
void SceneRenderer::RenderToWindow(const Camera* camera, RenderWindow* window, const std::wstring& windowTitle, float windowWidth, float windowHeight) {
	HTT_ASSERT_MSG(instance_->postProcess_ != nullptr, "InitializePost を先に呼んでください（3D は HDR の絵に描いてから画面へ写すので、その置き場が要る）");
	ApplyViewOptions({}); // ウィンドウ（製品版のゲーム）は全部掛ける
	RenderTexture* scene = RenderWorldForPost(camera, windowTitle);

	// 効果のパスでRTVが切り替わっているので、ウィンドウへ張り直す
	instance_->postProcess_->Prepare(*scene);
	window->BindRenderTarget(); // 描画先はウィンドウ
	// ウィンドウの中の、ゲームの比率の四角にだけ描く（外は帯の色で塗る）
	const ViewRect rect = GameScreen::CalcFitRect(windowWidth, windowHeight);
	window->ClearBars(rect, windowWidth, windowHeight);
	instance_->postProcess_->Composite(rect);
	RenderUI(windowTitle, rect); // UIは効果の外側に重ねる
}
```

### B-7 `Input/InputManager.cpp`

include を足す。

変更後：

```cpp
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Graphics/Renderer/GameScreen.h"
```

`GetGameViewRect` の製品版の所。

変更前：

```cpp
	// 製品版：ウィンドウの中全部（ゲームの絵はウィンドウいっぱいに描かれる）
	RECT client{};
	GetClientRect(GetInstance()->mainHwnd_, &client);
	topLeft = {0.0f, 0.0f};
	size = {static_cast<float>(client.right - client.left), static_cast<float>(client.bottom - client.top)};
```

変更後：

```cpp
	// 製品版：ウィンドウの中の、黒帯の内側（SceneRenderer::RenderToWindow が描く四角と同じ）
	RECT client{};
	GetClientRect(GetInstance()->mainHwnd_, &client);
	const ViewRect rect = GameScreen::CalcFitRect(static_cast<float>(client.right - client.left), static_cast<float>(client.bottom - client.top));
	topLeft = {rect.x, rect.y};
	size = {rect.width, rect.height};
```

### B-8 `Input/InputManager.h`

`GetMousePositionInGame` のコメント。

変更後：

```cpp
	/// <para>エディタでは Game ビューの絵の中、製品版ではウィンドウの黒帯の内側が画面。画面の外（黒帯の上も）なら ±1 を超える</para>
```

ここでビルドが通る。黒帯が出る。

---

## パートC 説明文

### C-1 `Editor/Windows/UIScreenEditor.cpp`

include を足す。

変更後：

```cpp
#include "HTTEngine/Editor/Windows/HierarchyWindow.h"
#include "HTTEngine/Graphics/Renderer/GameScreen.h"
```

変更後：

```cpp
	ImGui::TextWrapped("Sceneビューの絵の縁がゲームの画面");
```

変更後：

```cpp
	// --- 枠：UI はゲームの画面を絵の全体に合わせて描くので、絵の縁＝画面の縁（線の太さの半分だけ内側に描く）---
```

変更後：

```cpp
	const std::string label = std::format("UI Screen: {}  ({:.0f}×{:.0f})", NameOf(edit.filePath), GameScreen::GetWidth(), GameScreen::GetHeight());
```

### C-2 残りの言葉

Visual Studio の「フォルダーを指定して置換」（Ctrl+Shift+H）で、`HTTEngine` フォルダの中を置き換える。

| 検索 | 置換 |
|---|---|
| `1280×720 の画面のつもり` | `ゲームの画面のつもり` |
| `→ 1280×720 の画面のピクセル` | `→ ゲームの画面のピクセル` |
| `画面の枠（1280×720 の画面＝絵の縁` | `画面の枠（ゲームの画面＝絵の縁` |
| `画面の外から入れるなら 1280・720` | `画面の外から入れるなら、画面の幅か高さの値` |

置き換えた後に `1280` を検索して、`GameScreen.h` の説明と初期値、`Win32Window.h` の初期値、`main` の設定だけが残っていればよい。

---

## パートD 確認

### D-1 確認の手順

横長（既定）の確認は `main.cpp` を変えずに行う。縦長は `main.cpp` に次を足して、確認の後に戻す。

```cpp
config.width = 540;
config.height = 960;
config.gameWidth = 720;
config.gameHeight = 1280;
```

### D-2 確認項目

「写しで確認」は、一時フォルダの写しで実際に動かした結果。写経の後に、同じ項目を確かめる。

| # | 確認すること | やり方 | 写しで確認 |
|---|---|---|---|
| 1 | 既定の 16:9 は絵が変わらない | Release で `VectorShapeTest` を開く | ウィンドウの中が前の絵と全ピクセル一致 |
| 2 | 縦に長いウィンドウ | Release で高さを広げる | 上下に黒帯。円は円のまま（ウィンドウ 1000×900 で確認） |
| 3 | 横に長いウィンドウ | Release で幅を広げる | 左右に黒帯（ウィンドウ 1700×700 で確認） |
| 3b | 帯だけ黒い | 2・3 の帯の色を見る | 上下・左右の帯とも全ピクセルが 0,0,0。`kClearColor` は元の青のまま |
| 4 | 縦長のゲーム | 720×1280 で起動 | 4つの Anchor が画面の4隅。円は円のまま |
| 5 | 縦長のゲームを横に長いウィンドウで | Release で幅を広げる | 左右に黒帯 |
| 6 | 黒帯のときのマウス | `GetMousePositionInGame` をログへ出す | 真ん中が 0,0 付近。黒帯の上は y が 1 を超え、`IsMouseInGame` が false |
| 7 | エディタの Scene・Game ビュー | 縦長で起動 | 縦長の絵が真ん中に出る。余白は元の暗い色 |
| 8 | 縦長での UI のドラッグ | Scene ビューで図形を動かす | (0, 200) が計算どおり (87, 113) へ動いた |
| 9 | UI Screen の編集 | 縦長で `.uiscreen` を開く | 枠の文字が `720×1280` |
| 10 | 3 構成 | Debug・Development・Release | ビルドと起動・終了が通る |

### D-3 検証の結果

- **ビルド：** Debug・Development・Release の3構成が通った。パートA の後も Development で通った。
- **実行：** 3構成とも起動と終了を確かめた（終了コード 0）。ログに警告とエラーは無かった。
- **`ClearBars`：** 確認の間だけ Release でデバッグレイヤーを有効にして、ウィンドウ 1000×900 と 1700×700 で動かした。止まらず、ログに警告もエラーも無かった。エディタ（Debug・Development）は `RenderToWindow` を通らないので `ClearBars` を呼ばない。
- **エディタの操作：** ImGui へ入力を流して確かめた。マウスの確認は `SetCursorPos` で本物のカーソルを動かした。

### D-4 未確認のこと・気を付けること

- **Alt+Enter のフルスクリーン：** 試していない（ウィンドウの大きさの変更と同じ通り道を通る）。
- **`CameraFollow` の Bounds：** 式は直したが、縦長で動かしていない。
- **ビルドの警告：** エラーだけを見た。
- **横長で作ったシーンを縦長で開いたとき：** UI の位置はピクセルのまま読むので、自動で並び直さない。Anchor で端からの位置にしてある物は端に付いたまま動く。
- **モニタより大きいウィンドウ：** `config.height = 1280` は 1080 のモニタに入らない。`gameWidth`・`gameHeight` は 1280 のまま、ウィンドウの `width`・`height` を小さくする。
