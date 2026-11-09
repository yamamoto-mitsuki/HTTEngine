#include "HTTEngine/Graphics/Renderer/Renderer.h"
#include <cmath>
#include <format>
#include <utility>
#include <algorithm>
#include <unordered_set>
#include <Windows.h>
#include "HTTEngine/Math/Color.h"
#include "HTTEngine/Diagnostics/HTTAssert.h"
#include "HTTEngine/Diagnostics/LogManager.h"
#include "HTTEngine/Camera/Camera.h"
#include "HTTEngine/Graphics/Pipeline/VertexFormat.h"
#include "HTTEngine/Graphics/Material/MaterialManager.h"
#include "HTTEngine/Graphics/Model/ModelManager.h"
#include "HTTEngine/Graphics/Pipeline/ShaderPackageLoader.h"
#include "HTTEngine/Graphics/Texture/TextureManager.h"
#include "HTTEngine/Graphics/Renderer/DrawRequest.h"
#include "HTTEngine/Graphics/Renderer/RenderQueue.h"
#include "HTTEngine/Graphics/Renderer/RenderContext.h"
#include "HTTEngine/Graphics/Renderer/SkinCache.h"
#include "HTTEngine/Graphics/RenderTarget/RenderWindow.h"
#include "HTTEngine/Math/MathUtility.h"

// 静的メンバ変数
std::unique_ptr<Renderer> Renderer::instance_ = nullptr;


//=============================================================================
// 共通作成部分
//=============================================================================
// ===== テクスチャ未指定(0)を白1x1に差し替える =====
static uint32_t ResolveTextureIndex(uint32_t textureHandle) { return (textureHandle != 0) ? textureHandle : TextureManager::GetWhiteTextureHandle(); }

// ===== カメラとの距離の2乗（半透明を奥から手前の順に並べるのに使う）=====
static float CameraDistanceSq(const Matrix4x4& worldMatrix, const Camera* camera) {
	const Vector3 worldPos = {worldMatrix.m[3][0], worldMatrix.m[3][1], worldMatrix.m[3][2]};
	const Vector3 cameraPos = camera ? camera->GetTranslation() : Vector3(0.0f, 0.0f, 0.0f);
	const Vector3 d = cameraPos - worldPos;
	return Dot(d, d);
}

// ===== Inspector の値 → マテリアル1つぶんの値 =====
Renderer::MaterialValues Renderer::ToMaterialValues(const MaterialParams& params) {
	// 色は Inspector で選んだ sRGB の色なので、リニアに直す（読み込んだマテリアルは、もうリニア）
	MaterialValues values;
	values.baseColor = SrgbToLinear(params.baseColor); // Base Color 物の色（A は不透明度。そのまま）
	values.ambient = SrgbToLinear(params.ambient);     // Ka: Ambient   環境光（影になっている部分の明るさ）
	values.specular = SrgbToLinear(params.specular);   // Ks: Specular  鏡面反射（ハイライトの強さ）
	values.emissive = SrgbToLinear(params.emissive);   // Ke: Emissive  自己発光（光源がなくても発光する色）
	values.shininess = params.shininess;               // Ns: Shininess 光沢（値が大きいほどハイライトが小さく鋭くなる）
	values.metallic = params.metallic;
	values.roughness = params.roughness;
	return values;
}

// ===== モデルのマテリアルの枠1つに使うマテリアルを決める =====
Renderer::MaterialValues Renderer::ResolveModelMaterial(const ModelConfig& config, uint32_t materialIndex) {
	const ModelManager::ImportedMaterial* imported = ModelManager::GetImportedMaterial(config.modelHandle, materialIndex);
	MaterialValues values;
	// --- どのマテリアルか（Override なら Inspector の値、でなければ読み込んだ値）---
	if (config.overrideMaterial || imported == nullptr) {
		values = ToMaterialValues(config.material);
	} else {
		values.baseColor = imported->baseColor; // 読み込みでリニアにそろえてある
		values.ambient = imported->ambient;
		values.specular = imported->specular;
		values.emissive = imported->emissive;
		values.shininess = imported->shininess;
		values.metallic = imported->metallic;
		values.roughness = imported->roughness;
	}
	// --- テクスチャ（Inspector の上書きにテクスチャは無いので、Override でも読み込んだ物を使う）---
	if (imported != nullptr) {
		values.baseColorTexture = imported->baseColorTextureHandle;
	}
	// Model Renderer の Texture を選んでいれば、全部の枠のテクスチャを差し替える（マテリアルの色はそのまま掛かる）
	if (config.textureHandle != 0) {
		values.baseColorTexture = config.textureHandle;
	}
	return values;
}

// ===== GPU の定数バッファの形に詰める =====
Material3dData Renderer::MakeMaterial3dData(const MaterialValues& values, const Vector4& tint, const Transform& uvTransform, float alphaCutoff) {
	Material3dData material;
	material.tint = tint;
	material.baseColor = values.baseColor;
	material.uvTransform = MakeUVTransformMatrix(uvTransform);
	material.ambient = values.ambient;
	material.alphaCutoff = alphaCutoff;
	material.specular = values.specular;
	material.shininess = values.shininess;
	material.emissive = values.emissive;
	material.textureIndex = ResolveTextureIndex(values.baseColorTexture);
	material.metallic = values.metallic;
	material.roughness = values.roughness;
	return material;
}

