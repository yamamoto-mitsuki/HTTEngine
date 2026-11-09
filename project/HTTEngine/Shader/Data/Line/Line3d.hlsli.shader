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
#HLSL_END