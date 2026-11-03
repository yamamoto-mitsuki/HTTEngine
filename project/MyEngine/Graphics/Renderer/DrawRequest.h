#pragma once
#include <array>
#include <memory>
#include <vector>

#include "MyEngine/Graphics/Pipeline/VertexFormat.h"
#include "MyEngine/Graphics/Pipeline/RenderStates.h"
#include "MyEngine/Graphics/Pipeline/ShaderConstants.h"

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
};