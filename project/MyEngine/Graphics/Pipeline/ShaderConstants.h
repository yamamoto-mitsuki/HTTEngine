#pragma once
#include <cstdint>

#include "MyEngine/Math/MathIncludes.h"
#include "MyEngine/Graphics/IBL/IBLConfig.h"
#include "MyEngine/Graphics/Pipeline/RenderStates.h"

// このファイルはShaderのレジスタに送る情報の構造体をまとめたファイル

//=============================================================================
// VS
//=============================================================================

// ===== 座標変換 =====
struct ObjectTransformData {
	Matrix4x4 worldMatrix = MakeIdentity4x4();
	Matrix4x4 normalMatrix = MakeIdentity4x4(); // 法線用。worldMatrixの「逆行列の転置」
	uint32_t billboardMode = 0;                 // BillboardModeの値（0=なし 1=全方向 2=Y軸だけ）
	float padA[3] = {};
};
static_assert(sizeof(ObjectTransformData) == 144, "HLSLのObjectTransformと大きさが違います");

/// <summary>
/// ワールド行列と、そこから作る法線用の行列をまとめて入れる
/// <para>非均一スケール（Yだけ2倍など）のとき、法線をワールド行列で変換すると面に垂直でなくなり陰影が崩れる</para>
/// </summary>
inline ObjectTransformData MakeObjectTransform(const Matrix4x4& worldMatrix, BillboardMode billboard = BillboardMode::None) {
	ObjectTransformData data;
	data.worldMatrix = worldMatrix;
	data.normalMatrix = Transpose(Inverse(worldMatrix));
	data.billboardMode = static_cast<uint32_t>(billboard);
	return data;
}

/// <summary>
/// 霧（距離の霧＋高さの霧）。カメラの定数バッファ（b0）に入れて、3D のシェーダーの最後で色に混ぜる
/// <para>値は FogSystem が、シーンの Fog Component から毎フレーム SceneRenderer::SetFog で渡す。初期値は「霧なし」（濃さの上限が 0）</para>
/// <para>HLSL の Camera（Buffers/Camera.hlsli）の fog〜 と同じ並び。16 バイトずつ3つ</para>
/// </summary>
struct FogData {
	Vector3 color = {0.0f, 0.0f, 0.0f};       // 距離の霧の色
	float startDistance = 0.0f;               // カメラからこの距離（m）までは、距離の霧が無い
	float density = 0.0f;                     // 距離の霧の濃さ（1m あたり。大きいほど近くから濃くなる）
	float maxOpacity = 0.0f;                  // 距離の霧の一番濃い所（0〜1。0 なら距離の霧なし）
	float heightBase = 0.0f;                  // 高さの霧が一番濃くなる高さ（ワールドの Y。これより下はずっと一番濃い）
	float heightFalloff = 1.0f;               // 高さの霧が、上へ薄くなる速さ（1m あたり）
	Vector3 heightColor = {0.0f, 0.0f, 0.0f}; // 高さの霧の色
	float heightMaxOpacity = 0.0f;            // 高さの霧の一番濃い所（0〜1。0 なら高さの霧なし）
};
static_assert(sizeof(FogData) == 48, "HLSLのCameraの霧の所と大きさが違います");

// ===== カメラ =====
struct CameraData {
	Matrix4x4 viewProj;
	Vector3 worldPosition;
	float padA = 0.0f;
	Vector3 right; // ビルボード用
	float padB = 0.0f;
	Vector3 up; // ビルボード用
	float padC = 0.0f;
	FogData fog;                // 霧（3D のシェーダーの最後で、色に混ぜる。Fog.hlsli の ApplyFog）
	Matrix4x4 inverseViewProj;  // viewProj の逆行列（画面の位置と深度から、ワールドの位置へ戻す。デカールが使う）
};
static_assert(sizeof(CameraData) == 224, "HLSLのCameraと大きさが違います");

