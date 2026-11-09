#META
stage: Include
#META_END

#HLSL
struct VertexShaderOutput
{
    float32_t4 position : SV_POSITION;
    float32_t2 texcoord : TEXCOORD0; // アトラスの UV
    float32_t2 local : TEXCOORD1;    // 文字のかたまりの中の場所（左上が 0,0、右下が 1,1。斜めの光に使う）
};
#HLSL_END