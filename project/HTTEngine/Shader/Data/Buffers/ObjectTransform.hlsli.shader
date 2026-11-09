#META
stage: Include
#META_END

#HLSL
struct ObjectTransform
{
    float4x4 world;
    float4x4 normalMatrix;
    uint billboardMode; // 0=なし 1=全方向 2=Y軸だけ（C++の BillboardMode と同じ並び）
    float3 padA;
};
ConstantBuffer<ObjectTransform> gObjectTransform : register(b1);
#HLSL_END