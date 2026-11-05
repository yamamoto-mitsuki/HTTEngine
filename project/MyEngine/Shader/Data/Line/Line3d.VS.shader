#META
stage:   VS
profile: vs_6_0
#META_END

#HLSL
#include "Line3d.hlsli"
#include "Buffers/Camera.hlsli"
#include "Buffers/ObjectTransform.hlsli"

struct VertexShaderInput {
    float32_t4 position : POSITION0;
    float32_t4 color    : COLOR0;
};


VertexShaderOutput main(VertexShaderInput input) {
    VertexShaderOutput output;
    float4 worldPos = mul(input.position, gObjectTransform.world);
    output.position = mul(worldPos, gCamera.viewProj);
    output.worldPos = worldPos.xyz;
    output.color = input.color;
    return output;
    return output;
}
#HLSL_END