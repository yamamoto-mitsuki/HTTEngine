#include "SpriteRenderSystem.h"

#include <array>
#include <cmath>
#include <memory>
#include <vector>
#include <utility>
#include <algorithm>

#include "MyEngine/Camera/Camera.h"
#include "MyEngine/Components/Core/TransformComponent.h"
#include "MyEngine/Components/UI/AlphaGroupComponent.h"
#include "MyEngine/Diagnostics/MyAssert.h"
#include "MyEngine/ECS/ComponentSerializer.h"
#include "MyEngine/ECS/ComponentUI.h"
#include "MyEngine/ECS/EntityManager.h"
#include "MyEngine/ECS/GameComponent.h"
#include "MyEngine/Editor/Inspector/ComponentEditor.h"
#include "MyEngine/Editor/Viewport/SceneVisibility.h"
#include "MyEngine/Graphics/Renderer/Renderer.h"
#include "MyEngine/Graphics/Texture/TextureManager.h"
#include "MyEngine/Input/InputManager.h"
#include "MyEngine/Math/Color.h"
#include "MyEngine/Systems/UI/UIMaskSystem.h"
#include "MyEngine/Systems/UI/UIWorldFollowSystem.h"

namespace {
constexpr float kAlphaClipThreshold = 0.5f;     // Alpha Clip のとき、これより透明なピクセルを捨てる
constexpr float kDefaultPixelsPerUnit = 100.0f; // Pixels Per Unit に0以下が入っていたときに使う値
constexpr float kMinTileSize = 1.0e-3f;         // Tile Size の下限（ピクセル。0 で割らないように）

/// <summary>
/// 画像の中の1コマの場所（UV。0〜1）。左上が uvMin、右下が uvMax（反転すると入れ替わる）
/// </summary>
struct FrameUV {
	Vector2 min;
	Vector2 max;
};


//=============================================================================
// 画像の使う範囲と形の計算
//=============================================================================
// frame 番目のコマの UV。番号が範囲の外なら、ぐるっと回す（アニメで frame を増やし続けてよい）
FrameUV CalcFrameUV(int columns, int rows, int frame) {
	const int safeColumns = (std::max)(columns, 1);
	const int safeRows = (std::max)(rows, 1);
	const int count = safeColumns * safeRows;
	const int index = ((frame % count) + count) % count; // マイナスの番号でも 0〜count-1 にする
	const float width = 1.0f / static_cast<float>(safeColumns);
	const float height = 1.0f / static_cast<float>(safeRows);
	FrameUV uv;
	uv.min = {static_cast<float>(index % safeColumns) * width, static_cast<float>(index / safeColumns) * height};
	uv.max = {uv.min.x + width, uv.min.y + height};
	return uv;
}

// Rect の大きさ（ピクセル）。0 以下の向きは、左上から画像の端まで
Vector2 CalcRectSize(const Vector2& rectPosition, const Vector2& rectSize, const Vector2& textureSize) {
	const float width = (rectSize.x > 0.0f) ? rectSize.x : textureSize.x - rectPosition.x;
	const float height = (rectSize.y > 0.0f) ? rectSize.y : textureSize.y - rectPosition.y;
	return {width, height};
}

// 反転は、貼る向きを逆にするだけ（左の端に右の UV、右の端に左の UV）
FrameUV FlipUV(FrameUV uv, bool flipX, bool flipY) {
	if (flipX) {
		std::swap(uv.min.x, uv.max.x);
	}
	if (flipY) {
		std::swap(uv.min.y, uv.max.y);
	}
	return uv;
}

// 画像の中で使う範囲の UV。Use Rect なら Rect のピクセルの範囲、そうでなければ Sheet のコマ（画像が無い＝白い板のときは Rect を使わない）
// Sprite Renderer と Image は同じ名前のメンバ（textureHandle・useRect・columns など）を持つので、テンプレートで1つの関数にして両方に使う
template<class T> FrameUV CalcSourceUV(const T& component) {
	if (!component.useRect || component.textureHandle == 0) {
		return FlipUV(CalcFrameUV(component.columns, component.rows, component.frame), component.flipX, component.flipY);
	}
	// ピクセル → UV（画像の幅・高さで割る。画像の左上が 0,0、右下が 1,1）
	const Vector2 textureSize = TextureManager::GetTextureSize(component.textureHandle);
	const Vector2 size = CalcRectSize(component.rectPosition, component.rectSize, textureSize);
	FrameUV uv;
	uv.min = {component.rectPosition.x / textureSize.x, component.rectPosition.y / textureSize.y};
	uv.max = {(component.rectPosition.x + size.x) / textureSize.x, (component.rectPosition.y + size.y) / textureSize.y};
	return FlipUV(uv, component.flipX, component.flipY);
}

// 画像の中で使う範囲の大きさ（ピクセル）。Sprite Renderer の板の大きさ・Image の Set Native Size に使う
template<class T> Vector2 CalcSourceSize(const T& component) {
	if (!component.useRect || component.textureHandle == 0) {
		return SpriteRenderSystem::CalcFrameSize(component.textureHandle, component.columns, component.rows);
	}
	return CalcRectSize(component.rectPosition, component.rectSize, TextureManager::GetTextureSize(component.textureHandle));
}

// アンカーの場所（画面の真ん中から、ピクセルで。Y は上が＋）
// ScreenAnchor は「左上・上・右上 / 左・真ん中・右 / 左下・下・右下」の順に並んでいるので、
// 番号を3で割った余りが横（0=左 1=真ん中 2=右）、商が縦（0=上 1=真ん中 2=下）
Vector2 AnchorPosition(ScreenAnchor anchor) {
	const int index = static_cast<int>(anchor);
	const float column = static_cast<float>(index % 3 - 1); // -1=左 0=真ん中 1=右
	const float row = static_cast<float>(1 - index / 3);    //  1=上 0=真ん中 -1=下
	return {column * GameScreen::GetWidth() * 0.5f, row * GameScreen::GetHeight() * 0.5f};
}

// Image の4つの角を、画面の正規化座標で出す（描くのにも、マウスが乗ったかを調べるのにも使う）。Fill の分だけ右の端を縮める
ScreenQuad CalcImageQuad(const ImageComponent& image, const TransformComponent& transform) {
	return SpriteRenderSystem::CalcScreenQuad(transform.worldMatrix, image.anchor, image.pivot, image.size, image.fill);
}

// 板の大きさ（使う範囲＝1コマか Rect のピクセル数 ÷ Pixels Per Unit。画像が無ければ 1×1）
Vector2 CalcSpriteSize(const SpriteRendererComponent& sprite) {
	const float pixelsPerUnit = (sprite.pixelsPerUnit > 0.0f) ? sprite.pixelsPerUnit : kDefaultPixelsPerUnit;
	const Vector2 pixels = (sprite.textureHandle == 0) ? Vector2{pixelsPerUnit, pixelsPerUnit} : CalcSourceSize(sprite);
	return pixels / pixelsPerUnit;
}

// ビルボードの板を置く行列（シェーダー Object3d.VS と同じ計算。位置と大きさは world のまま、向きだけカメラの軸に置き換える）
Matrix4x4 MakeBillboardMatrix(const Matrix4x4& world, const Camera& camera, BillboardMode billboard) {
	// カメラの右・上（ビュー行列の1列目・2列目。SceneRenderer::UploadCameraCB がシェーダーへ送るのと同じ値）
	const Matrix4x4& view = camera.GetViewMatrix();
	Vector3 axisX = {view.m[0][0], view.m[1][0], view.m[2][0]};
	Vector3 axisY = {view.m[0][1], view.m[1][1], view.m[2][1]};
	if (billboard == BillboardMode::AxisY) {
		// Y 軸だけ回す：上はワールドの上のまま、右はカメラの右を水平にした物
		const Vector3 flatRight = {axisX.x, 0.0f, axisX.z};
		const float flatLength = Length(flatRight);
		axisX = (flatLength > 1.0e-4f) ? flatRight / flatLength : axisX;
		axisY = {0.0f, 1.0f, 0.0f};
	}
	const Vector3 axisZ = Cross(axisX, axisY); // 左手座標系なので「右 × 上 = 奥」
	const std::array<Vector3, 3> axes = {axisX, axisY, axisZ};

	Matrix4x4 result = world; // 4行目（位置）はそのまま
	for (int row = 0; row < 3; ++row) {
		const float scale = Length(Vector3{world.m[row][0], world.m[row][1], world.m[row][2]});
		result.m[row][0] = axes[row].x * scale;
		result.m[row][1] = axes[row].y * scale;
		result.m[row][2] = axes[row].z * scale;
	}
	return result;
}

// 点 p が、a → b の線のどちら側にあるか（左なら＋、右なら−。2次元の外積）
float SideOf(const Vector2& a, const Vector2& b, const Vector2& p) { return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x); }

