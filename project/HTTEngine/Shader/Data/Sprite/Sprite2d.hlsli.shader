#META
stage: Include
#META_END

#HLSL
struct VertexShaderOutput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};
#HLSL_END