#pragma once
#include <array>
#include <cstddef>
#include <span>
#include <list>
#include <memory>
#include <vector>
#include <string>
#include <cstdint>
#include <unordered_map>

#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/Math/MathIncludes.h"
#include "HTTEngine/Particle/ParticleManager.h"
#include "HTTEngine/Graphics/GPU/DirectXCommon.h"
#include "HTTEngine/Graphics/Pipeline/RenderStates.h"
#include "HTTEngine/Graphics/Pipeline/VertexFormat.h"
#include "HTTEngine/Graphics/Pipeline/ShaderConstants.h"
#include "HTTEngine/Graphics/Renderer/GameScreen.h"
#include "HTTEngine/Graphics/IBL/IBLEnvironment.h"
#include "HTTEngine/Graphics/Model/ModelManager.h"


// 前方宣言
struct Entity;
struct PolylineMeshData;
class RenderContext;
class RenderWindow;
class Camera;


/// <summary>
/// AABB,OBBなど基本矩形を描画するクラス
/// <para>Config の色は sRGB の見た目の値（Vector4 の RGBA）。中でリニアに直してシェーダーへ送る</para>
/// <para>3D の色の RGB は 1 を超えてよい（3D は HDR の絵に描くので、1 を超えた分も残り、ブルームで光る）</para>
/// </summary>
class Renderer {
public:
	// ===== Passkey Idiom =====
	class ConstructorKey {
	private:
		ConstructorKey() = default;
		friend class Renderer;
	};
	explicit Renderer(ConstructorKey) {} // 鍵を受け取るコンストラクタ

