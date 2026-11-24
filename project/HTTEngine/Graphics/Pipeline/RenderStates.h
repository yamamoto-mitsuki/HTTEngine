#pragma once
#include <array>
#include <string>
#include <compare>
#include <cstdint>

#include <d3d12.h>

#include "HTTEngine/Core/EnumTips.h"
#include "HTTEngine/Math/MathIncludes.h"


// ===== モデルマテリアルの調整用パラメータ（色はInspectorで選ぶsRGB。Rendererがリニアに直す）=====
struct MaterialParams {
	Vector3 ambient = {0.2f, 0.2f, 0.2f};         // Ka: 環境光色
	Vector4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f}; // 物の色。ModelRendererのOverrideのときだけ、読み込んだ色の代わりに使う（Primitive は Color の方を使う）
	Vector3 specular = {0.0f, 0.0f, 0.0f};        // Ks: 鏡面反射色
	Vector3 emissive = {0.0f, 0.0f, 0.0f};        // Ke: 自己発光色
	float emissiveIntensity = 1.0f;               // 自己発光の強さ（emissive に掛ける。1 より大きいと HDR の明るさになり、ブルームで光る）
	float shininess = 32.0f;                      // Ns: 鏡面反射指数
	float metallic = 0.0f;                        // 0=非金属 1=金属
	float roughness = 0.5f;                       // 0=鏡面 1=完全拡散
	float alphaCutoff = 0.0f;                     // これ以下のアルファのピクセルを捨てる
};

// ===== 描画したい形 =====
enum class DrawCategory {
	Sprite,
	Model,
	Particle,
	Line,
	Text,
	VectorShape, // 画像を使わないUIの図形
	Polyline,    // UIの折れ線（メッシュで形を作り、縁はシェーダーでなめらかにする）
	Decal,       // 箱の中にある物（深度に描かれた不透明な物）に塗る。不透明な物の後に描く
	GizmoLine,   // ギズモの線。ポストエフェクトの後の絵（SDR）に描く。物の後ろかは、3D の絵の深度をシェーダーで読んで比べる
};

// ===== サンプリング設定 =====
enum class SamplingType { 
	LinearWrap,
	LinearClamp,
	PointClamp,
	ShadowMap,
};

// ===== シェーディング設定 =====
enum class ShadingType {
	Unlit,       // Lightingなし
	Lambert,     // Lambert
	HalfLambert, // Half Lambert
	Phong,       // PhongReflection
	BlinnPhong,  // BlinnPhongReflection
	PBR,         // Cook-Torrance（GGX）物理ベース
};
// 選択肢ごとの説明
constexpr auto EnumTips(ShadingType) {
	using enum ShadingType;
	return MakeEnumTips<ShadingType>({
	    {Unlit, "光を使わず、色そのまま"},
	    {Lambert, "陰だけ（光の向きで暗くなる）"},
	    {HalfLambert, "陰だけ（Lambert より、暗い側がやわらかい）"},
	    {Phong, "陰とハイライト（Specular・Shininess）"},
	    {BlinnPhong, "陰とハイライト（Phong より、ハイライトの広がりが自然）"},
	    {PBR, "物理ベース（Metallic・Roughness で、金属らしさ・ざらざら具合を決める）"},
	});
}

// ===== ブレンド設定 =====
enum class BlendMode {
	None,     // ブレンド無し
	Normal,   // 通常aブレンド（デフォルト）: Src * SrcA + Dest * (1 - SrcA)
	Add,      // 加算: Src * SrcA + Dest * 1
	Subtract, // 減算: Dest * 1 - Src * SrcA
	Multiply, // 乗算: Src * 0 + Dest * Src
	Screen,   // スクリーン: Src * (1 - Dest) + Dest * 1
};
// 選択肢ごとの説明
constexpr auto EnumTips(BlendMode) {
	using enum BlendMode;
	return MakeEnumTips<BlendMode>({
	    {None, "重ねない（そのまま上書きする。UI の図形は縁のなめらかさが無くなる）"},
	    {Normal, "普通（透明な所が透ける）"},
	    {Add, "足して光る（重なるほど明るい）"},
	    {Subtract, "引いて暗くする"},
	    {Multiply, "掛けて暗くする（影・色の付いたガラス）"},
	    {Screen, "明るくする（Add より白く飛びにくい）"},
	});
}

