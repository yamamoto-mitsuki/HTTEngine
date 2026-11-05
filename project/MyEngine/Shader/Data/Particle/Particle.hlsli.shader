#META
stage: Include
#META_END

#HLSL
struct VertexShaderOutput
{
    float4 position      : SV_POSITION;
    float2 texcoord      : TEXCOORD0;
    float4 color         : COLOR0;
    float3 worldPosition : POSITION0; // 霧の距離・高さに使う
};

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};
#HLSL_END