//=============================================================================
// 描く
//=============================================================================
// Sprite Renderer：Entity のワールド行列の所に、画像の板を置く
void DrawSprites(Camera* camera) {
	EntityManager::ForEach<SpriteRendererComponent>([camera](Handle<Entity> entity, const SpriteRendererComponent& sprite) {
		if (!sprite.enabled || !EntityManager::IsActiveInHierarchy(entity)) {
			return;
		}
		const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
		if (transform == nullptr) {
			return;
		}
		// 親までの Alpha Group を掛ける（0 なら描かない）
		const float groupAlpha = SpriteRenderSystem::CalcGroupAlpha(entity);
		if (groupAlpha <= 0.0f) {
			return;
		}
		const FrameUV uv = CalcSourceUV(sprite);

		Renderer::Sprite3dConfig config;
		config.worldMatrix = &transform->worldMatrix;
		config.size = CalcSpriteSize(sprite);
		config.pivot = sprite.pivot;
		config.uvMin = uv.min;
		config.uvMax = uv.max;
		config.color = ApplyIntensity(MultiplyAlpha(sprite.color, groupAlpha), sprite.intensity); // 明るさの倍率はリニアで掛ける（Color.h）
		config.textureHandle = sprite.textureHandle;
		config.blendMode = sprite.blend;
		config.billboard = sprite.billboard;
		config.camera = camera;
		if (sprite.alphaClip) {
			// 切り抜き：透明な所を捨てて、奥行きを書く（ほかの物と前後が正しく重なる）
			config.depthMode = DepthMode::TestWrite;
			config.alphaCutoff = kAlphaClipThreshold;
		} else {
			// 半透明：奥行きは書かず、カメラから遠い順に描く
			config.depthMode = DepthMode::TestNoWrite;
		}
		config.isSceneHidden = SceneVisibility::IsHidden(entity); // Hierarchy の目で隠した物は、Scene ビューだけ描かない
		Renderer::DrawSprite3d(config);
	});
}