// ===== メッシュの共通作成部分 =====
template<class TConfig> 
void Renderer::PushMesh(const TConfig& config, std::vector<Vertex3dData>&& vertices, std::vector<uint32_t>&& indices, const Matrix4x4& worldMatrix) {
	PushMesh(config, std::move(vertices), std::move(indices), worldMatrix, config.isBillboard ? BillboardMode::Full : BillboardMode::None);
}

template<class TConfig> 
void Renderer::PushMesh(const TConfig& config, std::vector<Vertex3dData>&& vertices, std::vector<uint32_t>&& indices, 
	const Matrix4x4& worldMatrix, BillboardMode billboard) {
	// --- bind情報、頂点データ ---
	MeshRequest req;
	req.vertices = std::move(vertices);
	req.indices = std::move(indices);
	// --- マテリアル：Config が material を持っていて、何かを指していれば、その値（光沢・金属感・自己発光など）---
	MaterialValues values;
	if constexpr (requires { config.material; }) {
		if (config.material != nullptr) {
			values = ToMaterialValues(*config.material);
		}
	}
	// 形（Primitive など）には読み込んだマテリアルが無く、Config の Color と Texture がマテリアルの色とテクスチャ（sRGB → リニア）
	// そのため MaterialParams の Base Color は使わず、物ごとの色（tint）は白
	values.baseColor = SrgbToLinear(config.color);
	values.baseColorTexture = config.textureHandle;
	req.materialData = MakeMaterial3dData(values, {1.0f, 1.0f, 1.0f, 1.0f}, config.uvTransform, config.alphaCutoff); // 切り抜きは、どの Config にもある alphaCutoff の方を使う
	req.objectTransformData = MakeObjectTransform(worldMatrix, billboard);
	req.shadingType = config.shadingType;
	req.blendMode = config.blendMode;
	req.rasterizerType = config.rasterizerType;
	req.depthMode = config.depthMode;
	// --- デカールを受けるか：Config が receivesDecals を持っていれば、その値（持たない形は受ける）---
	if constexpr (requires { config.receivesDecals; }) {
		req.receivesDecals = config.receivesDecals;
	}
	static_cast<DrawTarget&>(req) = config; // 描き分け（ウィンドウ・層・目で隠したか）をまとめて写す（DrawTarget の所だけ代入する）
	// --- 環境光（IBL）：PBR に要る。Config が env を持っていれば、そのアドレス ---
	if constexpr (requires { config.env; }) {
		req.iblParamsAddress = (config.env != nullptr) ? config.env->GetParametersAddress() : 0;
	}
	// 環境光が無いまま PBR で描くと止まる（RenderContext の assert）ので、陰だけ付ける Half Lambert で描く
	if (req.shadingType == ShadingType::PBR && req.iblParamsAddress == 0) {
		req.shadingType = ShadingType::HalfLambert;
	}
	// 半透明のソートに使うカメラとの距離
	req.cameraDistanceSq = CameraDistanceSq(worldMatrix, config.camera);
	RenderQueue::Request(std::move(req));
}

// ===== スプライトの共通作成部分 =====
template<class TConfig>
void Renderer::PushSprite(const TConfig& config, Vector2 lb, Vector2 lt, Vector2 rb, Vector2 rt, 
	Vector2 uvLb, Vector2 uvLt, Vector2 uvRb, Vector2 uvRt) {
	// bind情報、頂点情報（色は sRGB → リニア。2D も画面へ書くときに sRGB へ戻る）
	SpriteRequest req;
	req.materialData.color = SrgbToLinear(config.color);
	req.materialData.uvTransform = MakeUVTransformMatrix(config.uvTransform);
	req.materialData.textureIndex = ResolveTextureIndex(config.textureHandle);
	req.blendMode = config.blendMode;
	req.rasterizerType = config.rasterizerType;
	req.vertices[0] = {
	    {lb.x, lb.y, 0.0f, 1.0f},
        uvLb
    };
	req.vertices[1] = {
	    {lt.x, lt.y, 0.0f, 1.0f},
        uvLt
    };
	req.vertices[2] = {
	    {rb.x, rb.y, 0.0f, 1.0f},
        uvRb
    };
	req.vertices[3] = {
	    {rt.x, rt.y, 0.0f, 1.0f},
        uvRt
    };
	static_cast<Draw2dTarget&>(req) = config; // 描き分け（ウィンドウ・層・目で隠したか・重ねる順・ステンシル）をまとめて写す
	RenderQueue::Request(std::move(req));
}


//=============================================================================
// 初期化
//=============================================================================
void Renderer::Initialize() {
	HTT_ASSERT_MSG(instance_ == nullptr, "Initialize()が2回以上呼び出されています。");
	instance_ = std::make_unique<Renderer>(ConstructorKey());

	LogManager::Log("Initialized");
}


//=============================================================================
// 解放
//=============================================================================
void Renderer::Release() {
	HTT_ASSERT_MSG(instance_ != nullptr, "Initialize()より先にRelease()が呼ばれています");
	instance_.reset();
	LogManager::Log("Released");
}


//=============================================================================
// ライト
//=============================================================================
void Renderer::SetFrameLights(const DirectionalLightData& directionalLight, const PointLightListData& pointLights, const SpotLightListData& spotLights) {
	// GPUへ書くのはRenderContextの役目なので、そのまま渡す
	RenderContext::SetFrameLights(directionalLight, pointLights, spotLights);
}