// ===== ラスタライザ設定 =====
enum RasterizerType {
	SolidBack,  // 通常描画: 背面カリング + 塗りつぶし
	SolidNone,  // 両面描画: カリングなし
	SolidFront, // シャドウマップ用
	Wireframe,  // デバック用: ワイヤー（塗りつぶさない）
};
// 選択肢ごとの説明
constexpr auto EnumTips(RasterizerType) {
	using enum RasterizerType;
	return MakeEnumTips<RasterizerType>({
	    {SolidBack, "普通（裏の面は描かない）"},
	    {SolidNone, "両面を描く"},
	    {SolidFront, "表の面を描かない（影の絵を作る用）"},
	    {Wireframe, "線だけ（形を確かめる用）"},
	});
}

// ===== 深度設定 =====
enum DepthMode {
	TestWrite,   // 深度テストあり・書き込み有: 不透明3D
	TestNoWrite, // 深度テストあり・書き込み無: 半透明3D
	Disable,     // 深度テストなし・書き込み無: 2D, UI, フルスクリーン
};
// 選択肢ごとの説明
constexpr auto EnumTips(DepthMode) {
	using enum DepthMode;
	return MakeEnumTips<DepthMode>({
	    {TestWrite, "奥行きを比べて、書く（不透明な 3D）"},
	    {TestNoWrite, "奥行きを比べるが、書かない（半透明な 3D）"},
	    {Disable, "奥行きを使わない（2D・UI・画面全体）"},
	});
}

// ===== ステンシルのビットの分け方 =====
inline constexpr uint8_t kStencilUIMaskBits = 0x7F;  // 下の7ビット：UI のマスク（外にあるマスクの数。入れ子は 8 段までなので足りる）
inline constexpr uint8_t kStencilNoDecalBit = 0x80;  // 一番上のビット：デカールを受けない物（Receive Decals を外した物）を描いた所

// ステンシルの使い方
// UI のマスク：値は「外にあるマスクの数」で、参照値と同じところだけ通す（kStencilUIMaskBits だけ使う）
enum class StencilMode {
	None,        // 使わない（マスクの外の UI・3D）
	Test,        // 値が参照値と同じ所だけ描く（マスクの中の UI）
	Increment,   // 値が参照値と同じ所を 1 増やす（マスクの形を書く。入れ子なら、外のマスクの中だけが増える）
	Decrement,   // 値が参照値と同じ所を 1 減らす（子を描き終わったマスクの形を消す）
	MarkNoDecal, // 描いた所に kStencilNoDecalBit を立てる（デカールを受けない不透明な物）
	DecalTest,   // kStencilNoDecalBit の無い所だけ描く（デカール）
};

// ===== 3Dのメッシュをどのパスで描くか（同じメッシュを、パスごとに違うPSOで何回か描く）=====
// 色を塗る前に深度だけのパスを2つ通す（影の深度・深度プリパス） 。どちらもRTVを結ばず、PSは切り抜きだけにする
enum class MeshPass {
	Color,        // 色を塗る（物のShader・Blend・深度・ステンシルのまま）
	DepthPrepass, // 深度プリパス: カメラから見た深度だけを書く（色を塗る前に一番手前の深度をそろえておく）
	ShadowCaster, // 影の深度: ライトから見た深度だけを書く（面の傾きに合わせて深度をずらす）
};

