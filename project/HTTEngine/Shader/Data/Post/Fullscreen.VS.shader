#META
stage: VS
profile : vs_6_0
#META_END

#HLSL
struct VSOutput
{
    float32_t4 position : SV_Position;
    float32_t2 texcoord : TEXCOORD0;
};

// 頂点バッファなしで画面全体を覆う三角形を1枚出す
VSOutput main(uint32_t id : SV_VertexID)
{
    VSOutput output;
    output.texcoord = float32_t2((id << 1) & 2, id & 2); // (0,0)(2,0)(0,2)
    output.position = float32_t4(output.texcoord * float32_t2(2, -2) + float32_t2(-1, 1), 0, 1);
    return output;
}
#HLSL_END