#META
stage: VS
profile : vs_6_0
#META_END

#HLSL
#include "Text2d.hlsli"

// 頂点は Sprite と同じ形（Vertex2dData）。position の z・w は使わない所なので、文字のかたまりの中の場所を入れて運ぶ
struct VertexShaderInput
{
    float32_t4 position : POSITION0; // x・y：画面の正規化座標 / z・w：かたまりの中の場所（0〜1）
    float32_t2 texcoord : TEXCOORD0;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    output.position = float32_t4(input.position.x, input.position.y, 0.0f, 1.0f);
    output.texcoord = input.texcoord;
    output.local = input.position.zw;
    return output;
}
#HLSL_END