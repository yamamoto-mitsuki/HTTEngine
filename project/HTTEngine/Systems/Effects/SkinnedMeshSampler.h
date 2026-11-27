#pragma once
#include <cstdint>
#include <vector>

#include "HTTEngine/Components/Effects/ParticleSystemComponent.h"
#include "HTTEngine/Core/Handle.h"
#include "HTTEngine/ECS/Entity.h"
#include "HTTEngine/Graphics/Model/ModelManager.h"
#include "HTTEngine/Graphics/Pipeline/RenderStates.h"
#include "HTTEngine/Math/Matrix4x4.h"
#include "HTTEngine/Math/Vector2.h"
#include "HTTEngine/Math/Vector3.h"

/// <summary>
/// Particle System の Shape の Skinned Mesh。骨で曲がるモデルの今のポーズの、表面・骨の線から、粒が出る場所と向きを選ぶ
/// <para>GPU の Skinning.CS は全部の頂点を曲げるが、粒に要るのは選んだ三角形の3頂点だけ。その3つだけ CPU で同じ計算をする（GPU から読み戻さない）</para>
/// <para>三角形を面積で選ぶための表は、モデルごとに初めて使ったときに1回だけ作って持つ（Clear で捨てる）</para>
/// <para>使い方：EmitParticles 1回につき MakeContext を1回 → 粒の数だけ Sample</para>
/// </summary>
namespace SkinnedMeshSampler {
struct MeshTable;

// ===== 選んだ1点（ワールド座標）=====
struct SamplePoint {
	Vector3 position; // 出る場所（表面の点・骨の線の上の点）
	Vector3 normal;   // 外への向き
	Vector2 uv = {0.0f, 0.0f}; // 表面の点の UV（3頂点の UV を、点を選んだ重心座標で混ぜた物。Bone は (0, 0)）
};

// ===== 1回のEmitの間だけ使う材料 =====
struct Context {
	const ModelManager::ModelAsset* asset = nullptr;         // 出す元のモデル
	const MeshTable* table = nullptr;                        // そのモデルの選ぶための表
	const std::vector<Matrix4x4>* nodeMatrices = nullptr;    // 今のポーズ（AnimationSystem::GetNodeMatrices。nullptr ならモデルに入っている形）
	Matrix4x4 modelWorld = MakeIdentity4x4();                // モデルを持つEntityのワールド行列（モデルの空間 → ワールド）
	ParticleMeshSource source = ParticleMeshSource::Surface; // 表面から・骨の線から
	DissolveParams dissolve;                                 // モデルの今のディゾルブ（境目から出すとき）
};

/// <summary>
/// 粒の Entity から親へたどってモデルを探し、今のポーズで選ぶ材料を作る
/// <para>Surface なら、行列パレット（骨の数だけ逆行列）もここで1回だけ作る。粒ごとに作らない</para>
/// </summary>
/// <param name="emitter">Particle System を持つ Entity（自分 → 親 → 親の親 … の順に、最初の Model Renderer を使う）</param>
/// <param name="source">表面から・骨の線から</param>
/// <param name="context">書き込む先</param>
/// <returns>出せるか（Model Renderer が無い・骨で曲がるメッシュが無い・Bone なのに親も骨の骨が無いなら false）</returns>
bool MakeContext(Handle<Entity> emitter, ParticleMeshSource source, Context& context);

// 1点選ぶ（MakeContext が true を返した context だけ渡す）
SamplePoint Sample(const Context& context);

/// <summary>
/// モデルのディゾルブの境目（光っている帯）から1点選ぶ。表面から選んで、帯に入るまで選び直す（最大 kMaxEdgeTries 回）
/// <para>見つからなければ false（境目が狭い・Amount が 0 か 1 で境目が無い・Mesh Source が Bone・画像を CPU で読めない）。そのときは粒を出さない</para>
/// </summary>
bool SampleDissolveEdge(const Context& context, SamplePoint& out);

// 持っている表を全部捨てる（ParticleSystem::Release から）
void Clear();
} // namespace SkinnedMeshSampler
