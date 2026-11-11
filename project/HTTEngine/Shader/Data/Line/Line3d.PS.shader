#META
stage:   PS
profile: ps_6_0
#META_END

#PROGRAM
category: Line
shading:  Unlit
vs:       Line3dVS
#PROGRAM_END

#HLSL
#include "Line3d.hlsli"

ConstantBuffer<LineMaterial> gMaterial : register(b20);


PixelShaderOutput main(VertexShaderOutput input) {
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