// ===== Image の UV。Tiled なら、画像全体を Tile Size ごとに並べて繰り返す =====
// サンプラーは WRAP（1 を超えた UV は画像の頭に戻る）なので、UV を「並べる枚数」まで伸ばすだけで繰り返す
FrameUV CalcImageUV(const ImageComponent& image) {
	if (!image.tiled || image.textureHandle == 0) {
		return CalcSourceUV(image);
	}
	const Vector2 textureSize = TextureManager::GetTextureSize(image.textureHandle);
	const Vector2 tile = {
	    (std::max)((image.tileSize.x > 0.0f) ? image.tileSize.x : textureSize.x, kMinTileSize),
	    (std::max)((image.tileSize.y > 0.0f) ? image.tileSize.y : textureSize.y, kMinTileSize),
	};
	FrameUV uv;
	uv.min = image.tileOffset;
	uv.max = {image.tileOffset.x + image.size.x / tile.x, image.tileOffset.y + image.size.y / tile.y};
	return FlipUV(uv, image.flipX, image.flipY);
}

// ===== Image：画面に重ねる（重ねる順・マスクの中かは UIMaskSystem が決め、RenderQueue が並べる）=====
void DrawImage(Handle<Entity> entity, const ImageComponent& image) {
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	if (transform == nullptr) {
		return;
	}
	// 親までの Alpha Group を掛ける（0 なら描かない）
	const float groupAlpha = SpriteRenderSystem::CalcGroupAlpha(entity);
	if (groupAlpha <= 0.0f) {
		return;
	}
	const ScreenQuad quad = CalcImageQuad(image, *transform);
	const FrameUV uv = CalcImageUV(image);
	// Fill の分だけ、右の端の U も縮める（画面の左から、Fill の割合だけ貼る。反転していても同じ）
	const float uRight = uv.min.x + (uv.max.x - uv.min.x) * std::clamp(image.fill, 0.0f, 1.0f);

	Renderer::Quad2dConfig config;
	config.lb = quad.lb;
	config.lt = quad.lt;
	config.rb = quad.rb;
	config.rt = quad.rt;
	config.uvLb = {uv.min.x, uv.max.y};
	config.uvLt = {uv.min.x, uv.min.y};
	config.uvRb = {uRight, uv.max.y};
	config.uvRt = {uRight, uv.min.y};
	config.color = ClampColor(MultiplyAlpha(image.color, groupAlpha));
	config.textureHandle = image.textureHandle;
	config.blendMode = image.blend;
	config.rasterizerType = RasterizerType::SolidNone;                              // 反転・回転で裏返っても消えないように、両面を描く
	UIMaskSystem::MakeStep(entity, image.order, UIDrawKind::Image).ApplyTo(config); // Order の順に重なる。マスクの中なら切り抜く
	config.isSceneHidden = SceneVisibility::IsHidden(entity);                       // Hierarchy の目で隠した物は、Scene ビューだけ描かない
	Renderer::DrawQuad2d(config);
}

