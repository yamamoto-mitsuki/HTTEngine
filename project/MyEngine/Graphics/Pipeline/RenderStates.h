#pragma once
#include <array>
#include <string>
#include <compare>
#include <cstdint>

#include <d3d12.h>

#include "MyEngine/Math/MathIncludes.h"


// モデルマテリアルの調整用パラメータ（色は Inspector で選ぶ sRGB。Renderer がリニアに直す）
struct MaterialParams {
	Vector3 ambient = {0.2f, 0.2f, 0.2f};         // Ka: 環境光色
	Vector4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f}; // 物の色（A は不透明度）。Model Renderer の Override のときだけ、読み込んだ色の代わりに使う（Primitive は Color の方を使う）
	Vector3 specular = {0.0f, 0.0f, 0.0f};        // Ks: 鏡面反射色
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
	Polyline,    // UIの折れ線（メッシュで形を作り、縁はシェーダーでなめらかにする）
	Decal,       // 箱の中にある物（深度に描かれた不透明な物）に塗る。不透明な物の後に描く
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
	TestWrite,   // 深度テストあり・書き込み有: 不透明3D
	TestNoWrite, // 深度テストあり・書き込み無: 半透明3D
	Disable,     // 深度テストなし・書き込み無: 2D, UI, フルスクリーン
};

// ステンシルのビットの分け方（1つの値の中を、使い道ごとにビットで分ける。分けた所しか読まない・書かない）
inline constexpr uint8_t kStencilUIMaskBits = 0x7F;  // 下の 7 ビット：UI のマスク（外にあるマスクの数。入れ子は 8 段までなので足りる）
inline constexpr uint8_t kStencilNoDecalBit = 0x80;  // 一番上のビット：デカールを受けない物（Receive Decals を外した物）を描いた所

// ステンシルの使い方
// UI のマスク：値は「外にあるマスクの数」で、参照値と同じところだけ通す（kStencilUIMaskBits だけ使う）
// デカール：受けない物が kStencilNoDecalBit を立て、デカールはそのビットの無い所だけ塗る（参照値は kStencilNoDecalBit）
enum class StencilMode {
	None,        // 使わない（マスクの外の UI・3D）
	Test,        // 値が参照値と同じ所だけ描く（マスクの中の UI）
	Increment,   // 値が参照値と同じ所を 1 増やす（マスクの形を書く。入れ子なら、外のマスクの中だけが増える）
	Decrement,   // 値が参照値と同じ所を 1 減らす（子を描き終わったマスクの形を消す）
	MarkNoDecal, // 描いた所に kStencilNoDecalBit を立てる（デカールを受けない不透明な物）
	DecalTest,   // kStencilNoDecalBit の無い所だけ描く（デカール）
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
/// 2D を重ねる順の1段（Order → 種類 → Entity の番号の順に比べる。小さい方が奥）
/// </summary>
struct UIDrawKeyPart {
	int order = 0;     // Component の Order
	uint32_t kind = 0; // 同じ Order のときの順（Image → Vector Shape → Polyline → Text。UIDrawKind の番号）
	uint64_t id = 0;   // 同じ Order・種類なら、Entity の番号の小さい方（先に作った物）が奥

	// <=> を = default にすると、メンバーを上から順に比べる比較（<・== など）をコンパイラが作る
	auto operator<=>(const UIDrawKeyPart&) const = default;
};

/// <summary>
/// 2D の描く順。マスクの入れ子の数だけ段（parts）を持つ（外のマスクから順に並べ、最後が自分）
/// <para>マスクの子は、マスクの段を頭に持つので、マスクの「始まり」と「終わり」の間に並ぶ（マスクの Order の所にまとまる）</para>
/// <para>マスクの外の物は、段が1つだけ（自分の Order）。今までの Order の並びと同じになる</para>
/// </summary>
struct UIDrawKey {
	static constexpr size_t kMaxDepth = 9;        // 段の数の上限（マスクの入れ子 8 段＋自分）
	std::array<UIDrawKeyPart, kMaxDepth> parts{}; // 段（使うのは先頭から depth 個）
	uint32_t depth = 1;                           // 使っている段の数
	int32_t phase = 0;                            // マスクの図形の段：-1 始まり（子より前）/ 0 ふつう / +1 終わり（子より後）
	uint32_t step = 0;                            // 同じキーの中の順（マスクの始まりは 塗り → 形を書く、終わりは 形を消す → 縁取り）

	// Order だけの順（マスクを使わない物。シーンを切り替える幕など）
	static UIDrawKey FromOrder(int order) {
		UIDrawKey key;
		key.parts[0].order = order;
		return key;
	}
	// このキーの物が、other より先に（奥に）描かれるか
	bool operator<(const UIDrawKey& other) const;
};

// 2D の図形・折れ線のどこを描くか（VectorShape.PS・Polyline.PS の drawPart と同じ番号）
enum class UIDrawPart : uint32_t {
	All,        // 塗りと線の全部（ふつう）
	Fill,       // 塗りだけ（マスクの図形を、子より奥に描く）
	Stroke,     // 線だけ（マスクの図形の縁取りを、子より手前に描く）
	MaskRegion, // 色は出さず、形の内側だけを残す（マスクの形をステンシルに書く・消す）
};

/// <summary>
/// 2D の描き分け（DrawTarget に、重ねる順とマスクのステンシルを足した物）。2D の Config とリクエストが受け継ぐ
/// </summary>
struct Draw2dTarget : DrawTarget {
	UIDrawKey drawKey;                       // 重ねる順（RenderQueue が、この順に並べて描く）
	StencilMode stencil = StencilMode::None; // ステンシルの使い方
	uint32_t stencilRef = 0;                 // ステンシルの参照値（外にあるマスクの数）
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
	/// 引数にいれた DepthMode・StencilMode の設定を返す
	/// </summary>
	static D3D12_DEPTH_STENCIL_DESC MakeDepthStencilDesc(DepthMode mode, StencilMode stencil = StencilMode::None);
};
