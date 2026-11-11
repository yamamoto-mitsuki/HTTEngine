#pragma once
#include <array>
#include <cstddef>
#include <memory>
#include <vector>

#include "HTTEngine/Graphics/Pipeline/VertexFormat.h"
#include "HTTEngine/Graphics/Pipeline/RenderStates.h"
#include "HTTEngine/Graphics/Pipeline/ShaderConstants.h"

// どのリクエストも DrawTarget（どのウィンドウ・どの層・目で隠したか）を受け継ぐ。Renderer が Config から写す
// 2D のリクエストは Draw2dTarget（重ねる順の drawKey・マスクのステンシル）を受け継ぐ。RenderQueue が drawKey の順に並べて描く

// ===== 3Dメッシュ =====
struct MeshRequest : DrawTarget {
	// --- ソートキーの材料になるもの ---
	DrawCategory category = DrawCategory::Model;               // Model（スキンのあるモデルも、CS で曲げた頂点を Model として描く）
	ShadingType shadingType = ShadingType::Unlit;              // シェーディング設定
	BlendMode blendMode = BlendMode::Normal;                   // ブレンド設定
	RasterizerType rasterizerType = RasterizerType::SolidBack; // ラスタライザ設定
	DepthMode depthMode = DepthMode::TestWrite;                // 深度設定
	bool receivesDecals = true;                                // false：デカールを受けない（不透明な物だけ。ステンシルに印を付ける）
	StencilMode stencilMode = StencilMode::None;               // ステンシル（RenderQueue::Request が receivesDecals から決める）
	float cameraDistanceSq = 0.0f;                             // カメラとの距離
	uint64_t sortKey;                                          // ソートキー
	
	// --- ジオメトリ ---
	// 動的（Primitive系）: vertices, indicesを使用。DrawMeshがリングバッファに書く
	std::vector<Vertex3dData> vertices; // ピクセルを結ぶ頂点
	std::vector<uint32_t> indices;      // 頂点インデックスの格納場所
	// 静的（Model）: GPU常駐バッファの場所、個数だけ運ぶ。DrawMeshはバインドのみ
	D3D12_VERTEX_BUFFER_VIEW vbv{};     // VertexBufferView
	D3D12_INDEX_BUFFER_VIEW ibv{};      // IndexBufferView
	uint32_t indexCount = 0;            // DrawIndexedInstancedに渡す個数（静的用。動的はindices.size()）
	bool isStatic = false;              // DrawMeshの経路分岐スイッチ
	
	// --- ShaderとのBind情報として使う構造体 ---
	ObjectTransformData objectTransformData;
	Material3dData materialData;
	CameraData cameraData;
	D3D12_GPU_VIRTUAL_ADDRESS iblParamsAddress = 0; 

	// --- その他 ---
	bool isBillboard = false; // ビルボード（Shaderで計算）
	const std::string* debugName = nullptr;    // PIX表示用（ModelAssetの文字列を指す。所有しない）
	const std::string* debugSubName = nullptr; // サブメッシュのマテリアル名
};


// ===== デカール（箱の中にある、不透明な物の深度の所に塗る）=====
// Shader・状態・gMaterial の中身は、マテリアルから作る（Renderer::DrawDecal が MaterialManager::Resolve で作る）
struct DecalRequest : DrawTarget {
	// --- 描き方（マテリアルの Shader と、描くときの状態）---
	uint32_t programId = 0;                                     // 描画プログラムの番号（category が Decal の Shader）
	BlendMode blendMode = BlendMode::Normal;                    // ブレンド設定
	RasterizerType rasterizerType = RasterizerType::SolidFront; // ラスタライザ設定（箱の裏の面を描く。カメラが箱の中にあっても塗れる）
	DepthMode depthMode = DepthMode::Disable;                   // 深度設定（深度は読むだけの窓口で結ぶので、書く TestWrite は使えない）
	int order = 0;                                              // 重なったときの順（小さい方が先＝下）

	// --- 箱 ---
	Matrix4x4 worldMatrix = MakeIdentity4x4(); // 箱のワールド行列（大きさ 1 で 1辺 1）

	// --- Shaderとのバインド情報 ---
	std::vector<std::byte> materialConstants; // gMaterial の中身（マテリアルの値。空なら結ばない）
	std::vector<std::byte> drawData;          // gDrawData の中身（描き手の値。形など。描き手が無い物は空）
};


// ===== パーティクル =====
struct ParticleRequest : DrawTarget {
	std::vector<ParticleData> instances; // ビルボード計算済みのインスタンス配列
	MaterialParticleData materialData;
	BlendMode blendMode = BlendMode::Add;
	bool isBeforeDepthOfField = false; // true：被写界深度の前に描く（不透明な物と一緒にぼける）。false：後に描く（ぼけない）
	uint64_t sortKey = 0;
};


// ===== 2Dスプライト =====
struct SpriteRequest : Draw2dTarget {
	// --- ジオメトリ ---
	std::array<Vertex2dData, 4> vertices;

	// --- Shaderとのバインド情報 ---
	Material2dData materialData;

	// --- 描画設定 ---
	BlendMode blendMode = BlendMode::Normal;
	RasterizerType rasterizerType = RasterizerType::SolidBack;
};


// ===== 2Dの文字 =====
struct TextRequest : Draw2dTarget {
	// --- ジオメトリ（1文字につき4つ。左下・左上・右下・右上の順。位置は正規化座標）---
	std::vector<Vertex2dData> vertices;

	// --- Shaderとのバインド情報 ---
	TextMaterialData materialData;

	// --- 描画設定 ---
	BlendMode blendMode = BlendMode::Normal;
};


// ===== 2Dの図形（画像を使わないUIの図形）=====
struct VectorShapeRequest : Draw2dTarget {
	// --- ジオメトリ（左下・左上・右下・右上の順。位置は正規化座標、texcoord は図形の真ん中を原点にしたピクセル）---
	std::array<Vertex2dData, 4> vertices;

	// --- Shaderとのバインド情報 ---
	VectorShapeMaterialData materialData;

	// --- 描画設定 ---
	BlendMode blendMode = BlendMode::Normal;
};


// ===== 2Dの折れ線 =====
// 折れ線のメッシュ（PolylineSystem が作って持つ。描き終わるまでは RenderQueue も同じ物を指す）
struct PolylineMeshData {
	std::vector<VertexPolylineData> vertices; // 頂点（位置は折れ線の Entity から見たピクセル）
	std::vector<uint32_t> indices;            // 三角形の頂点の番号（3つで1つ）
};

struct PolylineRequest : Draw2dTarget {
	// --- ジオメトリ（毎フレーム写さずに、PolylineSystem のメッシュを共有する。indices のうち indexStart から indexCount 個を描く）---
	std::shared_ptr<const PolylineMeshData> mesh;
	uint32_t indexStart = 0;
	uint32_t indexCount = 0;

	// --- Shaderとのバインド情報 ---
	PolylineMaterialData materialData;

	// --- 描画設定 ---
	BlendMode blendMode = BlendMode::Normal;
};


// ===== Line =====
struct LineRequest : DrawTarget {
	// --- ジオメトリ ---
	std::vector<VertexLineData> vertices;

	// --- Shaderとのバインド情報 ---
	ObjectTransformData objectTransformData;
	MaterialLineData materialData;

	// --- 描画設定 ---
	DepthMode depthMode = DepthMode::TestWrite;
	bool isGizmo = false; // ギズモの線（ポストエフェクトの後に描く。RenderQueue::FlushGizmoLine）
};