//=============================================================================
// Inspector と 保存
//=============================================================================
// 画像のどこを使うか（Sheet のコマ割りと Rect の切り取り）の見せ方。Sprite Renderer と Image で同じなので、テンプレートで1つにする
template<class T> 
void DescribeSource(ComponentUI& ui, T& value) {
	ui.Separator("Sheet");
	ui.Field("Columns###columns", value.columns, AtLeast(1.0f).Tip("画像を横に何コマに分けるか"));
	ui.Field("Rows###rows", value.rows, AtLeast(1.0f).Tip("縦に何コマに分けるか"));
	ui.Field("Frame###frame", value.frame, Tip("何コマ目を出すか（左上が 0。数を超えたら最初に戻る）"));
	ui.Separator("Rect");
	ui.Field("Use Rect###useRect", value.useRect, Tip("画像のピクセルの範囲を切り取って使う（オンの間は Sheet を使わない）"));
	ui.Field("Rect Position###rectPosition", value.rectPosition, AtLeast(0.0f).Drag(1.0f).Tip("切り取る範囲の左上（ピクセル。画像の左上が 0,0、右・下が＋）"));
	ui.Field("Rect Size###rectSize", value.rectSize, AtLeast(0.0f).Drag(1.0f).Tip("切り取る範囲の大きさ（ピクセル。0 の向きは画像の端まで）"));
}

// Sprite Renderer の見せ方。Inspector とシーンファイルの両方がこの並びを使う（Collider と同じ書き方）
void DescribeSpriteRenderer(ComponentUI& ui, SpriteRendererComponent& value) {
	ui.Field("Enabled###enabled", value.enabled);
	ui.AssetField("Sprite###texture", value.textureHandle, AssetType::Texture, Tip("resources の下の画像（無ければ白い板）"));
	ui.ColorField("Color###color", value.color);
	ui.Field("Intensity###intensity", value.intensity, AtLeast(0.0f).Tip("明るさの倍率（色はそのまま）。1 より大きくすると HDR の明るさになり、ブルーム（Threshold 1）で光る。Blend を Add にすると光の板になる"));
	ui.Field("Pixels Per Unit###pixelsPerUnit", value.pixelsPerUnit, AtLeast(0.01f).Tip("画像の何ピクセルを長さ1にするか（100 なら 256 ピクセルが 2.56）"));
	ui.Field("Pivot###pivot", value.pivot, Tip("板のどこを位置に合わせるか（0,0=左下 0.5,0=足元 1,1=右上）"));
	ui.Field("Flip X###flipX", value.flipX, Tip("左右反転"));
	ui.Field("Flip Y###flipY", value.flipY, Tip("上下反転"));
	ui.Field("Billboard###billboard", value.billboard, Tip("カメラの方を向く（Full：いつも正面 / AxisY：縦に立てたまま回る）"));
	ui.Field("Blend###blend", value.blend, Tip("Normal：透明な所が透ける / Add：足して光る"));
	ui.Field("Alpha Clip###alphaClip", value.alphaClip, Tip("半分より透明な所を捨てて奥行きを書く（たくさん重ねても前後が崩れない。縁はギザギザ）"));
	DescribeSource(ui, value);
}

