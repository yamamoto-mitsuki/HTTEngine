#META
stage: VS
path : VectorShape/Polyline.VS.hlsl
profile : vs_6_0
#META_END


#HLSL
#include "VectorShape/Polyline.hlsli"

// C++ の VertexPolylineData と同じ並び
struct VertexShaderInput
{
    float32_t2 position : POSITION0;
    float32_t2 local : TEXCOORD0;
    float32_t2 param : TEXCOORD1;
    uint piece : TEXCOORD2;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    // メッシュは Entity から見たピクセルで作ってあるので、回転・移動・アンカーはここで掛ける（動かしてもメッシュを作り直さない）
    output.position = mul(float32_t4(input.position, 0.0f, 1.0f), gMaterial.toScreen);
    output.local = input.local;
    output.param = input.param;
    output.piece = input.piece;
    return output;
}
#HLSL_END