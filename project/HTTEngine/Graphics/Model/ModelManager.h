#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <unordered_map>

#include <d3d12.h>
#include <wrl/client.h>

#include "HTTEngine/Math/Vector2.h"
#include "HTTEngine/Math/Vector3.h"
#include "HTTEngine/Math/Vector4.h"
#include "HTTEngine/Math/Matrix4x4.h"
#include "HTTEngine/Math/Transform.h"
#include "HTTEngine/Graphics/GPU/DirectXCommon.h"
#include "HTTEngine/Graphics/Pipeline/VertexFormat.h"
#include "HTTEngine/Graphics/Pipeline/ShaderConstants.h"

// 前方宣言
class RenderContext;
class Camera;


/// <summary>
/// モデルを読みこみ、番号で管理する。描画リクエストも送る。
/// </summary>
class ModelManager {
public:
	static constexpr uint32_t kInvalidNode = 0xFFFFFFFF; // 親がいないノード（根）を表す

	// ===== 透け方（glTF の alphaMode。OBJ には無いので Opaque）=====
	enum class AlphaMode {
		Opaque, // 不透明（Base Color の A は使わない）
		Mask,   // 切り抜き（A が alphaCutoff 以下のピクセルを捨てる）
		Blend,  // 半透明（A の分だけ透ける）
	};