// Image の見せ方
void DescribeImage(ComponentUI& ui, ImageComponent& value) {
	ui.Field("Enabled###enabled", value.enabled);
	ui.AssetField("Image###texture", value.textureHandle, AssetType::Texture, Tip("resources の下の画像（無ければ白い四角）"));
	ui.ColorField("Color###color", value.color);
	ui.Field("Size###size", value.size, AtLeast(0.0f).Tip("大きさ（ピクセル。ゲームの画面のつもりで）"));
	// 画像の使う範囲（1コマか Rect）と同じ大きさにする（保存・読み込みのときは Button が false を返すので、何も起きない）
	if (value.textureHandle != 0 && ui.Button("Set Native Size")) {
		value.size = CalcSourceSize(value);
	}
	ui.Field("Anchor###anchor", value.anchor, Tip("画面のどこから位置を数えるか（位置は Transform の Translation の x・y。ピクセルで、上が＋）"));
	ui.Field("Pivot###pivot", value.pivot, Tip("絵のどこを位置に合わせるか（0,0=左下 0,1=左上 1,1=右上）"));
	ui.Field("Fill###fill", value.fill, Range(0.0f, 1.0f).Tip("左から何割まで出すか（ゲージに）"));
	ui.Field("Order###order", value.order, Tip("重ねる順（大きいほど手前）"));
	ui.Field("Flip X###flipX", value.flipX, Tip("左右反転"));
	ui.Field("Flip Y###flipY", value.flipY, Tip("上下反転"));
	ui.Field("Blend###blend", value.blend, Tip("Normal：透明な所が透ける / Add：足して光る"));
	// --- 繰り返し（模様）。オンの間は Sheet・Rect を使わない（灰色にするだけで、保存はする）---
	ui.Separator("Tile");
	ui.Field("Tiled###tiled", value.tiled, Tip("画像全体を並べて繰り返す（ドット・しま模様など）。Size を変えても模様は伸びず、並ぶ数が変わる"));
	ui.BeginDisabled(!value.tiled);
	ui.Field("Tile Size###tileSize", value.tileSize, AtLeast(0.0f).Drag(1.0f).Tip("1枚の大きさ（ピクセル）。0 の向きは画像の大きさ"));
	ui.Field("Tile Offset###tileOffset", value.tileOffset, Drag(0.01f).Tip("模様をずらす量（1 で1枚分。Timeline で動かすと模様が流れる）"));
	ui.EndDisabled();
	ui.BeginDisabled(value.tiled);
	DescribeSource(ui, value);
	ui.EndDisabled();
}

// Alpha Group の見せ方
void DescribeAlphaGroup(ComponentUI& ui, AlphaGroupComponent& value) {
	ui.Field("Enabled###enabled", value.enabled, Tip("外すと掛けない"));
	ui.Field("Alpha###alpha", value.alpha, Range(0.0f, 1.0f).Tip("自分と子の Image・Text・Sprite Renderer の透明度の倍率（0 なら見えない）。親の Alpha Group とも掛け合わせる"));
	ui.Label("Trigger Activator のフェードは、ここを動かす（再生中は、ここで直しても上書きされる）");
}
} // namespace