	//=============================================================================
	// Model
	//=============================================================================
	// --- モデルの描画設定 ---
	struct ModelConfig : DrawTarget {
		uint32_t modelHandle = 0;                                  // モデルハンドル
		uint32_t textureHandle = 0;                                // テクスチャハンドル
		Transform transform;                                       // 拡縮、回転、移動
		Transform uvTransform;                                     // UVの拡縮、回転、移動
		const Matrix4x4* worldMatrix = nullptr;                    // 指定するとtransformの代わりに使う（階層・ロール用）
		const std::vector<Matrix4x4>* nodeMatrices = nullptr;      // ノードの行列（AnimationPlayer::GetNodeMatrices）。nullptr・数が合わないときはモデルに入っている行列
		Handle<Entity> entity;                                     // 持ち主のEntity、スキンのあるメッシュを CS で曲げるとき、出力バッファを見分けるのに使う（無効なら曲げない）
		Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};                               // 物ごとの色（sRGB。読み込んだマテリアルの色に掛ける）
		ShadingType shadingType = ShadingType::Unlit;              // シェーディング設定
		BlendMode blendMode = BlendMode::Normal;                   // ブレンド設定
		RasterizerType rasterizerType = RasterizerType::SolidBack; // ラスタライザ設定
		DepthMode depthMode = DepthMode::TestWrite;                // 深度設定
		MaterialParams material;                                   // マテリアル調整パラメータ
		bool overrideMaterial = false;                             // true：material を、読み込んだマテリアルの値の代わりに使う（全部のメッシュに同じ値。テクスチャは読み込んだ物のまま）
		Camera* camera = nullptr;                                  // カメラ設定
		IBLEnvironment* env = nullptr;                             // 環境光源
		BillboardMode billboard = BillboardMode::None;             // カメラの方を向かせる（Full / AxisY。cameraが必要）
		bool receivesDecals = true;                                // false：デカールを受けない（不透明なときだけ効く）
	};

	// --- 3D三角形の描画設定 ---
	struct TriangleConfig : DrawTarget {
		Vector3 top = {0.0f, 0.5f, 0.0f};                          // 上頂点の座標
		Vector3 right = {0.5f, -0.5f, 0.0f};                       // 右頂点の座標
		Vector3 left = {-0.5f, -0.5f, 0.0f};                       // 左頂点の座標
		Transform transform;                                       // 拡縮、回転、移動
		Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};                               // 色
		float alphaCutoff = 0.0f;                                  // これ以下のアルファのピクセルを捨てる
		Transform uvTransform;                                     // UVの拡縮、回転、移動
		Vector2 uvTop = {0.5f, 0.0f};                              // 上頂点のUV座標
		Vector2 uvRight = {1.0f, 1.0f};                            // 右頂点のUV座標
		Vector2 uvLeft = {0.0f, 1.0f};                             // 左頂点のUV座標
		uint32_t textureHandle = 0;                                // テクスチャハンドル
		ShadingType shadingType = ShadingType::Unlit;              // シェーディング設定
		BlendMode blendMode = BlendMode::Normal;                   // ブレンド設定
		RasterizerType rasterizerType = RasterizerType::SolidBack; // ラスタライザ設定
		DepthMode depthMode = DepthMode::TestWrite;                // 深度設定
		Camera* camera = nullptr;                                  // カメラ設定
		bool isBillboard = false;                                  // trueで常にカメラの方を向く（cameraが必要）
	};

	// --- 3D球の描画設定 ---
	struct SphereConfig : DrawTarget {
		Transform transform;                                       // 拡縮、回転、移動
		Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};                               // 色
		float alphaCutoff = 0.0f;                                  // これ以下のアルファのピクセルを捨てる
		Transform uvTransform;                                     // UVの拡縮、回転、移動
		uint32_t textureHandle = 0;                                // テクスチャハンドル
		int subdivision = 16;                                      // 分割数
		ShadingType shadingType = ShadingType::Unlit;              // シェーディング設定
		BlendMode blendMode = BlendMode::Normal;                   // ブレンド設定
		RasterizerType rasterizerType = RasterizerType::SolidBack; // ラスタライザ設定
		DepthMode depthMode = DepthMode::TestWrite;                // 深度設定
		const MaterialParams* material = nullptr;                  // 光沢・金属感・自己発光など（nullptr なら既定の値。切り抜きは alphaCutoff の方を使う）
		IBLEnvironment* env = nullptr;                             // 環境光（PBR に要る。無ければ PBR の代わりに Half Lambert で描く）
		Camera* camera = nullptr;                                  // カメラ設定
		bool receivesDecals = true;                                // false：デカールを受けない（不透明なときだけ効く）
		bool isBillboard = false;                                  // trueで常にカメラの方を向く（cameraが必要）
	};

	// --- 3D矩形の描画設定 ---
	// 空間に大きさ1*1の板を設置する
	struct Rect3dConfig : DrawTarget {
		Transform transform;                                       // 拡縮、回転、移動
		Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};                               // 色
		float alphaCutoff = 0.0f;                                  // これ以下のアルファのピクセルを捨てる
		Transform uvTransform;                                     // UVの拡縮、回転、移動
		uint32_t textureHandle = 0;                                // テクスチャハンドル
		ShadingType shadingType = ShadingType::Unlit;              // シェーディング設定
		BlendMode blendMode = BlendMode::Normal;                   // ブレンド設定
		RasterizerType rasterizerType = RasterizerType::SolidBack; // ラスタライザ設定
		DepthMode depthMode = DepthMode::TestWrite;                // 深度設定
		Camera* camera = nullptr;                                  // カメラ設定
		bool isBillboard = false;                                  // trueで常にカメラの方を向く（cameraが必要）
	};

	// --- 3D空間に置く画像の板（スプライト）---
	// 板は worldMatrix のローカルの XY 平面に立つ（表は -Z 側。カメラを置いたときの向きのまま見える）。裏からも見える
	struct Sprite3dConfig : DrawTarget {
		const Matrix4x4* worldMatrix = nullptr;                    // 置く場所・向き・大きさ（Entity のワールド行列。nullptr なら原点）
		Vector2 size = {1.0f, 1.0f};                               // 板の幅・高さ（ローカル座標）
		Vector2 pivot = {0.5f, 0.5f};                              // 板のどこを原点に合わせるか（0,0=左下 1,1=右上）
		Vector2 uvMin = {0.0f, 0.0f};                              // 画像のどこを貼るか：左上のUV（左右反転するなら uvMin.x と uvMax.x を入れ替える）
		Vector2 uvMax = {1.0f, 1.0f};                              // 画像のどこを貼るか：右下のUV
		Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};                               // 色
		float alphaCutoff = 0.0f;                                  // これ以下のアルファのピクセルを捨てる
		Transform uvTransform;                                     // UVの拡縮、回転、移動
		uint32_t textureHandle = 0;                                // テクスチャハンドル（0なら白）
		ShadingType shadingType = ShadingType::Unlit;              // シェーディング設定
		BlendMode blendMode = BlendMode::Normal;                   // ブレンド設定
		RasterizerType rasterizerType = RasterizerType::SolidNone; // ラスタライザ設定（既定は両面）
		DepthMode depthMode = DepthMode::TestNoWrite;              // 深度設定（TestNoWrite＝半透明として奥から順に描く）
		BillboardMode billboard = BillboardMode::None;             // カメラの方を向かせる（Full / AxisY）
		Camera* camera = nullptr;                                  // 半透明を奥から並べるのに使う
	};

	// --- 頂点を調整できる板 ---
	struct Quad3dConfig : DrawTarget {
		Vector3 lb = {-0.5f, 0.0f, 0.5f};                          // 左下の座標
		Vector3 lt = {-0.5f, 0.0f, -0.5f};                         // 左上の座標
		Vector3 rb = {0.5f, 0.0f, 0.5f};                           // 右下の座標
		Vector3 rt = {0.5f, 0.0f, -0.5f};                          // 右上の座標
		Vector2 uvLb = {0.0f, 1.0f};                               // 左下のUV座標
		Vector2 uvLt = {0.0f, 0.0f};                               // 左上のUV座標
		Vector2 uvRb = {1.0f, 1.0f};                               // 右下のUV座標
		Vector2 uvRt = {1.0f, 0.0f};                               // 右上のUV座標
		Vector3 rotate = {0.0f, 0.0f, 0.0f};                       // 回転
		Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};                               // 色
		float alphaCutoff = 0.0f;                                  // これ以下のアルファのピクセルを捨てる
		Transform uvTransform;                                     // UVの拡縮、回転、移動
		uint32_t textureHandle = 0;                                // テクスチャハンドル
		ShadingType shadingType = ShadingType::Unlit;              // シェーディング設定
		BlendMode blendMode = BlendMode::Normal;                   // ブレンド設定
		RasterizerType rasterizerType = RasterizerType::SolidBack; // ラスタライザ設定
		DepthMode depthMode = DepthMode::TestWrite;                // 深度設定
		Camera* camera = nullptr;                                  // カメラ設定
		bool isBillboard = false;                                  // trueで常にカメラの方を向く（cameraが必要）
	};

	// --- 頂点を自分で並べる形（軌跡の帯など、毎フレーム形が変わる物）---
	// 頂点と番号は DrawMesh の引数で渡す（ここに持たせると、毎フレーム配列ごと写すことになるので）
	struct MeshConfig : DrawTarget {
		Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};                               // 色
		float alphaCutoff = 0.0f;                                  // これ以下のアルファのピクセルを捨てる
		Transform uvTransform;                                     // UVの拡縮、回転、移動
		uint32_t textureHandle = 0;                                // テクスチャハンドル
		ShadingType shadingType = ShadingType::Unlit;              // シェーディング設定
		BlendMode blendMode = BlendMode::Normal;                   // ブレンド設定
		RasterizerType rasterizerType = RasterizerType::SolidNone; // ラスタライザ設定（既定は両面。並べ方の向きを気にしなくてよいように）
		DepthMode depthMode = DepthMode::TestWrite;                // 深度設定
		const MaterialParams* material = nullptr;                  // 光沢・金属感・自己発光など（nullptr なら既定の値。切り抜きは alphaCutoff の方を使う）
		IBLEnvironment* env = nullptr;                             // 環境光（PBR に要る。無ければ PBR の代わりに Half Lambert で描く）
		Camera* camera = nullptr;                                  // カメラ設定
		bool receivesDecals = true;                                // false：デカールを受けない（不透明なときだけ効く）
		bool isBillboard = false;                                  // 使わない（頂点はワールド座標で渡す）。PushMesh が読むので置いてある
	};

	// --- AABBを生成 ---
	struct AABBConfig : DrawTarget {
		AABB aabb;                                                 // AABBの形状設定
		Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};                               // 色
		float alphaCutoff = 0.0f;                                  // これ以下のアルファのピクセルを捨てる
		Transform uvTransform;                                     // UVの拡縮、回転、移動
		uint32_t textureHandle = 0;                                // テクスチャハンドル
		ShadingType shadingType = ShadingType::Unlit;              // シェーディング設定
		BlendMode blendMode = BlendMode::Normal;                   // ブレンド設定
		RasterizerType rasterizerType = RasterizerType::SolidBack; // ラスタライザ設定
		DepthMode depthMode = DepthMode::TestWrite;                // 深度設定
		Camera* camera = nullptr;                                  // カメラ設定
		bool isBillboard = false;                                  // trueで常にカメラの方を向く（cameraが必要）
	};

	// --- OBBを生成 ---
	struct OBBConfig : DrawTarget {
		OBB obb;                                                   // OBBの形状設定
		Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};                               // 色
		float alphaCutoff = 0.0f;                                  // これ以下のアルファのピクセルを捨てる
		Transform uvTransform;                                     // UVの拡縮、回転、移動
		uint32_t textureHandle = 0;                                // テクスチャハンドル
		ShadingType shadingType = ShadingType::Unlit;              // シェーディング設定
		BlendMode blendMode = BlendMode::Normal;                   // ブレンド設定
		RasterizerType rasterizerType = RasterizerType::SolidBack; // ラスタライザ設定
		DepthMode depthMode = DepthMode::TestWrite;                // 深度設定
		const MaterialParams* material = nullptr;                  // 光沢・金属感・自己発光など（nullptr なら既定の値。切り抜きは alphaCutoff の方を使う）
		IBLEnvironment* env = nullptr;                             // 環境光（PBR に要る。無ければ PBR の代わりに Half Lambert で描く）
		Camera* camera = nullptr;                                  // カメラ設定
		bool receivesDecals = true;                                // false：デカールを受けない（不透明なときだけ効く）
		bool isBillboard = false;                                  // trueで常にカメラの方を向く（cameraが必要）
	};

	//=============================================================================
	// Particle
	//=============================================================================
	// --- パーティクルの描画設定 ---
	struct ParticleConfig : DrawTarget {
		const std::list<Particle>* particles = nullptr; // シミュレーション結果（ParticleManagerが持つ）
		uint32_t textureHandle = 0;                     // グループ共通テクスチャ（エミッター単位）
		Vector4 color = {1.0f,1.0f,1.0f,1.0f};          // グループ全体にかける色
		Transform uvTransform;                          // UVアニメ用
		BlendMode blendMode = BlendMode::Add;           // グループ単位（ドローコール単位なので粒ごとは不可能）
		Camera* camera = nullptr;                       // カメラ
		bool isBillboard = false;                       // trueで常にカメラの方を向く（cameraが必要）
	};

	// --- 粒の配列をそのまま描く設定（ParticleSystem が使う。ParticleConfig も中でこれに直して描く）---
	// 粒は VS でカメラの方へ向けるので、ここで向きは計算しない（world から読むのは、中心と大きさだけ。画面の上の回転は direction・rotation）
	struct ParticleInstancesConfig : DrawTarget {
		std::span<const ParticleData> instances;  // 粒ごとの行列・色・向き・角度（色は sRGB の見た目の値。中でリニアに直す）。DrawParticleInstances の中で写すので、呼んだ後は消してよい
		uint32_t textureHandle = 0;               // 粒の絵（0 なら白）
		bool isSoftCircle = false;                // 絵の代わりに丸いぼかしで描く（textureHandle は使わない）
		bool useFog = true;                       // 霧（Fog）を混ぜる
		bool useDepthOfField = false;             // 被写界深度でぼかす（DOF の前に描く。ぼけ方は粒の後ろにある物の距離で決まる）
		Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f}; // 全部の粒に掛ける色（sRGB）
		Transform uvTransform;                    // UVの拡縮、回転、移動
		BlendMode blendMode = BlendMode::Add;     // 重ね方（1回の描画で1つなので、粒ごとには変えられない）
	};

	//=============================================================================
	// Sprite
	//=============================================================================
	// --- スプライトの描画設定 ---
	// 座標系は正規化座標。画面中央が原点、右がX+、上がY+、画面端が±1。
	// 例: position={0,0}で画面中央、{-1,1}で左上、{1,-1}で右下
	struct SpriteConfig : Draw2dTarget {
	public:
		Vector2 position = {0.0f, 0.0f};                           // 中心座標（画面中央が原点、端が±1）
		Vector2 scale = {1.0f, 1.0f};                              // 拡大率（1.0でテクスチャ原寸）
		float rotate = 0.0f;                                       // 回転（ラジアン、反時計回り）
		Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};                               // 色
		Transform uvTransform;                                     // UVの拡縮、回転、移動
		uint32_t textureHandle = 0;                                // テクスチャハンドル
		BlendMode blendMode = BlendMode::None;                     // ブレンド設定
		RasterizerType rasterizerType = RasterizerType::SolidBack; // ラスタライザ設定
		Vector2 pivot = {0.5f, 0.5f};                              // 矩形のどこをpositionに合わせるか。基本触らない！（0,0=左上 1,1=右下）
	};

	// --- 頂点を調整できるスプライト ---
	struct Quad2dConfig : Draw2dTarget {
		Vector2 lb = {-0.1f, -0.1f};                               // 左下の座標
		Vector2 lt = {-0.1f, +0.1f};                               // 左上の座標
		Vector2 rb = {+0.1f, -0.1f};                               // 右下の座標
		Vector2 rt = {+0.1f, +0.1f};                               // 右上の座標
		float rotate = 0.0f;                                       // 回転（ラジアン、反時計回り）
		Vector2 uvLb = {0.0f, 1.0f};                               // 左下のUV座標
		Vector2 uvLt = {0.0f, 0.0f};                               // 左上ののUV座標
		Vector2 uvRb = {1.0f, 1.0f};                               // 右下のUV座標
		Vector2 uvRt = {1.0f, 0.0f};                               // 右上のUV座標
		Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};                               // 色
		Transform uvTransform;                                     // UVの拡縮、回転、移動
		uint32_t textureHandle = 0;                                // テクスチャハンドル
		BlendMode blendMode = BlendMode::None;                     // ブレンド設定
		RasterizerType rasterizerType = RasterizerType::SolidBack; // ラスタライザ設定
	};

	//=============================================================================
	// Text
	//=============================================================================
	// --- 画面に重ねる文字（文字の並べ方は TextRenderSystem が決めて、頂点で渡す）---
	struct Text2dConfig : Draw2dTarget {
		Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};        // 文字の色
		Vector4 outlineColor = {0.0f, 0.0f, 0.0f, 1.0f}; // 縁取りの色
		float outlineWidth = 0.0f;                       // 縁取りの太さ（SDF の値で 0〜0.5。0 なら縁取りなし）
		Vector4 glowColor = {1.0f, 1.0f, 1.0f, 1.0f};    // 光（グロー）の色
		float glowWidth = 0.0f;                          // 光の広さ（SDF の値で。縁取りの外側へ。0 なら無し）
		Vector4 shineColor = {1.0f, 1.0f, 1.0f, 1.0f};   // 斜めに通る光の色
		float shinePosition = 0.0f;                      // 斜めの光の帯の真ん中（文字のかたまりの左が 0、右が 1）
		float shineWidth = 0.0f;                         // 帯の半分の幅（かたまりの幅に対して。0 なら無し）
		uint32_t fontTextureIndex = 0;                   // フォントのアトラス（FontManager::GetAtlasTextureIndex）
		BlendMode blendMode = BlendMode::Normal;         // ブレンド設定
	};

	//=============================================================================
	// VectorShape
	//=============================================================================
	// --- 画像を使わない UI の図形（値を決めるのは VectorShapeSystem。形はシェーダーが計算する）---
	struct VectorShapeConfig : Draw2dTarget {
		Vector2 lb;                              // 板の左下の座標（正規化座標）
		Vector2 lt;                              // 板の左上の座標
		Vector2 rb;                              // 板の右下の座標
		Vector2 rt;                              // 板の右上の座標
		Vector2 localHalf;                       // 板の半分の大きさ（描かれるピクセル。シェーダーが読む位置の範囲）
		VectorShapeMaterialData material;        // 図形の値（色は sRGB。中でリニアに直す）
		BlendMode blendMode = BlendMode::Normal; // ブレンド設定
	};

	//=============================================================================
	// Polyline
	//=============================================================================
	// --- UI の折れ線（メッシュを作って持つのは PolylineSystem。ここでは共有して受け取るだけで、写さない）---
	struct PolylineConfig : Draw2dTarget {
		std::shared_ptr<const PolylineMeshData> mesh; // メッシュ（位置は折れ線の Entity から見たピクセル）
		uint32_t indexStart = 0;                      // 描く三角形の番号の始まり
		uint32_t indexCount = 0;                      // 描く三角形の番号の数
		PolylineMaterialData material;                // 色・太さ・画面へ写す行列（色は sRGB。中でリニアに直す）
		BlendMode blendMode = BlendMode::Normal;      // ブレンド設定
	};

	//=============================================================================
	// Line
	//=============================================================================
	// --- 3Dライン1本の定義 ---
	struct LineSegment {
		Vector3 start;               // スタート座標
		Vector3 end;                 // 終了座標
		Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f}; // 色
	};

	// --- 3Dライン群の描画設定(複数の線を1リクエストでまとめて描く) ---
	struct LineListConfig : DrawTarget {
		std::vector<LineSegment> lines;  // 座標（ワールド座標）
		float fadeStartDistance = 30.0f; // フェード開始距離
		float fadeEndDistance = 50.0f;   // フェード終了距離(この距離でalpha=0)
		Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};     // 色
		Camera* camera = nullptr;        // カメラ設定
		DepthMode depthMode = DepthMode::TestWrite; // Disable にすると、物の後ろ・中でも見える（骨のギズモなど）
	};

	//=============================================================================
	// Decal
	//=============================================================================
	// --- デカール（箱の中にある不透明な物に、マテリアルで塗る）---
	// 箱は大きさ 1 で 1辺 1（worldMatrix の大きさが箱の大きさ）。上（+Y）から下へ貼る。箱の XZ がテクスチャの UV
	struct DecalConfig : DrawTarget {
		uint32_t material = 0;                     // マテリアル（MaterialManager::Load の番号。Shader の #PROGRAM の category が Decal の物）
		float progress = 0.0f;                     // マテリアルの Curve の横軸（0〜1。描き手が決める）
		Matrix4x4 worldMatrix = MakeIdentity4x4(); // 箱のワールド行列
		int order = 0;                             // 重なったときの順（小さい方が先＝下）
	};

	// コピー・ムーブ禁止
	Renderer(const Renderer&) = delete;
	Renderer& operator=(const Renderer&) = delete;

	// 初期化
	static void Initialize();
	// 解放
	static void Release();

	/// <summary>
	/// モデルの描画リクエストを追加する
	/// </summary>
	static void DrawModel(const ModelConfig& config);

	/// <summary>
	/// パーティクルの描画リクエストを追加
	/// </summary>
	/// <param name="config"></param>
	static void DrawParticle(const ParticleConfig& config);

	/// <summary>
	/// 粒の配列を、そのまま1回のインスタンス描画で描くリクエストを追加する（空なら何もしない）
	/// </summary>
	static void DrawParticleInstances(const ParticleInstancesConfig& config);

	/// <summary>
	/// 3D三角形の描画リクエストを追加する
	/// <para>カメラを設定すると透視投影が適用される</para>
	/// </summary>
	static void DrawTriangle(const TriangleConfig& config);

	/// <summary>
	/// 2d矩形の描画リクエストを追加する
	/// 板のようなものを生成する
	/// </summary>
	static void DrawSprite(const SpriteConfig& config);

	/// <summary>
	/// 3D空間に置く画像の板の描画リクエストを追加する（Sprite Renderer が使う）
	/// <para>大きさ・ずらし・画像の切り出しを決められる板。ビルボードは Y 軸だけ（AxisY）もできる</para>
	/// </summary>
	static void DrawSprite3d(const Sprite3dConfig& config);

	/// <summary>
	/// 3d矩形の描画リクエストを追加する
	/// 板のようなものを生成する
	/// </summary>
	static void DrawRect3d(const Rect3dConfig& config);

	/// <summary>
	/// 2d矩形の描画リクエストを追加する
	/// 入力した4頂点を繋げたものを生成する
	/// </summary>
	static void DrawQuad2d(const Quad2dConfig& config);

	/// <summary>
	/// 画面に重ねる文字の描画リクエストを追加する（何文字あっても1回の描画）
	/// </summary>
	/// <param name="vertices">1文字につき4つ（左下・左上・右下・右上の順。位置は正規化座標、UV はアトラスの場所）。中で動かすので std::move で渡すと写さずに済む</param>
	static void DrawText2d(const Text2dConfig& config, std::vector<Vertex2dData> vertices);

	/// <summary>
	/// 画像を使わない UI の図形の描画リクエストを追加する
	/// </summary>
	static void DrawVectorShape(const VectorShapeConfig& config);

	/// <summary>
	/// UI の折れ線の描画リクエストを追加する
	/// </summary>
	static void DrawPolyline(const PolylineConfig& config);

	/// <summary>
	/// 3d矩形の描画リクエストを追加する
	/// 入力した4頂点を繋げたものを生成する
	/// </summary>
	static void DrawQuad3d(const Quad3dConfig& config);

	/// <summary>
	/// 頂点と番号を自分で並べた形の描画リクエストを追加する（1回の描画で出る）
	/// <para>頂点はワールド座標。番号は3つで三角形1つ（0, 1, 2, ...）</para>
	/// </summary>
	/// <param name="vertices">頂点（位置の w は 1）。中で動かすので、使い終わった配列を std::move で渡すと写さずに済む</param>
	/// <param name="indices">三角形の頂点の番号</param>
	static void DrawMesh(const MeshConfig& config, std::vector<Vertex3dData> vertices, std::vector<uint32_t> indices);

	/// <summary>
	/// 3D球の描画リクエストを追加する
	/// </summary>
	static void DrawSphere(const SphereConfig& config);

	/// <summary>
	/// AABBの描画リクエストを追加する
	/// </summary>
	static void DrawAABB(const AABBConfig& config);

	/// <summary>
	/// OBBの描画リクエストを追加する
	/// </summary>
	static void DrawOBB(const OBBConfig& config);

	/// <summary>
	/// 3Dライン群の描画リクエスト(複数の線を1ドローコールで描く)
	/// </summary>
	static void DrawLines(const LineListConfig& config);

	/// <summary>
	/// デカールを描く（不透明な物の後に、その深度の所へ塗る）
	/// <para>drawData は描き手の値（Shader の gDrawData と同じ並びの構造体のバイト）。Shader が gDrawData を読むなら、大きさが合わないと止める</para>
	/// <para>マテリアルが無い・Shader が Decal の物でないときは描かない（警告は1回だけ）</para>
	/// </summary>
	static void DrawDecal(const DecalConfig& config, std::span<const std::byte> drawData = {});

	/// <summary>
	/// このフレームで使うライトを設定する（LightSystemが1フレームに1回呼ぶ）
	/// <para>Unlit以外の描画は、すべてこのライトで照らされる</para>
	/// </summary>
	static void SetFrameLights(const DirectionalLightData& directionalLight, const PointLightListData& pointLights, const SpotLightListData& spotLights);


