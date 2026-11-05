#META
stage:   VS
profile: vs_6_0
#META_END

#HLSL
#include "Object3d.hlsli"
#include "Buffers/Camera.hlsli"
#include "Buffers/ObjectTransform.hlsli"


struct VertexShaderInput {
    float32_t4 position : POSITION0;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal   : NORMAL0;
};

VertexShaderOutput main(VertexShaderInput input) {
    VertexShaderOutput output;

    float4 worldPos;
    float3 worldNormal;
    if (gObjectTransform.billboardMode != 0)
    {
        // --- カメラの方を向く3本の軸を作る ---
        float3 axisX = gCamera.right;
        float3 axisY = gCamera.up;
        if (gObjectTransform.billboardMode == 2)
        {
            // Y軸だけ回す：上はワールドの上のまま、右はカメラの右を水平にしたもの
            float3 flatRight = float3(gCamera.right.x, 0.0f, gCamera.right.z);
            float flatLength = length(flatRight);
            axisX = (flatLength > 1.0e-4f) ? flatRight / flatLength : gCamera.right;
            axisY = float3(0.0f, 1.0f, 0.0f);
        }
        float3 axisZ = cross(axisX, axisY); // 左手系なので「右 × 上 = 奥」

        // --- world から位置と大きさだけ取り出す（回転はカメラの軸で置き換える）---
        float3 center = float3(gObjectTransform.world._41, gObjectTransform.world._42, gObjectTransform.world._43);
        float3 scale = float3(
            length(float3(gObjectTransform.world._11, gObjectTransform.world._12, gObjectTransform.world._13)),
            length(float3(gObjectTransform.world._21, gObjectTransform.world._22, gObjectTransform.world._23)),
            length(float3(gObjectTransform.world._31, gObjectTransform.world._32, gObjectTransform.world._33)));

        // --- 頂点をカメラの軸で組み立てる（zも使うので、立体のモデルも潰れない）---
        float3 local = input.position.xyz * scale;
        worldPos = float4(center + axisX * local.x + axisY * local.y + axisZ * local.z, 1.0f);
        // 法線は大きさで割ってから同じ軸で回す（非均一スケールでも面に垂直になる。Light.md Step 5.7と同じ理由）
        worldNormal = normalize(axisX * (input.normal.x / scale.x) + axisY * (input.normal.y / scale.y) + axisZ * (input.normal.z / scale.z));
    }
    else
    {
        worldPos = mul(input.position, gObjectTransform.world);
        worldNormal = normalize(mul(input.normal, (float32_t3x3) gObjectTransform.normalMatrix));
    }

    output.position = mul(worldPos, gCamera.viewProj);
    output.texcoord = input.texcoord;
    output.normal = worldNormal;
    output.worldPosition = worldPos.xyz;
    return output;
}
#HLSL_END