//=============================================================================
// 初期化 / 解放
//=============================================================================
void SpriteRenderSystem::Initialize() {
	// 3つの Component の置き場所・保存・Inspector を登録する。見せ方はそれぞれ Describe〜 1つで、保存と Inspector の両方に使う
	EntityManager::RegisterComponent<SpriteRendererComponent>();
	ComponentSerializer::Register<SpriteRendererComponent>("Sprite Renderer", &DescribeSpriteRenderer);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<SpriteRendererComponent>>("Sprite Renderer", "Rendering2D", &DescribeSpriteRenderer));
	EntityManager::RegisterComponent<ImageComponent>();
	ComponentSerializer::Register<ImageComponent>("Image", &DescribeImage);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<ImageComponent>>("Image", "UI", &DescribeImage));
	// Alpha Group は描く物ではないが、Image・Text・Sprite Renderer の3つが読むので、ここで登録する
	EntityManager::RegisterComponent<AlphaGroupComponent>();
	ComponentSerializer::Register<AlphaGroupComponent>("Alpha Group", &DescribeAlphaGroup);
	ComponentEditorRegistry::Register(std::make_unique<DescribedComponentEditor<AlphaGroupComponent>>("Alpha Group", "UI", &DescribeAlphaGroup));
}

void SpriteRenderSystem::Release() {} // 持っている物は無い（Image の重ねる順は RenderQueue が並べる）

//=============================================================================
// 描く（停止中も）
//=============================================================================
void SpriteRenderSystem::Draw(Camera* camera) {
	DrawSprites(camera);
	// Image：見つけた順に積む（重ねる順は drawKey で RenderQueue が並べる）
	EntityManager::ForEach<ImageComponent>([](Handle<Entity> entity, const ImageComponent& image) {
		if (image.enabled && EntityManager::IsActiveInHierarchy(entity)) {
			DrawImage(entity, image);
		}
	});
}

//=============================================================================
// ゲームから使う
//=============================================================================
bool SpriteRenderSystem::IsMouseOver(Handle<Entity> entity) {
	const ImageComponent* image = EntityManager::Get<ImageComponent>(entity);
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	if (image == nullptr || transform == nullptr || !image->enabled || !EntityManager::IsActiveInHierarchy(entity) || !InputManager::IsMouseInGame()) {
		return false;
	}
	// UI World Follow が隠している物（描いていない）には当たらない
	if (UIWorldFollowSystem::IsHiddenInHierarchy(entity)) {
		return false;
	}
	// マウスの位置（GetMousePositionInGame）も、描いた角（CalcImageQuad）も、同じ正規化座標
	return IsInsideQuad(InputManager::GetMousePositionInGame(), CalcImageQuad(*image, *transform));
}

Vector2 SpriteRenderSystem::CalcFrameSize(uint32_t textureHandle, int columns, int rows) {
	const Vector2 textureSize = TextureManager::GetTextureSize(textureHandle);
	return {textureSize.x / static_cast<float>((std::max)(columns, 1)), textureSize.y / static_cast<float>((std::max)(rows, 1))};
}

float SpriteRenderSystem::CalcGroupAlpha(Handle<Entity> entity) {
	// 自分 → 親 → 親の親 … と一番上までたどって、有効な Alpha Group の Alpha を掛けていく（親子は輪にならない。SetParent が断る）
	// UI World Follow が隠している（画面の外など）なら 0（Image・Text・Vector Shape・Polyline は 0 なら描かない）
	float alpha = 1.0f;
	for (Handle<Entity> current = entity; current.IsValid();) {
		if (const AlphaGroupComponent* group = EntityManager::Get<AlphaGroupComponent>(current); group != nullptr && group->enabled) {
			alpha *= std::clamp(group->alpha, 0.0f, 1.0f);
		}
		if (const UIWorldFollowComponent* follow = EntityManager::Get<UIWorldFollowComponent>(current); follow != nullptr && follow->enabled && follow->isHidden) {
			return 0.0f;
		}
		const Entity* owner = EntityManager::Get(current);
		current = (owner != nullptr) ? owner->parent : Handle<Entity>{};
	}
	return alpha;
}

