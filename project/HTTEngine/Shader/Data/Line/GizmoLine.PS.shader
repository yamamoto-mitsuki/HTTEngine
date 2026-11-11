#META
stage: PS
profile : ps_6_0
#META_END

#PROGRAM
category: GizmoLine
shading : Unlit
vs: Line3dVS
depth : Disable
#PROGRAM_END

#HLSL
#include "Line3d.hlsli"
#include "Buffers/Camera.hlsli"
#include "Resources/Texture.hlsli"

ConstantBuffer<LineMaterial> gMaterial : register(b20);

// 物の面とこの割合（0.2%）までしか離れていない線は、手前とみなす
// 深度の絵は 3D の絵の大きさで、描画先と1ピクセルずつは合わない。面に沿った線（Collider の箱など）がちらつかないように
static const float32_t kDepthTolerance = 0.002f;

// ===== この位置が、3Dのに書かれたものの後ろか =====
bool IsBehindSurface(float32_t3 worldPos) {
    // --- 線の位置 → 画面の位置（-1〜+1）→ 深度の絵の UV（0〜1。y は下が +）---
    // SV_POSITION ではなくワールドの位置から戻すので、描画先の大きさ・黒帯があっても、深度の絵の同じ所を指す
    float32_t4 clip = mul(float32_t4(worldPos, 1.0f), gCamera.viewProj);
    float32_t2 ndc = clip.xy / clip.w;
    float32_t2 uv = float32_t2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
    // --- そのピクセルの深度（一番手前の不透明なもの。1なら何もない）---
    uint32_t width, height;
    gTextures[gMaterial.depthIndex].GetDimensions(width, height);
    int32_t2 pixel = clamp(int32_t2(uv * float32_t2(width, height)), int32_t2(0, 0), int32_t2(width - 1, height - 1));
    float32_t depth = gTextures[gMaterial.depthIndex].Load(int32_t3(pixel, 0)).r;
    if (depth >= 1.0f)
    {
        return false;
    }
    // --- 深度 → 物のワールドの位置 ---
    float32_t4 surface = mul(float32_t4(ndc, depth, 1.0f), gCamera.inverseViewProj);
    float32_t3 surfaceWorld = surface.xyz / surface.w;
    // --- カメラからの距離で比べる ---
    float32_t lineDistance = length(worldPos - gCamera.worldPosition);
    float32_t surfaceDistance = length(surfaceWorld - gCamera.worldPosition);
    return lineDistance > surfaceDistance * (1.0f + kDepthTolerance);
}


PixelShaderOutput main(VertexShaderOutput input) {
    if (gMaterial.testsDepth != 0 && IsBehindSurface(input.worldPos))
    {
        discard;
    }
    PixelShaderOutput output;
    output.color = input.color;
    output.color.a *= CalcLineFade(gMaterial, input.worldPos);
    if (output.color.a == 0.0f)
    {
        discard;
    }
    return output;
}
#HLSL_END