#pragma once
#include <string>

#include <d3d12.h>

#include "MyEngine/Math/MathIncludes.h"


// モデルマテリアルの調整用パラメータ
struct MaterialParams {
	Vector3 ambient = {0.2f, 0.2f, 0.2f};  // Ka: 環境光色
	Vector3 diffuse = {1.0f, 1.0f, 1.0f};  // Kd: 拡散反射色
	Vector3 specular = {0.0f, 0.0f, 0.0f}; // Ks: 鏡面反射色
	Vector3 emissive = {0.0f, 0.0f, 0.0f}; // Ke: 自己発光色
	float shininess = 32.0f;               // Ns: 鏡面反射指数
	float metallic = 0.0f;                 // 0=非金属 1=金属
	float roughness = 0.5f;                // 0=鏡面 1=完全拡散
	float alphaCutoff = 0.0f;              // これ以下のアルファのピクセルを捨てる（切り抜き。0で今まで通り）
};

// 描画したい形
enum class DrawCategory {
	Sprite, // Spriteは深度情報の関係で必ず最後に描画すること
	Model,
	Particle,
	Line,
	Text,
	VectorShape, // 画像を使わないUIの図形
};

// サンプリング設定
enum class SamplingType { 
	LinearWrap, // 通常描画
	LinearClamp,
	PointClamp,
	ShadowMap,
};

// シェーディング設定
enum class ShadingType {
	Unlit,       // Lightingなし
	Lambert,     // Lambert
	HalfLambert, // Half Lambert
	Phong,       // PhongReflection
	BlinnPhong,  // BlinnPhongReflection
	PBR,         // Cook-Torrance（GGX）物理ベース
};

// ブレンド設定
enum class BlendMode {
	None,     // ブレンド無し
	Normal,   // 通常aブレンド（デフォルト）: Src * SrcA + Dest * (1 - SrcA)
	Add,      // 加算: Src * SrcA + Dest * 1
	Subtract, // 減算: Dest * 1 - Src * SrcA
	Multiply, // 乗算: Src * 0 + Dest * Src
	Screen,   // スクリーン: Src * (1 - Dest) + Dest * 1
};

// ラスタライザ設定
enum RasterizerType {
	SolidBack,  // 通常描画:      背面カリング + 塗りつぶし
	SolidNone,  // 両面描画:      カリングなし
	SolidFront, // シャドウマップ用
	Wireframe,  // デバック用:    ワイヤー（塗りつぶさない）
};

// 深度設定
enum DepthMode {
	TestWrite,  // 深度テストあり・書き込み有: 不透明3D
	TestNoWrite, // 深度テストあり・書き込み無: 半透明3D
	Disable,     // 深度テストなし・書き込み無: 2D, UI, フルスクリーン
};

// ビルボード（カメラの方を向かせる）設定
enum class BillboardMode {
	None,  // 向かせない（Transformの回転どおり）
	Full,  // カメラの向きに完全に合わせる（アイコン、エフェクト、名札）
	AxisY, // Y軸まわりだけ回す（木、キャラの立ち絵。カメラが上から見ても傾かない）
};

// 描く物の層（ビューごとに、描く・描かないを選べる。Unity の Layer とカメラの Culling Mask の小さい版）
enum class DrawLayer {
	Default,     // ふつうの物（いつも描く）
	EditorGrid,  // エディタのグリッド（Scene・Game ビューで別々に切れる。View メニュー）
	EditorGizmo, // エディタのギズモ（Scene ビューだけ描く。カメラのアイコンなど、ゲームの絵に入れない物）
};

/// <summary>
/// 描く物を、どのウィンドウ・どのビューに描くか（Renderer の Config と、RenderQueue のリクエストが受け継ぐ）
/// <para>どの Config にも同じ値があるので、1つにまとめた。System は config の中のこの値を書き、Renderer はリクエストへまとめて写す</para>
/// <para>描くかどうかは RenderQueue::IsDrawnInView が、ビューを描くときに決める（積むのは1回、ビューごとに飛ばす）</para>
/// </summary>
struct DrawTarget {
	std::wstring windowTitle = L"";       // 描画したいウィンドウ名（空なら、どのウィンドウにも描く）
	DrawLayer layer = DrawLayer::Default; // 層（ビューごとに描く・描かないを選ぶ。RenderQueue::SetLayerVisible）
	bool isSceneHidden = false;           // Hierarchy の目で隠した物（Scene ビューだけ描かない。Game ビューにはいつも描く）
};


/// <summary>
/// 描画の際に調整したい設定の登録クラス
/// </summary>
class RenderStates {
public:

	/// <summary>
	/// 引数に入れた BlendMode の設定を返す
	/// </summary>
	static D3D12_BLEND_DESC MakeBlendDesc(BlendMode mode);

	/// <summary>
	/// 引数に入れた RasterizerTypeの設定を返す
	/// </summary>
	static D3D12_RASTERIZER_DESC MakeRasterizerDesc(RasterizerType type);

	/// <summary>
	/// 引数にいれた
	/// </summary>
	static D3D12_DEPTH_STENCIL_DESC MakeDepthStencilDesc(DepthMode mode);
};