	// ===== モデルファイルから読み込んだマテリアル（OBJ の mtl・glTF の material を、エンジンの1つの形にそろえた物）=====
	// 色はどのファイルから読んでもリニア（glTF は決まりでリニア。OBJ の Kd も、Blender が書くリニアの値として扱う）
	struct ImportedMaterial {
		std::string name;                             // マテリアルの名前（表示・GPU のデバッグ名に使う。ぶつかることもあるので、引くときは番号で）
		Vector4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f}; // 物の色（RGB：OBJ の Kd・glTF の baseColorFactor、A：OBJ の d・glTF の baseColorFactor の A）。テクスチャに掛ける
		Vector3 ambient = {0.0f, 0.0f, 0.0f};         // Ka: 環境光色
		Vector3 specular = {1.0f, 1.0f, 1.0f};        // Ks: 鏡面反射色
		Vector3 emissive = {0.0f, 0.0f, 0.0f};        // Ke: 自己発光色
		float shininess = 32.0f;                      // Ns: 鏡面反射指数
		float metallic = 0.0f;                        // 0=非金属 1=金属
		float roughness = 0.5f;                       // 表面の粗さ（0=鏡面, 1=完全拡散）
		AlphaMode alphaMode = AlphaMode::Opaque;      // 透け方（glTF の alphaMode。今は記録だけ。マテリアルの枠を作るときに Blend・切り抜きへ渡す）
		float alphaCutoff = 0.5f;                     // Mask のときの切り抜きの値（glTF の alphaCutoff。今は記録だけ）
		int32_t illum = 1;                            // illum: 照明モデル番号
		std::string baseColorTexturePath;             // 物の色のテクスチャ（OBJ の map_Kd・glTF の baseColorTexture）
		std::string ambientTexFilePath;               // map_Ka: アンビエントテクスチャ
		std::string specularTexFilePath;              // map_Ks: スペキュラテクスチャ
		std::string bumpTexFilePath;                  // map_bump/bump: バンプマップ
		uint32_t baseColorTextureHandle = 0;          // baseColorTexturePath を TextureManager::Load() で読んだ番号（0 はテクスチャ無し）
	};

	// ===== メッシュデータ（1つのusemtlに対応）=====
	struct SubMesh {
		// CPU側で使う。ロード時のバッファ構築に使用
		std::vector<Vertex3dData> vertices;
		std::vector<uint32_t> indices;
		uint32_t materialIndex = 0; // 使うマテリアルの番号（ModelAsset::materials の番号＝マテリアルの枠の番号）
		// GPU側で使う。描画時にバインド
		Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer;
		Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer;
		D3D12_VERTEX_BUFFER_VIEW vbv{};
		D3D12_INDEX_BUFFER_VIEW ibv{};
		uint32_t indexCount = 0;
		// スキン（isSkinnedのときだけ中身がある）
		std::vector<VertexInfluence> influences; // 頂点ごとの骨と重み
		Microsoft::WRL::ComPtr<ID3D12Resource> influenceBuffer; // influenceのGPU常駐バッファ（Skinning の CS が StructuredBuffer として読む）
		bool isSkinned = false; // 骨で曲げるメッシュか（頂点がもうモデル全体の空間にあるので、ノードの行列を掛けない。MeshMatrix）
	};

	// ===== ノード（モデルファイルの中の階層構造。glTF / FBX / OBJ 共通） =====
	// ポインタで木を作らず、配列とインデックスで持つ（0番が根）
	struct Node {
		std::string name;                          // ノード名（Blenderなどで付けた名前）
		Matrix4x4 localMatrix = MakeIdentity4x4(); // 親から見た行列（エンジンの向きに直したもの）
		Matrix4x4 worldMatrix = MakeIdentity4x4(); // 根からたどって掛け合わせた行列（読み込み時に計算）
		uint32_t parent = kInvalidNode;            // 親のインデックス。根は kInvalidNode
		std::vector<uint32_t> children;            // 子のインデックス
		std::vector<uint32_t> meshIndices;         // このノードが持つメッシュ（ModelAsset::meshes の番号）
		bool isJoint = false;                      // 骨（スキンのあるメッシュが、頂点を曲げるのに使うノード）
		bool isSkeleton = false;                   // 骨か、骨の親・祖先か
		Matrix4x4 inverseBindPoseMatrix = MakeIdentity4x4(); // 逆バインド行列
	};

	// ===== モデルデータ（複数メッシュ・マテリアル・ノード）=====
	struct ModelAsset {
		std::string name;
		std::vector<SubMesh> meshes;
		std::vector<Node> nodes; // 階層構造。0番が根
		std::vector<ImportedMaterial> materials; // 読み込んだマテリアル（メッシュが使う物だけ、ファイルの並びのまま。番号で引く）
	};

	/// <summary>
	/// 初期化。TextureManager::Init()の後に呼ぶ
	/// </summary>
	static void Initialize();

	/// <summary>
	/// 全モデルの解放。WinMainのreturn 0の前に呼ぶ
	/// </summary>
	static void Release();

	/// <summary>
	/// OBJファイルを読み込む。同じパスを渡すとキャッシュから返す
	/// </summary>
	/// <param name="filePath">モデルファイルのパス（例: "resources/cube/cube.obj"）。UTF-8 の文字から作るときは ToPath を通す</param>
	/// <returns>描画時に使うモデルハンドル</returns>
	static uint32_t Load(const std::filesystem::path& filePath);

	/// <summary>
	/// スキンの行列パレット（スキン用のVSが頂点を曲げるのに使う、骨ごとの行列）を作る
	/// <para>palette[ノードの番号] = 逆バインド行列 × 今の骨の行列。骨で無いノードは単位行列</para>
	/// </summary>
	/// <param name="nodeMatrices">今のノードの行列（AnimationPlayer::GetNodeMatrices）。nullptr ならモデルに入ってる形</param>
	/// <param name="palette">書き込む先</param>
	static void MakeMatrixPalette(const ModelAsset& asset, const std::vector<Matrix4x4>* nodeMatrices, std::vector<WellForGPU>& palette);

	/// <summary>
	/// モデルハンドルからモデルを取得
	/// </summary>
	/// <param name="modelHandle">Loadで呼んだモデルハンドル</param>
	/// <returns></returns>
	static const ModelAsset* GetModelAsset(uint32_t modelHandle);

	/// <summary>
	/// 読み込んだマテリアルを番号で取得
	/// </summary>
	/// <param name="handle">Load が返したモデルハンドル</param>
	/// <param name="materialIndex">マテリアルの番号（SubMesh::materialIndex）</param>
	/// <returns>無いモデル・番号なら nullptr</returns>
	static const ImportedMaterial* GetImportedMaterial(uint32_t handle, uint32_t materialIndex);

	/// <summary>
	/// メッシュを置く行列（ノードの行列のうち、そのメッシュに掛ける物）
	/// <para>スキンのあるメッシュは、gltfの決まりでノードの行列を掛けない（頂点がもうモデル全体の空間にある）。
	/// 掛けると、Mixamoのモデルは Armatureの0.001倍・90度の回転が2回かかって、小さく寝た形になる</para>
	/// </summary>
	/// <param name="nodeMatrix">そのメッシュを持つノードの行列（Node::worldMatrix か、AnimationPlayer が作った物）</param>
	static Matrix4x4 MeshMatrix(const SubMesh& mesh, const Matrix4x4& nodeMatrix) { return mesh.isSkinned ? MakeIdentity4x4() : nodeMatrix; }

	// ハンドルから表示名を取得（見つからなければ空）
	static const std::string& GetModelName(uint32_t modelHandle);
	// ハンドルから読み込んだときのパスを取得（見つからなければ空）。シーンの保存でパスに直すのに使う）
	static const std::string& GetModelPath(uint32_t modelHandle);