Vector2 SpriteRenderSystem::CalcScreenPoint(const Matrix4x4& worldMatrix, ScreenAnchor anchor, const Vector2& local) {
	// --- 1. Transform のワールド行列で回す・大きくする・動かす（x・y だけ使う。親子も効く）---
	// --- 2. アンカーの場所を足して、ピクセル → 正規化座標（基準の画面の半分で割る）---
	const Matrix4x4& m = worldMatrix;
	const Vector2 anchorPosition = AnchorPosition(anchor);
	const float pixelX = local.x * m.m[0][0] + local.y * m.m[1][0] + m.m[3][0] + anchorPosition.x;
	const float pixelY = local.x * m.m[0][1] + local.y * m.m[1][1] + m.m[3][1] + anchorPosition.y;
	return {pixelX / (GameScreen::GetWidth() * 0.5f), pixelY / (GameScreen::GetHeight() * 0.5f)};
}

ScreenQuad SpriteRenderSystem::CalcScreenQuad(const Matrix4x4& worldMatrix, ScreenAnchor anchor, const Vector2& pivot, const Vector2& size, float fill) {
	// --- 1. 四角の4つの角（ピクセル。pivot の所が原点、Y が上）。Fill の分だけ右の端を縮める ---
	const float left = -pivot.x * size.x;
	const float right = left + size.x * std::clamp(fill, 0.0f, 1.0f);
	const float bottom = -pivot.y * size.y;
	const float top = bottom + size.y;

	// --- 2. 画面の正規化座標へ（Transform・アンカーの掛け方は CalcScreenPoint。Image・Text・エディタの枠で同じ）---
	const auto toScreen = [&worldMatrix, anchor](float x, float y) { return CalcScreenPoint(worldMatrix, anchor, {x, y}); };
	return {toScreen(left, bottom), toScreen(left, top), toScreen(right, bottom), toScreen(right, top)};
}

Vector2 SpriteRenderSystem::CalcScreenScale(const Matrix4x4& worldMatrix) {
	// std::hypot(x, y)：sqrt(x * x + y * y) と同じ長さを、途中であふれにくい形で出す
	const Matrix4x4& m = worldMatrix;
	return {std::hypot(m.m[0][0], m.m[0][1]), std::hypot(m.m[1][0], m.m[1][1])};
}

bool SpriteRenderSystem::IsInsideQuad(const Vector2& point, const ScreenQuad& quad) {
	// 左下 → 右下 → 右上 → 左上 → 左下 と回る4つの辺
	const std::array<float, 4> sides = {SideOf(quad.lb, quad.rb, point), SideOf(quad.rb, quad.rt, point), SideOf(quad.rt, quad.lt, point), SideOf(quad.lt, quad.lb, point)};
	const bool allLeft = std::ranges::all_of(sides, [](float side) { return side >= 0.0f; });
	const bool allRight = std::ranges::all_of(sides, [](float side) { return side <= 0.0f; });
	return allLeft || allRight;
}

//=============================================================================
// エディタから使う
//=============================================================================
bool SpriteRenderSystem::CalcSpritePlacement(Handle<Entity> entity, const Camera* camera, AABB& localBox, Matrix4x4& matrix) {
	const SpriteRendererComponent* sprite = EntityManager::Get<SpriteRendererComponent>(entity);
	const TransformComponent* transform = EntityManager::Get<TransformComponent>(entity);
	if (sprite == nullptr || transform == nullptr || !sprite->enabled || !EntityManager::IsActiveInHierarchy(entity)) {
		return false;
	}
	// 板の4つの角は Renderer::DrawSprite3d と同じ（pivot の所が原点、Y が上）
	const Vector2 size = CalcSpriteSize(*sprite);
	const float left = -sprite->pivot.x * size.x;
	const float bottom = -sprite->pivot.y * size.y;
	localBox.min = {left, bottom, 0.0f};
	localBox.max = {left + size.x, bottom + size.y, 0.0f};
	const bool isBillboard = (sprite->billboard != BillboardMode::None) && (camera != nullptr);
	matrix = isBillboard ? MakeBillboardMatrix(transform->worldMatrix, *camera, sprite->billboard) : transform->worldMatrix;
	return true;
}