private:
	~Renderer() = default;
	// シングルトン
	friend struct std::default_delete<Renderer>;
	static std::unique_ptr<Renderer> instance_;

	// 球のジオメトリキャッシュ
	struct SphereGeometry {
		std::vector<Vertex3dData> vertices;
		std::vector<uint32_t> indices;
	};
	std::unordered_map<int, SphereGeometry> sphereGeometryCache_;

	// メッシュの共通作成部分（カメラの方を向かせるかは config.isBillboard で決める）
	template<class TConfig> static void PushMesh(const TConfig& config, std::vector<Vertex3dData>&& vertices, std::vector<uint32_t>&& indices, const Matrix4x4& worldMatrix);
	// 同じ物で、カメラの方を向かせ方を billboard で渡す（Y 軸だけ回す AxisY も使える）
	template<class TConfig> static void PushMesh(const TConfig& config, std::vector<Vertex3dData>&& vertices, std::vector<uint32_t>&& indices, const Matrix4x4& worldMatrix, BillboardMode billboard);

	template<class TConfig> static void PushSprite(const TConfig& config, Vector2 lb, Vector2 lt, Vector2 rb, Vector2 rt, 
		Vector2 uvLb, Vector2 uvLt, Vector2 uvRb, Vector2 uvRt);

	// ===== マテリアル1つぶんの値 =====
	// 読み込んだマテリアル・Inspector の上書き・Primitive のどこから来ても、この形（色はリニア）にそろえてから GPU の形に詰める
	// 既定の値は「マテリアルを渡されなかった形（Triangle・Plane など）」の見た目（光沢・環境光なし）
	struct MaterialValues {
		Vector4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f}; // マテリアルの色（リニア。A は不透明度）
		Vector3 ambient = {0.0f, 0.0f, 0.0f};         // Ka: 環境光色
		Vector3 specular = {0.0f, 0.0f, 0.0f};        // Ks: 鏡面反射色
		Vector3 emissive = {0.0f, 0.0f, 0.0f};        // Ke: 自己発光色
		float shininess = 1.0f;                       // Ns: 鏡面反射指数
		float metallic = 0.0f;                        // 0=非金属 1=金属
		float roughness = 0.5f;                       // 表面の粗さ
		uint32_t baseColorTexture = 0;                // 物の色のテクスチャ（TextureManager の番号。0 は白）
	};

	// Inspector の値（MaterialParams。色は sRGB）→ MaterialValues（色はリニア）。テクスチャは持たないので 0
	static MaterialValues ToMaterialValues(const MaterialParams& params);

	// モデルのマテリアルの枠1つ（= メッシュが使うマテリアルの番号）に使うマテリアルを決める
	// 今は「Override なら Inspector の値、でなければ読み込んだ値」。Model Renderer の Texture を選んでいれば、テクスチャだけ差し替える
	// 後でマテリアルの枠（.material）を作るときは、ここに「枠に .material があればそれ」を足す
	static MaterialValues ResolveModelMaterial(const ModelConfig& config, uint32_t materialIndex);

	// MaterialValues と物ごとの色（tint。リニア）を、GPU の定数バッファの形に詰める
	static Material3dData MakeMaterial3dData(const MaterialValues& values, const Vector4& tint, const Transform& uvTransform, float alphaCutoff);

	// 重心を操作
	static Vector2 RotateAround2d(const Vector2& point, const Vector2& center, float radian);
	static Vector3 RotateAround3d(const Vector3& point, const Vector3& center, const Vector3& rotation);

	/// <summary>
	/// 4頂点の四角形から表面法線を計算する
	/// </summary>
	static Vector3 CalcQuadNormal(const Vector3& lb, const Vector3& rb, const Vector3& lt);

	/// <summary>
	/// 8頂点のボックスをModelDescの頂点・インデックスに変換する（AABB/OBB共通）
	/// <para>面ごとに外向き法線を計算し、裏面カリングと整合する巻き順で6面を生成する</para>
	/// </summary>
	/// <param name="corners">ボックスの8頂点（ビット: bit0=X,bit1=Y,bit2=Z / 0=min,1=max）</param>
	/// <param name="center">ボックス中心（面の外向き判定に使用）</param>
	static void MakeBoxGeometry(const std::array<Vector3, 8>& corners, const Vector3& center, std::vector<Vertex3dData>& outVertices, std::vector<uint32_t>& outIndices);

	/// <summary>
	/// 球のジオメトリを生成する（頂点・インデックス）
	/// subdivision: 経線・緯線の分割数
	/// </summary>
	static SphereGeometry GenerateSphereGeometry(int subdivision);
};