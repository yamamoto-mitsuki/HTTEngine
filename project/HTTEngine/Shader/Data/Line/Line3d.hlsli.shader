#META
stage: Include
#META_END

#HLSL
struct VertexShaderOutput {
    float32_t4 position : SV_POSITION;
    float32_t3 worldPos : POSITION0;
    float32_t4 color    : COLOR0;
};

struct PixelShaderOutput {
    float32_t4 color : SV_TARGET0;
};

// 線のマテリアル（C++ の MaterialLineData と同じ並び。Line3d.PS と GizmoLine.PS が b20 で受ける）
struct LineMaterial
{
    float32_t3 cameraWorldPos; // 距離を測るカメラの位置
    float32_t fadeStartDistance; // この距離から薄くなる
    float32_t fadeEndDistance; // この距離で見えなくなる
    uint32_t depthIndex; // 3D の絵の深度（gTextures[] の番号。GizmoLine.PS だけが読む）
    uint32_t testsDepth; // 1：物の後ろは描かない 0：いつも描く（GizmoLine.PS だけが読む）
    float32_t pad0;
};

// カメラからの距離で薄くする割合（1：そのまま 0：見えない）
float32_t CalcLineFade(LineMaterial material, float32_t3 worldPos)
{
    float32_t dist = length(worldPos - material.cameraWorldPos);
    return saturate(1.0f - (dist - material.fadeStartDistance) / max(material.fadeEndDistance - material.fadeStartDistance, 0.0001f));
}
#HLSL_END