//=============================================================================
// 〇〇Config → 〇〇Request
//=============================================================================
// ===== Model =====
void Renderer::DrawModel(const ModelConfig& config) {
	// 参照するモデル
	const ModelManager::ModelAsset* asset = ModelManager::GetModelAsset(config.modelHandle);
	if (!asset) {
		LogManager::Warning("登録されていないModelHandleを参照");
		return;
	}
	// 行列
	Matrix4x4 worldMatrix = config.worldMatrix ? *config.worldMatrix : MakeAffineMatrix(config.transform.scale, config.transform.rotation, config.transform.translation);

	// ノードの行列: AnimationPlayerが作った物があればそれ、無ければモデルに入っている動かない行列
	const bool useNodeMatrices = config.nodeMatrices && config.nodeMatrices->size() == asset->nodes.size();
	// スキンのあるメッシュを CS で曲げるか：アニメーションのノードの行列があり、持ち主の Entity が分かるときだけ
	// Animator が無い・止めているときは、元の頂点がそのままバインドポーズ（モデルに入っている形）なので、曲げずに描ける
	const bool useSkinning = useNodeMatrices && config.entity.IsValid();
	// 行列パレットのGPUアドレス（モデル1体で1つ。最初のスキンのあるメッシュで作り、ほかのメッシュと共有する）
	D3D12_GPU_VIRTUAL_ADDRESS paletteAddress = 0;

	// ノードの階層をたどり、ノードが持つメッシュごとに1つのMeshRequest（ノードの行列を引くので番号で回す）
	for (size_t nodeIndex = 0; nodeIndex < asset->nodes.size(); ++nodeIndex) {
		const ModelManager::Node& node = asset->nodes[nodeIndex];
		const Matrix4x4& nodeMatrix = useNodeMatrices ? (*config.nodeMatrices)[nodeIndex] : node.worldMatrix;
		for (uint32_t meshIndex : node.meshIndices) {
			const ModelManager::SubMesh& mesh = asset->meshes[meshIndex];
			MeshRequest req;
			// 静的ジオメトリ：GPU常駐バッファを指すだけ
			req.isStatic = true;
			req.vbv = mesh.vbv;
			req.ibv = mesh.ibv;
			req.indexCount = mesh.indexCount;
			// スキンのあるメッシュ: CS で曲げた頂点（SkinnedVertex）を、ふつうの頂点バッファとして描く
			if (mesh.isSkinned && useSkinning) {
				if (paletteAddress == 0) {
					std::vector<WellForGPU> palette;
					ModelManager::MakeMatrixPalette(*asset, config.nodeMatrices, palette);
					paletteAddress = RenderContext::PushFrameArray(palette.data(), palette.size()); // このフレームだけ使う
				}
				// 曲げる仕事を積み、曲げた頂点の VBV に差し替える（Dispatch は描き始める前にまとめて）
				req.vbv = SkinCache::Request(config.entity, meshIndex, mesh, paletteAddress);
			}
			// マテリアル構築（枠＝メッシュが使うマテリアルの番号で決めて、物ごとの色（Color。sRGB → リニア）と一緒に詰める）
			// 切り抜きは読み込んだマテリアルに無い設定なので、いつも Inspector の値（MaterialParams）から取る
			const MaterialValues materialValues = ResolveModelMaterial(config, mesh.materialIndex);
			req.materialData = MakeMaterial3dData(materialValues, SrgbToLinear(config.color), config.uvTransform, config.material.alphaCutoff);
			// メッシュのワールド行列（ノードの行列を挟む。スキンのあるメッシュは挟まない）
			const Matrix4x4 meshWorldMatrix = ModelManager::MeshMatrix(mesh, nodeMatrix) * worldMatrix;
			req.objectTransformData = MakeObjectTransform(meshWorldMatrix, config.billboard);
			req.cameraDistanceSq = CameraDistanceSq(meshWorldMatrix, config.camera); // 半透明のソートに使うカメラとの距離
			req.cameraData.worldPosition = config.camera ? config.camera->GetTranslation() : Vector3{};
			req.iblParamsAddress = config.env ? config.env->GetParametersAddress() : 0;
			// 描画設定
			req.shadingType = config.shadingType;
			req.blendMode = config.blendMode;
			req.rasterizerType = config.rasterizerType;
			req.depthMode = config.depthMode;
			req.receivesDecals = config.receivesDecals;
			static_cast<DrawTarget&>(req) = config; // 描き分け（ウィンドウ・層・目で隠したか）をまとめて写す（DrawTarget の所だけ代入する）
			req.debugName = &ModelManager::GetModelName(config.modelHandle);
			req.debugSubName = &asset->materials[mesh.materialIndex].name;

			RenderQueue::Request(std::move(req));
		}
	}
}

// ===== Triangle =====
void Renderer::DrawTriangle(const TriangleConfig& config) {
	// 頂点データ
	std::vector<Vertex3dData> vertices = {
	    {{config.top.x, config.top.y, config.top.z, 1.0f},       config.uvTop,   {0.0f, 0.0f, 1.0f}},
	    {{config.right.x, config.right.y, config.right.z, 1.0f}, config.uvRight, {0.0f, 0.0f, 1.0f}},
	    {{config.left.x, config.left.y, config.left.z, 1.0f},    config.uvLeft,  {0.0f, 0.0f, 1.0f}},
	};
	// WorldMatrix
	Matrix4x4 worldMatrix = MakeAffineMatrix(config.transform.scale, config.transform.rotation, config.transform.translation);
	// メッシュ作成
	PushMesh(config, std::move(vertices), {0, 1, 2}, worldMatrix);
}