// ===== パーティクル（粒1つ分。StructuredBuffer<Particle> gParticles として Particle.VS が読む）=====
// VS は world から中心（4行目）と大きさ（1行目の長さ＝横・2行目の長さ＝縦）だけを読み、カメラの右・上で板を組む（world の向きは使わない）
struct ParticleData {
	Matrix4x4 world;
	Vector4 color;
	Vector3 direction = {0.0f, 0.0f, 0.0f}; // 絵の右（+X）を向ける向き（ワールド。長さ 0 なら向けない＝Align To Velocity を使わない粒）
	float rotation = 0.0f;                  // 板を画面の上で回す角度（ラジアン。正で左回り。direction があれば、その向きからさらに回す）
};
static_assert(sizeof(ParticleData) == 96, "HLSLのParticleと大きさが違います");

// ===== デカールの箱（全部のデカールで共通。描き手ごとの値は gDrawData に別に送る）=====
// 箱は Transform の大きさ 1 で 1辺 1（中心が原点、-0.5〜+0.5）。箱の XZ がテクスチャの UV、Y が貼る向き（上から下へ貼る）
struct DecalData {
	Matrix4x4 worldToDecal = MakeIdentity4x4(); // ワールド → 箱の中（箱のワールド行列の逆行列。中なら x・y・z が -0.5〜+0.5）
	uint32_t depthIndex = 0;                    // 不透明な物の深度（gTextures[] の番号）
	float padA[3] = {};
};
static_assert(sizeof(DecalData) == 80, "HLSLのDecal（Decal/Decal.hlsli）と大きさが違います");

// ===== スキンの行列パレットの1つ分（骨1本分）。StructuredBuffer<Well> gMatrixPalette としてSkinning の CS が読む =====
struct WellForGPU {
	Matrix4x4 skeletonSpaceMatrix = MakeIdentity4x4();                 // 逆バインド行列 × 今の骨の行列（頂点を曲げる）
	Matrix4x4 skeletonSpaceInverseTransposeMatrix = MakeIdentity4x4(); // 上の逆行列の転置（法線を曲げる）
};
static_assert(sizeof(WellForGPU) == 128, "HLSLのWellと大きさが違います");


//=============================================================================
// PS
//=============================================================================
// ===== Spriteのマテリアル =====
struct Material2dData {
	Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};
	Matrix4x4 uvTransform = MakeIdentity4x4();
	uint32_t textureIndex = 0; // 使用するテクスチャのインデックス（0はテクスチャなし）
};

// ===== 文字のマテリアル =====
struct TextMaterialData {
	Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};        // 文字の色
	Vector4 outlineColor = {0.0f, 0.0f, 0.0f, 1.0f}; // 縁取りの色
	Vector4 glowColor = {1.0f, 1.0f, 1.0f, 1.0f};    // 光（グロー）の色
	Vector4 shineColor = {1.0f, 1.0f, 1.0f, 1.0f};   // 斜めに通る光の色
	float outlineWidth = 0.0f;                       // 縁取りの太さ（SDF の値で 0〜0.5。0 なら縁取りなし）
	uint32_t textureIndex = 0;                       // フォントのアトラスの SRV の番号（FontManager::GetAtlasTextureIndex）
	float glowWidth = 0.0f;                          // 光の広さ（SDF の値で。縁取りの外側へ。0 なら無し）
	float shinePosition = 0.0f;                      // 斜めの光の帯の真ん中（文字のかたまりの左が 0、右が 1）
	float shineWidth = 0.0f;                         // 帯の半分の幅（かたまりの幅に対して。0 なら無し）
};

