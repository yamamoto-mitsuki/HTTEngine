#META
stage:   VS
profile: vs_6_0
#META_END

#HLSL
#include "Sprite2d.hlsli"

struct VertexShaderInput {
    float32_t4 position : POSITION0;
    float32_t2 texcoord : TEXCOORD0;
};


VertexShaderOutput main(VertexShaderInput input) {
    VertexShaderOutput output;

    output.position = float32_t4(input.position.x, input.position.y, 0.0f, 1.0f);
    output.texcoord = input.texcoord;
    return output;
}
#HLSL_END