// ===== Rect3d =====
void Renderer::DrawRect3d(const Rect3dConfig& config) { 
	// ワールド行列（ビルボードでも通常でも同じ作り方でOK。向きはVSが決める）
	Matrix4x4 worldMatrix = MakeAffineMatrix(config.transform.scale, config.transform.rotation, config.transform.translation);

	// 頂点はローカル座標のまま積む（VSがワールドへ変換する）
	std::vector<Vertex3dData> vertices;
	if (config.isBillboard) {
		// カメラを向くXY平面の板（±0.5）
		vertices = {
		    {{-0.5f, -0.5f, 0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
		    {{-0.5f, +0.5f, 0.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
		    {{+0.5f, -0.5f, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
		    {{+0.5f, +0.5f, 0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
		};
	} else {
		// 水平なXZ平面の板
		vertices = {
		    {{-0.5f, 0.0f, +0.5f, 1.0f}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
		    {{-0.5f, 0.0f, -0.5f, 1.0f}, {0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
		    {{+0.5f, 0.0f, +0.5f, 1.0f}, {1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
		    {{+0.5f, 0.0f, -0.5f, 1.0f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
		};
	}
	PushMesh(config, std::move(vertices), {0, 1, 2, 1, 3, 2}, worldMatrix);
}

// ===== Sprite3d =====
void Renderer::DrawSprite3d(const Sprite3dConfig& config) {
	// 板の4つの角（ローカル座標。pivot の所が原点、Y が上）
	const float left = -config.pivot.x * config.size.x;
	const float right = left + config.size.x;
	const float bottom = -config.pivot.y * config.size.y;
	const float top = bottom + config.size.y;
	// 表は -Z 側（カメラを置いたときの向き＝+Z を見ているカメラから見える面）。UVは左上が uvMin、右下が uvMax
	const Vector3 normal = {0.0f, 0.0f, -1.0f};
	std::vector<Vertex3dData> vertices = {
	    {{left, bottom, 0.0f, 1.0f},  {config.uvMin.x, config.uvMax.y}, normal}, // 左下
	    {{left, top, 0.0f, 1.0f},     {config.uvMin.x, config.uvMin.y}, normal}, // 左上
	    {{right, bottom, 0.0f, 1.0f}, {config.uvMax.x, config.uvMax.y}, normal}, // 右下
	    {{right, top, 0.0f, 1.0f},    {config.uvMax.x, config.uvMin.y}, normal}, // 右上
	};
	// 頂点はローカル座標のまま積む（VSがワールドへ変換する。ビルボードの向きもVSが決める）
	const Matrix4x4 worldMatrix = config.worldMatrix ? *config.worldMatrix : MakeIdentity4x4();
	PushMesh(config, std::move(vertices), {0, 1, 2, 1, 3, 2}, worldMatrix, config.billboard);
}

// ===== Quad3d =====
void Renderer::DrawQuad3d(const Quad3dConfig& config) { 
	// 頂点を算出
	Vector3 lb = config.lb, lt = config.lt, rb = config.rb, rt = config.rt; 
	if (config.rotate.x != 0.0f || config.rotate.y != 0.0f || config.rotate.z != 0.0f) {
		Vector3 center = {(lb.x + lt.x + rb.x + rt.x) * 0.25f, (lb.y + lt.y + rb.y + rt.y) * 0.25f, (lb.z + lt.z + rb.z + rt.z) * 0.25f};
		lb = RotateAround3d(lb, center, config.rotate);
		lt = RotateAround3d(lt, center, config.rotate);
		rb = RotateAround3d(rb, center, config.rotate);
		rt = RotateAround3d(rt, center, config.rotate);
	}
	// 法線
	Vector3 normal = CalcQuadNormal(lb, rb, lt);
	// 頂点データ
	std::vector<Vertex3dData> vertices = {
	    {{lb.x, lb.y, lb.z, 1.0f}, config.uvLb, normal},
	    {{lt.x, lt.y, lt.z, 1.0f}, config.uvLt, normal},
	    {{rb.x, rb.y, rb.z, 1.0f}, config.uvRb, normal},
	    {{rt.x, rt.y, rt.z, 1.0f}, config.uvRt, normal},
	};
	// メッシュ作成
	PushMesh(config, std::move(vertices), {0, 1, 2, 1, 3, 2}, MakeIdentity4x4());
}

// ===== 自分で並べた形 =====
void Renderer::DrawMesh(const MeshConfig& config, std::vector<Vertex3dData> vertices, std::vector<uint32_t> indices) {
	// 早期リターン（三角形が1つも無い）
	if (vertices.empty() || indices.size() < 3) {
		return;
	}
	// 頂点はワールド座標なので、ワールド行列は単位行列（DrawQuad3d と同じ）
	PushMesh(config, std::move(vertices), std::move(indices), MakeIdentity4x4());
}

// ===== AABB =====
void Renderer::DrawAABB(const AABBConfig& config) {
	// AABB
	const AABB& box = config.aabb;
	Vector3 center = {(box.min.x + box.max.x) * 0.5f, (box.min.y + box.max.y) * 0.5f, (box.min.z + box.max.z) * 0.5f};
	const std::array<Vector3, 8> corners = box.GetCorners();
	// 頂点データ
	std::vector<Vertex3dData> vertices;
	std::vector<uint32_t> indices;
	MakeBoxGeometry(corners, center, vertices, indices);
	// メッシュ作成
	PushMesh(config, std::move(vertices), std::move(indices), MakeIdentity4x4());
}

// ===== OBB =====
void Renderer::DrawOBB(const OBBConfig& config) {
	// OBB
	const OBB& obb = config.obb;
	const std::array<Vector3, 8> corners = obb.GetCorners();
	// 頂点生成
	std::vector<Vertex3dData> vertices;
	std::vector<uint32_t> indices;
	MakeBoxGeometry(corners, obb.center, vertices, indices);
	// メッシュ作成
	PushMesh(config, std::move(vertices), std::move(indices), MakeIdentity4x4());
}

// ===== Sphere =====
void Renderer::DrawSphere(const SphereConfig& config) {
	// ジオメトリキャッシュ（分割数ごとに1回生成）
	auto it = instance_->sphereGeometryCache_.find(config.subdivision);
	if (it == instance_->sphereGeometryCache_.end()) {
		it = instance_->sphereGeometryCache_.emplace(config.subdivision, GenerateSphereGeometry(config.subdivision)).first;
	}
	const SphereGeometry& geo = it->second;
	Matrix4x4 worldMatrix = MakeAffineMatrix(config.transform.scale, config.transform.rotation, config.transform.translation);
	// キャッシュはコピーして渡す（Requestが所有するため）
	PushMesh(config, std::vector<Vertex3dData>(geo.vertices), std::vector<uint32_t>(geo.indices), worldMatrix);
}


//=============================================================================
// Particle
//=============================================================================
void Renderer::DrawParticle(const ParticleConfig& config) {
	if (!config.particles || config.particles->empty()) {
		return;
	}
	HTT_ASSERT_MSG(config.camera != nullptr, "パーティクルにはカメラを設定してください");

	// list の粒を配列に並べ直して、配列の方で描く（描く処理を2か所に書かない）
	std::vector<ParticleData> instances;
	instances.reserve(config.particles->size());
	for (const Particle& p : *config.particles) {
		instances.push_back({p.world, p.color}); // フェード済みの色（sRGB のまま。DrawParticleInstances がリニアに直す）
	}
	ParticleInstancesConfig instancesConfig;
	static_cast<DrawTarget&>(instancesConfig) = config; // 描き分け（ウィンドウ・層・目で隠したか）をまとめて写す
	instancesConfig.instances = instances;
	instancesConfig.textureHandle = config.textureHandle;
	instancesConfig.color = config.color;
	instancesConfig.uvTransform = config.uvTransform;
	instancesConfig.blendMode = config.blendMode;
	DrawParticleInstances(instancesConfig);
}

void Renderer::DrawParticleInstances(const ParticleInstancesConfig& config) {
	if (config.instances.empty()) {
		return;
	}
	// ConstantBuffer,StructuredBufferに送る情報を作成（リクエストが持つので、ここで写す）
	ParticleRequest req;
	req.instances.reserve(config.instances.size());
	for (const ParticleData& instance : config.instances) {
		// 丸ごと写してから、色だけ sRGB → リニア（A はそのまま）。向き・角度などはそのまま渡る
		ParticleData& linear = req.instances.emplace_back(instance);
		linear.color = SrgbToLinear(instance.color);
	}
	// 全部の粒で同じマテリアル
	req.materialData.color = SrgbToLinear(config.color);
	req.materialData.uvTransform = MakeUVTransformMatrix(config.uvTransform);
	req.materialData.textureIndex = ResolveTextureIndex(config.textureHandle);
	req.materialData.isSoftCircle = config.isSoftCircle ? 1u : 0u;
	req.materialData.useFog = config.useFog ? 1u : 0u;
	req.blendMode = config.blendMode;
	req.isBeforeDepthOfField = config.useDepthOfField;
	static_cast<DrawTarget&>(req) = config; // 描き分け（ウィンドウ・層・目で隠したか）をまとめて写す（DrawTarget の所だけ代入する）
	RenderQueue::Request(std::move(req));
}


//=============================================================================
// Sprite
//=============================================================================
// ===== Rect2d =====
void Renderer::DrawSprite(const SpriteConfig& config) {
	// テクスチャ原寸(px)を基準解像度上の大きさとして扱う
	Vector2 texSize = TextureManager::GetTextureSize(ResolveTextureIndex(config.textureHandle));
	float width = texSize.x * config.scale.x;
	float height = texSize.y * config.scale.y;
	// pivotを原点にしたローカル矩形（基準解像度のピクセル単位、Y+が上）
	float left = -config.pivot.x * width;
	float right = left + width;
	float top = config.pivot.y * height;
	float bottom = top - height;
	// 頂点データ
	Vector2 lb = {left, bottom};
	Vector2 lt = {left, top};
	Vector2 rb = {right, bottom};
	Vector2 rt = {right, top};
	if (config.rotate != 0.0f) {
		const Vector2 origin = {0.0f, 0.0f};
		lb = RotateAround2d(lb, origin, config.rotate);
		lt = RotateAround2d(lt, origin, config.rotate);
		rb = RotateAround2d(rb, origin, config.rotate);
		rt = RotateAround2d(rt, origin, config.rotate);
	}
	// 中心へ移動してから正規化座標へ
	Vector2 center = GameScreen::NdcToPixel(config.position);
	lb = GameScreen::PixelToNdc(lb + center);
	lt = GameScreen::PixelToNdc(lt + center);
	rb = GameScreen::PixelToNdc(rb + center);
	rt = GameScreen::PixelToNdc(rt + center);
	PushSprite(config, lb, lt, rb, rt, {0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 1.0f}, {1.0f, 0.0f});
}

// ===== Quad2d =====
void Renderer::DrawQuad2d(const Quad2dConfig& config) {
	// 頂点情報
	Vector2 lb = config.lb, lt = config.lt, rb = config.rb, rt = config.rt;
	if (config.rotate != 0.0f) {
		// 正規化座標のまま回すと歪むので、ピクセル空間に直してから回す
		Vector2 lbPx = GameScreen::NdcToPixel(lb), ltPx = GameScreen::NdcToPixel(lt);
		Vector2 rbPx = GameScreen::NdcToPixel(rb), rtPx = GameScreen::NdcToPixel(rt);
		Vector2 center = {(lbPx.x + ltPx.x + rbPx.x + rtPx.x) * 0.25f, (lbPx.y + ltPx.y + rbPx.y + rtPx.y) * 0.25f};
		lb = GameScreen::PixelToNdc(RotateAround2d(lbPx, center, config.rotate));
		lt = GameScreen::PixelToNdc(RotateAround2d(ltPx, center, config.rotate));
		rb = GameScreen::PixelToNdc(RotateAround2d(rbPx, center, config.rotate));
		rt = GameScreen::PixelToNdc(RotateAround2d(rtPx, center, config.rotate));
	}
	PushSprite(config, lb, lt, rb, rt, config.uvLb, config.uvLt, config.uvRb, config.uvRt);
}

// ===== Text2d =====
void Renderer::DrawText2d(const Text2dConfig& config, std::vector<Vertex2dData> vertices) {
	if (vertices.empty()) {
		return;
	}
	TextRequest req;
	static_cast<Draw2dTarget&>(req) = config; // 描き分け（ウィンドウ・層・目で隠したか・重ねる順・ステンシル）をまとめて写す
	req.vertices = std::move(vertices);
	// 色は Inspector で選んだ sRGB の色なので、リニアに直す（A はそのまま）
	req.materialData.color = SrgbToLinear(config.color);
	req.materialData.outlineColor = SrgbToLinear(config.outlineColor);
	req.materialData.outlineWidth = config.outlineWidth;
	req.materialData.glowColor = SrgbToLinear(config.glowColor);
	req.materialData.glowWidth = config.glowWidth;
	req.materialData.shineColor = SrgbToLinear(config.shineColor);
	req.materialData.shinePosition = config.shinePosition;
	req.materialData.shineWidth = config.shineWidth;
	req.materialData.textureIndex = config.fontTextureIndex;
	req.blendMode = config.blendMode;
	RenderQueue::Request(std::move(req));
}

// ===== VectorShape =====
void Renderer::DrawVectorShape(const VectorShapeConfig& config) {
	VectorShapeRequest req;
	static_cast<Draw2dTarget&>(req) = config; // 描き分け（ウィンドウ・層・目で隠したか・重ねる順・ステンシル）をまとめて写す
	req.blendMode = config.blendMode;
	req.materialData = config.material;
	// 色は Inspector で選んだ sRGB の色なので、リニアに直す（A はそのまま）
	req.materialData.color = SrgbToLinear(config.material.color);
	req.materialData.gradientColor = SrgbToLinear(config.material.gradientColor);
	req.materialData.strokeColor = SrgbToLinear(config.material.strokeColor);
	// 頂点：texcoord に図形の真ん中を原点にした場所を入れる（Sprite2d.VS がそのまま PS へ運ぶ。板の中で直線で補間される）
	const Vector2 half = config.localHalf;
	req.vertices[0] = {
	    {config.lb.x, config.lb.y, 0.0f, 1.0f},
        {-half.x, -half.y}
    };
	req.vertices[1] = {
	    {config.lt.x, config.lt.y, 0.0f, 1.0f},
        {-half.x, +half.y}
    };
	req.vertices[2] = {
	    {config.rb.x, config.rb.y, 0.0f, 1.0f},
        {+half.x, -half.y}
    };
	req.vertices[3] = {
	    {config.rt.x, config.rt.y, 0.0f, 1.0f},
        {+half.x, +half.y}
    };
	RenderQueue::Request(std::move(req));
}

// ===== Polyline =====
void Renderer::DrawPolyline(const PolylineConfig& config) {
	if (config.mesh == nullptr || config.indexCount == 0) {
		return;
	}
	PolylineRequest req;
	static_cast<Draw2dTarget&>(req) = config; // 描き分け（ウィンドウ・層・目で隠したか・重ねる順・ステンシル）をまとめて写す
	req.blendMode = config.blendMode;
	req.mesh = config.mesh; // shared_ptr を写すだけ（頂点は写さない）
	req.indexStart = config.indexStart;
	req.indexCount = config.indexCount;
	req.materialData = config.material;
	// 色は Inspector で選んだ sRGB の色なので、リニアに直す（A はそのまま）
	req.materialData.fillColor = SrgbToLinear(config.material.fillColor);
	req.materialData.strokeColor = SrgbToLinear(config.material.strokeColor);
	RenderQueue::Request(std::move(req));
}

// ===== Line =====
void Renderer::DrawLines(const LineListConfig& config) {
	// 早期リターン
	if (config.lines.empty()) {
		return;
	}
	// bind情報
	LineRequest req;
	req.materialData.cameraWorldPos = config.camera ? config.camera->GetTranslation() : Vector3{};
	req.materialData.fadeStartDistance = config.fadeStartDistance;
	req.materialData.fadeEndDistance = config.fadeEndDistance;
	Matrix4x4 identity = MakeIdentity4x4();
	req.objectTransformData.worldMatrix = identity;
	req.depthMode = config.depthMode;
	req.vertices.reserve(config.lines.size() * 2);
	// 全体色は先に1回だけ直しておく（sRGB → リニア）
	const Vector4 base = SrgbToLinear(config.color);
	for (const LineSegment& seg : config.lines) {
		// 線ごとの色 × 全体色（どちらもリニアに直してから掛ける）
		const Vector4 col = MultiplyColor(SrgbToLinear(seg.color), base);
		req.vertices.push_back({
		    {seg.start.x, seg.start.y, seg.start.z, 1.0f},
            col
        });
		req.vertices.push_back({
		    {seg.end.x, seg.end.y, seg.end.z, 1.0f},
            col
        });
	}
	static_cast<DrawTarget&>(req) = config; // 描き分け（ウィンドウ・層・目で隠したか）をまとめて写す（DrawTarget の所だけ代入する）
	RenderQueue::Request(std::move(req));
}

//=============================================================================
// 2D頂点をcenterを原点としてZ軸回転させる
//=============================================================================
Vector2 Renderer::RotateAround2d(const Vector2& point, const Vector2& center, float radian) {
	float s = std::sin(radian);
	float c = std::cos(radian);
	float ox = point.x - center.x;
	float oy = point.y - center.y;
	return {ox * c - oy * s + center.x, ox * s + oy * c + center.y};
}


//=============================================================================
// 3D頂点をcenterを原点としてXYZ回転させる
//=============================================================================
Vector3 Renderer::RotateAround3d(const Vector3& point, const Vector3& center, const Vector3& rotation) {
	Vector4 local = {point.x - center.x, point.y - center.y, point.z - center.z, 1.0f};
	// 回転行列（scale={1,1,1}, translation={0,0,0}）
	Matrix4x4 rotMat = MakeAffineMatrix({1.0f, 1.0f, 1.0f}, rotation, {0.0f, 0.0f, 0.0f});
	Vector4 rotated = local * rotMat;
	return {rotated.x + center.x, rotated.y + center.y, rotated.z + center.z};
}


//=============================================================================
// 4頂点の四角形から表面法線を計算する
//=============================================================================
Vector3 Renderer::CalcQuadNormal(const Vector3& lb, const Vector3& rb, const Vector3& lt) {
	Vector3 edgeR = rb - lb;
	Vector3 edgeU = lt - lb;
	return Normalize(Cross(edgeR, edgeU));
}


//=============================================================================
// 球のジオメトリを生成（頂点・インデックス）
// 生成したジオメトリはsphereGeometryCache_にキャッシュされる
//=============================================================================
Renderer::SphereGeometry Renderer::GenerateSphereGeometry(int subdivision) {
	const float kLonEvery = MathUtility::kTwoPi / static_cast<float>(subdivision);
	const float kLatEvery = MathUtility::kPi / static_cast<float>(subdivision);
	const int kVertPerRow = subdivision + 1;

	SphereGeometry geo;
	geo.vertices.reserve(kVertPerRow * kVertPerRow);
	geo.indices.reserve(subdivision * subdivision * 6);

	for (int latIndex = 0; latIndex <= subdivision; ++latIndex) {
		float lat = -MathUtility::kPi / 2.0f + kLatEvery * latIndex;
		float v = 1.0f - static_cast<float>(latIndex) / static_cast<float>(subdivision);
		for (int lonIndex = 0; lonIndex <= subdivision; ++lonIndex) {
			float lon = kLonEvery * lonIndex;
			float u = static_cast<float>(lonIndex) / static_cast<float>(subdivision);
			float x = std::cos(lat) * std::cos(lon);
			float y = std::sin(lat);
			float z = std::cos(lat) * std::sin(lon);
			geo.vertices.push_back({
			    {x, y, z, 1.0f},
                {u, v},
                {x, y, z}
            });
		}
	}

	for (int latIndex = 0; latIndex < subdivision; ++latIndex) {
		for (int lonIndex = 0; lonIndex < subdivision; ++lonIndex) {
			uint32_t lb = latIndex * kVertPerRow + lonIndex;
			uint32_t lt = (latIndex + 1) * kVertPerRow + lonIndex;
			uint32_t rb = latIndex * kVertPerRow + (lonIndex + 1);
			uint32_t rt = (latIndex + 1) * kVertPerRow + (lonIndex + 1);
			geo.indices.insert(geo.indices.end(), {lb, lt, rb, lt, rt, rb});
		}
	}

	return geo;
}


//=============================================================================
// 8頂点のボックスを6面(24頂点・36インデックス)のジオメトリに変換する
// 面ごとに外向き法線を計算し、cross(rb-lb, lt-lb)が外向きになる巻き順で生成する
// （裏面カリング D3D12_CULL_MODE_BACK と整合）
//=============================================================================
void Renderer::MakeBoxGeometry(const std::array<Vector3, 8>& corners, const Vector3& center, std::vector<Vertex3dData>& outVertices, std::vector<uint32_t>& outIndices) {
	// 各面の頂点を（lb, lt, rb, rt）の順で指定する
	// cross（rb - lb, lt - lb）が外向きになるようにする
	const std::array<std::array<int, 4>, 6> kFaces = {{
	    {1, 5, 3, 7}, // +x
	    {0, 2, 4, 6}, // -x
	    {2, 3, 6, 7}, // +Y
	    {0, 4, 1, 5}, // -Y
	    {4, 6, 5, 7}, // +Z
	    {0, 1, 2, 3}, // -Z
	}};
	outVertices.clear();
	outIndices.clear();
	outVertices.reserve(24);
	outIndices.reserve(36);

	for (int f = 0; f < 6; ++f) {
		const Vector3& lb = corners[kFaces[f][0]];
		const Vector3& lt = corners[kFaces[f][1]];
		const Vector3& rb = corners[kFaces[f][2]];
		const Vector3& rt = corners[kFaces[f][3]];

		// ボックスでは「面中心 - ボックス中心」が外向き法線方向に一致する
		Vector3 faceCenter = {
		    (lb.x + lt.x + rb.x + rt.x) * 0.25f,
		    (lb.y + lt.y + rb.y + rt.y) * 0.25f,
		    (lb.z + lt.z + rb.z + rt.z) * 0.25f,
		};
		Vector3 normal = {faceCenter.x - center.x, faceCenter.y - center.y, faceCenter.z - center.z};
		float len = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
		if (len > 0.0001f) {
			normal.x /= len;
			normal.y /= len;
			normal.z /= len;
		}

		uint32_t base = static_cast<uint32_t>(outVertices.size());
		outVertices.push_back({
		    {lb.x, lb.y, lb.z, 1.0f},
            {0.0f, 1.0f},
            normal
        });
		outVertices.push_back({
		    {lt.x, lt.y, lt.z, 1.0f},
            {0.0f, 0.0f},
            normal
        });
		outVertices.push_back({
		    {rb.x, rb.y, rb.z, 1.0f},
            {1.0f, 1.0f},
            normal
        });
		outVertices.push_back({
		    {rt.x, rt.y, rt.z, 1.0f},
            {1.0f, 0.0f},
            normal
        });
		outIndices.insert(outIndices.end(), {base + 0, base + 2, base + 1, base + 2, base + 3, base + 1});
	}
}

//=============================================================================
// デカール
//=============================================================================
void Renderer::DrawDecal(const DecalConfig& config, std::span<const std::byte> drawData) {
	// --- マテリアルを描く形にする（Curve を付けた値は、進み具合で動かす）---
	ResolvedMaterial material;
	if (!MaterialManager::Resolve(config.material, config.progress, material)) {
		return; // マテリアルが無い・Shader が未選択
	}
	// --- 描けないマテリアルは、1回だけ警告して描かない（Inspector で選び間違えても止めない。毎フレーム警告しない）---
	static std::unordered_set<uint32_t> warnedMaterials; // 警告を出したマテリアルの番号
	const auto skip = [&config](std::string_view reason) {
		if (warnedMaterials.insert(config.material).second) {
			LogManager::Warning(std::format("{}: {}", MaterialManager::GetPath(config.material), reason));
		}
	};
	// Model の Shader などを選ぶと、頂点の形・描く所が合わない
	if (material.category != DrawCategory::Decal) {
		skip("Shader が Decal の物ではないので、デカールに使えません（#PROGRAM が category: Decal の .shader を選んでください）");
		return;
	}
	// 深度は読むだけの窓口で結んで描くので、書けない
	if (material.depth == DepthMode::TestWrite) {
		skip("デカールの Depth は TestNoWrite か Disable にしてください（深度を読みながら描くので、書けません）");
		return;
	}
	// 描き手の値（gDrawData）を読む Shader には、その値を送る描き手が要る
	const ProgramEntry& program = ShaderPackageLoader::GetProgramAt(material.programId);
	const ShaderConstantBuffer* drawDataBuffer = ShaderPackageLoader::GetShaderReflection(program.psName).FindConstantBuffer(kDrawDataConstantBufferName);
	if (drawDataBuffer != nullptr && drawData.empty()) {
		skip(std::format("この Shader は描き手の値（{}）を読みます。その値を送る描き手（Hitbox Telegraph など）で使ってください", kDrawDataConstantBufferName));
		return;
	}
	// 描き手の構造体と HLSL の構造体の大きさが違うのは、写し間違い（コードを直すまで正しく描けない）なので止める
	if (drawDataBuffer != nullptr) {
		constexpr size_t kRegisterBytes = 16; // 定数バッファは 16 バイトずつ
		const size_t alignedSize = (drawData.size() + kRegisterBytes - 1) / kRegisterBytes * kRegisterBytes;
		HTT_ASSERT_MSG(alignedSize == drawDataBuffer->size,
		    std::format("{}: 描き手の値の大きさが違います（C++ {} バイト / HLSL の {} {} バイト）。構造体の並びをそろえてください", program.assetPath, drawData.size(), kDrawDataConstantBufferName, drawDataBuffer->size));
	}

	// --- リクエスト ---
	DecalRequest req;
	static_cast<DrawTarget&>(req) = config; // 描き分け（ウィンドウ・層・目で隠したか）をまとめて写す（DrawTarget の所だけ代入する）
	req.programId = material.programId;
	req.blendMode = material.blend;
	req.rasterizerType = material.rasterizer;
	req.depthMode = material.depth;
	req.order = config.order;
	req.worldMatrix = config.worldMatrix;
	req.materialConstants = std::move(material.constants);
	if (drawDataBuffer != nullptr) {
		req.drawData.assign(drawData.begin(), drawData.end()); // 読まない Shader には送らない
	}
	RenderQueue::Request(std::move(req));
}