// ===== 画像を使わないUIの図形のマテリアル（長さは全部描かれるピクセル）=====
struct VectorShapeMaterialData {
	Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};         // 塗りの色（グラデーションの始まりの色）
	Vector4 gradientColor = {1.0f, 1.0f, 1.0f, 1.0f}; // グラデーションの終わりの色
	Vector4 strokeColor = {0.0f, 0.0f, 0.0f, 1.0f};   // 縁取りの色
	Vector4 cornerRadius = {0.0f, 0.0f, 0.0f, 0.0f};  // 角の半径（x=左上 y=右上 z=右下 w=左下）
	Vector2 shapeHalf = {0.0f, 0.0f};                 // 図形の半分の大きさ（平行四辺形は、角を丸める前の形の真ん中の幅の半分と、高さの半分）
	Vector2 shapeParam = {0.0f, 0.0f};                // 円：x=半径 / リング：x=太さの真ん中の半径 y=太さの半分 / 平行四辺形：x=上の辺の横のずれ y=角の半径
	Vector2 gradientAxis = {0.0f, 0.0f};              // グラデーションの軸（直線：dot(位置, 軸) + 0.5 / 放射：length(位置 × 軸)）
	float gradientStart = 0.0f;                       // 色が変わり始める場所
	float gradientScale = 1.0f;                       // 1 ÷ 色が変わる範囲
	float strokeInner = 0.0f;                         // 輪郭からの距離がこれより内側が塗り（距離は内側がマイナス）
	float strokeOuter = 0.0f;                         // 輪郭からの距離がこれより内側が図形全体（縁取りを含む）
	float fillOpacity = 1.0f;                         // 塗りを出すか（0 か 1）
	float gaugeOffset = 0.0f;                         // 直線のゲージの切り口の場所
	Vector2 gaugeAxisA = {0.0f, 0.0f};                // ゲージの線の法線 A
	Vector2 gaugeAxisB = {0.0f, 0.0f};                // ゲージの線の法線 B（扇形の終わりの線）
	uint32_t shape = 0;                               // 図形の種類（VectorShapeType の順）
	uint32_t gradientType = 0;                        // グラデーションの種類（VectorShapeGradient の順）
	uint32_t gaugeMode = 0;                           // 0=切らない 1=直線 2=180 度より狭い扇形 3=180 度以上の扇形
	uint32_t blendMode = 0;                           // 重ね方（BlendMode の順）
	uint32_t strokeUsesGradient = 0;                  // 1：縁取りにも塗りのグラデーションを使う
	uint32_t drawPart = 0;                            // 何を描くか（UIDrawPart の順。マスクの図形は、塗り・縁取り・形だけを分けて描く）
	float padA[2] = {};
};
static_assert(sizeof(VectorShapeMaterialData) == 160, "HLSLのMaterial（VectorShape.PS）と大きさが違います");

// ===== UIの折れ線のマテリアル（VS と PS の両方が読む。長さは全部描かれるピクセル）=====
struct PolylineMaterialData {
	Matrix4x4 toScreen = MakeIdentity4x4();         // 頂点の位置（Entity から見たピクセル）→ 画面の正規化座標（回転・移動・アンカー）
	Vector4 fillColor = {1.0f, 1.0f, 1.0f, 1.0f};   // 塗りの色
	Vector4 strokeColor = {1.0f, 1.0f, 1.0f, 1.0f}; // 線の色
	float halfWidth = 0.0f;                         // 線の太さの半分
	uint32_t blendMode = 0;                         // 重ね方（BlendMode の順）
	uint32_t drawPart = 0;                          // 何を描くか（UIDrawPart の順。マスクの形だけを描くときは MaskRegion）
	float padA = 0.0f;
};
static_assert(sizeof(PolylineMaterialData) == 112, "HLSLのMaterial（Polyline.hlsli）と大きさが違います");

// ===== パーティクルのマテリアル =====
struct MaterialParticleData {
	Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};
	Matrix4x4 uvTransform = MakeIdentity4x4();
	uint32_t textureIndex = 0;
	uint32_t isSoftCircle = 0; // 1：テクスチャの代わりに、丸いぼかし（真ん中が濃く、縁へ向かって透明）
	uint32_t useFog = 1;       // 1：霧を混ぜる（0：霧の中でも、そのままの色）
};