// ===== ビルボード（設定 =====
enum class BillboardMode {
	None,  // 向かせない（Transformの回転どおり）
	Full,  // カメラの向きに完全に合わせる（アイコン、エフェクト、名札）
	AxisY, // Y軸まわりだけ回す（木、キャラの立ち絵。カメラが上から見ても傾かない）
};
// 選択肢ごとの説明
constexpr auto EnumTips(BillboardMode) {
	using enum BillboardMode;
	return MakeEnumTips<BillboardMode>({
	    {None, "向かせない（Transform の回転のまま）"},
	    {Full, "いつもカメラの正面を向く（アイコン・エフェクト・名札）"},
	    {AxisY, "縦に立てたまま、Y 軸のまわりだけ回す（木・キャラの立ち絵。カメラが上から見ても傾かない）"},
	});
}

// ===== 描く物の層（ビューごとに、描く・描かないを選ぶ）=====
enum class DrawLayer {
	Default,       // ふつうの物
	EditorGrid,    // エディタのグリッド
	EditorGizmo,   // エディタのギズモ
	GameViewGuide, // エディタの線のうち、Game ビューだけに描く物
};


// ===== 描く物をどのウィンドウ・どのビューに描くか =====
struct DrawTarget {
	std::wstring windowTitle = L"";       // 描画したいウィンドウ名（空なら、どのウィンドウにも描く）
	DrawLayer layer = DrawLayer::Default; // 層
	bool isSceneHidden = false;           // Hierarchy の目で隠した物（Scene ビューだけ描かない。Game ビューにはいつも描く）
};

// ===== 2D を重ねる順の1段 =====
struct UIDrawKeyPart {
	int order = 0;     // ComponentのOrder
	uint32_t kind = 0; // 同じOrder のときの順
	uint64_t id = 0;   // 同じOrder・種類ならEntity の番号の小さい方（先に作った物）が奥
	auto operator<=>(const UIDrawKeyPart&) const = default;
};

// ===== 2D の描く順。マスクの入れ子の数だけ段を持つ =====
struct UIDrawKey {
	static constexpr size_t kMaxDepth = 9;        // 段の数の上限（マスクの入れ子 8 段＋自分）
	std::array<UIDrawKeyPart, kMaxDepth> parts{}; // 段（使うのは先頭から depth 個）
	uint32_t depth = 1;                           // 使っている段の数
	int32_t phase = 0;                            // マスクの図形の段：-1 始まり / 0 ふつう / +1 終わり
	uint32_t step = 0;                            // 同じキーの中の順（マスクの始まりは 塗り → 形を書く、終わりは 形を消す → 縁取り）

	// Order だけの順（マスクを使わない物。シーンを切り替える幕など）
	static UIDrawKey FromOrder(int order) {
		UIDrawKey key;
		key.parts[0].order = order;
		return key;
	}
	// このキーの物が、other より先に描かれるか
	bool operator<(const UIDrawKey& other) const;
};

// ===== 2Dの図形・折れ線のどこを描くか（VectorShape.PS・Polyline.PS の drawPart と同じ番号）=====
enum class UIDrawPart : uint32_t {
	All,        // 塗りと線の全部（ふつう）
	Fill,       // 塗りだけ（マスクの図形を、子より奥に描く）
	Stroke,     // 線だけ（マスクの図形の縁取りを、子より手前に描く）
	MaskRegion, // 色は出さず、形の内側だけを残す（マスクの形をステンシルに書く・消す）
};

/// ===== 2Dの描き分け（DrawTargetに、重ねる順とマスクのステンシルを足した物）=====
struct Draw2dTarget : DrawTarget {
	UIDrawKey drawKey;                       // 重ねる順
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
	/// 影の深度を書くときのRasterizerTypeの設定（MakeRasterizerDescに、面の傾きに合わせて深度をずらす量を足した物）
	/// <para>斜めの面は、影の深度の絵の1ピクセルの中の深度が大きく変わる。比べるときに自分の影が縞になる（シャドウアクネ）ので傾くほど奥へずらす</para>
	/// </summary>
	static D3D12_RASTERIZER_DESC MakeShadowCasterRasterizerDesc(RasterizerType type);

	/// <summary>
	/// 引数にいれた DepthMode・StencilMode の設定を返す
	/// </summary>
	static D3D12_DEPTH_STENCIL_DESC MakeDepthStencilDesc(DepthMode mode, StencilMode stencil = StencilMode::None);
};
