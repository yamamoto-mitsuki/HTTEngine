#META
stage: CS
profile : cs_6_0
#META_END


#HLSL
// ===== CSでSkinning（1スレッド=1頂点）=====
// 元の頂点を、骨の行列パレットと重みで曲げて、gOutputVertices（SkinnedVertex）に書く
// 書いた物は、そのまま頂点バッファとして普通の Object3d.VS で描く


// ===== 行列パレット（骨1本につき1つ。番号はモデルのノードの番号。C++のWellForGPU と同じ並び）=====
struct Well
{
    float32_t4x4 skeletonSpaceMatrix; // 逆バインド行列 × 今の骨の行列（頂点を曲げる）
    float32_t4x4 skeletonSpaceInverseTransposeMatrix; // 上の行列の逆行列の転置（法線を曲げる）
};

// ===== 頂点（C++のVertex3dDataと同じ並び）=====
struct Vertex
{
    float32_t4 position;
    float32_t2 texcoord;
    float32_t3 normal;
};

// ===== 骨と重み（C++のVertexInfluence と同じ並び）=====
struct VertexInfluence
{
    float32_t4 weight;
    int32_t4 index;
};

// ===== Skinningの情報 =====
struct SkinningInformation
{
    uint32_t numVertices; // 頂点の数
};

StructuredBuffer<Well> gMatrixPalette : register(t0);         // 骨の行列（DrawModel が毎フレーム作る）
StructuredBuffer<Vertex> gInputVertices : register(t1);       // 元の頂点（VS では頂点バッファのスロット0だった物）
StructuredBuffer<VertexInfluence> gInfluences : register(t2); // 骨と重み（VS では頂点バッファのスロット1だった物）
RWStructuredBuffer<Vertex> gOutputVertices : register(u0);    // 曲げた頂点（SkinnedVertex）
ConstantBuffer<SkinningInformation> gSkinningInformation : register(b0); // 頂点の数


// 1グループ = 64スレッド
[numthreads(64,1,1)]
void main(uint32_t3 dispatchThreadId : SV_DispatchThreadID) {
    // 何番目の頂点を受け持つか
    uint32_t vertexIndex = dispatchThreadId.x;
    // 最後のグループには、頂点の数を超えたスレッドがいる。何もせず帰る
    if (vertexIndex >= gSkinningInformation.numVertices)
    {
        return;
    }
    Vertex input = gInputVertices[vertexIndex];
    VertexInfluence influence = gInfluences[vertexIndex];
    
    // ===== 骨4本それぞれで動かした位置・法線を、重みで混ぜる =====
    Vertex skinned;
    // --- 位置 ---
    skinned.position  = mul(input.position, gMatrixPalette[influence.index.x].skeletonSpaceMatrix) * influence.weight.x;
    skinned.position += mul(input.position, gMatrixPalette[influence.index.y].skeletonSpaceMatrix) * influence.weight.y;
    skinned.position += mul(input.position, gMatrixPalette[influence.index.z].skeletonSpaceMatrix) * influence.weight.z;
    skinned.position += mul(input.position, gMatrixPalette[influence.index.w].skeletonSpaceMatrix) * influence.weight.w;
    skinned.position.w = 1.0f; // 重みの合計が1なので本当は1のまま。計算の誤差を消しておく
    // --- 法線（3x3 で回すだけ。位置の移動は要らない）---
    skinned.normal  = mul(input.normal, (float32_t3x3) gMatrixPalette[influence.index.x].skeletonSpaceInverseTransposeMatrix) * influence.weight.x;
    skinned.normal += mul(input.normal, (float32_t3x3) gMatrixPalette[influence.index.y].skeletonSpaceInverseTransposeMatrix) * influence.weight.y;
    skinned.normal += mul(input.normal, (float32_t3x3) gMatrixPalette[influence.index.z].skeletonSpaceInverseTransposeMatrix) * influence.weight.z;
    skinned.normal += mul(input.normal, (float32_t3x3) gMatrixPalette[influence.index.w].skeletonSpaceInverseTransposeMatrix) * influence.weight.w;
    skinned.normal = normalize(skinned.normal); // 混ぜると長さが1でなくなるので戻す
    // --- UV は曲げても変わらない ---
    skinned.texcoord = input.texcoord;

    gOutputVertices[vertexIndex] = skinned;
}
#HLSL_END