// ===== モデル用のPS =====
struct Material3dData {
	Vector4 color;            // 色
	Matrix4x4 uvTransform;    // UV変換行列
	Vector3 ambient;          // Ka: 環境光色
	float alphaCutoff = 0.0f; // これ以下のアルファのピクセルを捨てる
	Vector3 diffuse;          // Kd: 拡散反射色
	float padB = 0.0f;
	Vector3 specular;          // Ks: 鏡面反射色
	float shininess;           // Ns: 鏡面反射指数
	Vector3 emissive;          // Ke: 自己発光色
	uint32_t textureIndex = 0; // 使用するテクスチャのインデックス（0はテクスチャなし）
	float metallic = 0.0f;     // 0=非金属 1=金属
	float roughness = 0.5f;    // 表面の粗さ（0=鏡面, 1=完全拡散）
	float padC = 0.0f;
	float padD = 0.0f;
};

// Line3D用マテリアル(b0, PS)
struct MaterialLineData {
	Vector3 cameraWorldPos;  // カメラのワールド座標
	float fadeStartDistance; // フェード開始距離
	float fadeEndDistance;   // フェード終了距離(この距離でalpha=0)
	float pad0 = 0.0f;
	float pad1 = 0.0f;
	float pad2 = 0.0f;
};

// IBLの索引
struct IBLParamsData {
	uint32_t irradianceIndex;
	uint32_t prefilterIndex;
	uint32_t brdfLutIndex;
	uint32_t prefilterMipCount = IBLConfig::kPrefilterMipCount;
	uint32_t enabled = 1;
	float pad[3];
};


//=============================================================================
// 光源
//=============================================================================

// 平行光源
struct DirectionalLightData {
	Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f};
	Vector3 direction = {0.0f, -1.0f, 0.0f};
	float intensity = 1.0f;
};


// 1個分のポイントライト
struct PointLightData {
	Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f}; // 色
	Vector3 position = {0.0f, 0.0f, 0.0f};    // 位置
	float intensity = 1.0f;                   // 輝度
	float radius = 10.0f;                     // ライトの届く最大距離
	float decay = 1.0f;                       // 減衰率
	float padA[2] = {};
};
static constexpr uint32_t kMaxPointLights = 64; // ポイントライトの最大設置数
// 複数のポイントライトを管理
struct PointLightListData {
	PointLightData lights[kMaxPointLights];
	uint32_t count = 0; // HLSL側の uint に合わせる
	float padA[3] = {};
};


// 1個分のスポットライト
struct SpotLightData {
	Vector4 color = {1.0f, 1.0f, 1.0f, 1.0f}; // 色
	Vector3 position = {0.0f, 0.0f, 0.0f};    // 位置
	float intensity = 1.0f;                   // 輝度
	Vector3 direction = {0.0f, -1.0f, 0.0f};  // 照らす向き（正規化済み）
	float range = 10.0f;                      // ライトの届く最大距離
	float decay = 1.0f;                       // 減衰率
	float cosOuter = 0.0f;                    // 外側の角度のcos（これより外は照らさない）
	float cosInner = 0.0f;                    // 内側の角度のcos（これより内は100%で照らす）
	float padA = 0.0f;
};
static_assert(sizeof(SpotLightData) == 64, "HLSLのSpotLightと大きさが違います");
static constexpr uint32_t kMaxSpotLights = 32; // スポットライトの最大設置数（HLSLと一致させる）
// 複数のスポットライトを管理
struct SpotLightListData {
	SpotLightData lights[kMaxSpotLights];
	uint32_t count = 0; // HLSL側の uint に合わせる
	float padA[3] = {};
};


//=============================================================================
// 天球
//=============================================================================
struct SkyboxData {
	Matrix4x4 world;
	Matrix4x4 viewProj;
	float intensity = 1.0f;
	int cubeIndex;
	float padA[2];
};