private:
	static ModelManager& GetInstance();
	// コピー・ムーブ禁止
	ModelManager(const ModelManager&) = delete;
	ModelManager& operator=(const ModelManager&) = delete;
	ModelManager() = default;

	/// <summary>
	/// OBJファイルを読み込む
	/// </summary>
	static ModelAsset LoadObjFile(const std::filesystem::path& filePath);

	/// <summary>
	/// aiNodeの木を ModelAsset::nodes（配列）へ写す。子を再帰でたどる
	/// </summary>
	/// <returns>追加したノードのインデックス</returns>
	static uint32_t LoadNode(const struct aiNode* node, uint32_t parentIndex, const Matrix4x4& parentWorld, ModelAsset& modelAsset);

	/// <summary>
	/// スキン（骨）を読む。LoadNodeの後に呼ぶ。
	/// <para>・メッシュの骨（aiBone）の名前と同じノードに isJoint、その祖先にisSkeletonの印を付ける</para>
	/// <para>・骨のノードに逆バインド行列を入れる</para>
	/// <para>・スキンのあるメッシュに、頂点ごとの骨と重み（SubMesh::influences）を作る
	/// </summary>
	static void LoadSkin(const struct aiScene* scene, ModelAsset& modelAsset);

	/// <summary>
	/// 頂点に骨を1本足す。空き枠に入れ、4本とも埋まっていたら一番軽い骨と入れ替える
	/// </summary>
	static void AddInfluence(VertexInfluence& influence, int32_t jointIndex, float weight);

	/// <summary>
	/// 重みの合計を1にそろえる
	/// </summary>
	/// <param name="influence"></param>
	static void NormalizeInfluence(VertexInfluence& influence);

	/// <summary>
	/// メッシュのCPU頂点をGPU常駐バッファへ転送する（ロード時1回のみ）
	/// </summary>
	/// <param name="bufferName">バッファの名前の頭（「ファイル名/マテリアル名」。PIX などで見分ける）</param>
	static void MakeMeshBuffer(SubMesh& mesh, const std::string& bufferName);

	// 名前を変えた・動かしたファイル：キーのパスと、マテリアルのテクスチャのパスを付け替える（番号はそのまま。AssetMoveRegistry から呼ばれる）
	static void OnAssetMoved(const struct MovedPath& moved);

	std::unordered_map<uint32_t, ModelAsset> models_;        // キー → ModelAsset
	std::unordered_map<std::string, uint32_t> pathToHandle_; // ファイルパス → キー（重複防止）
	uint32_t modelsKey_ = 1;
};