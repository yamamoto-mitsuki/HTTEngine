#META
stage: Include
#META_END

#HLSL
// 影の受け方。ModelのPSが平行光源の光にどれだけ影が掛かるかを読む
struct ShadowReceiver {
    uint maskIndex; // 影の量の画面
    uint hasMask;   // 1: 影の量の画面を読む 2: 影なし
    float2 pad;
};
ConstantBuffer<ShadowReceiver> gShadow : register(b24);

// ===== 平行光源の光が届く割合（1: 届く 0: 影）=====
float GetDirectionalShadow(float4 svPosition) {
    if (gShadow.hasMask == 0)
    {
        return 1.0f;
    }
    return gTextures[gShadow.maskIndex].Load(int3(svPosition.xy, 0)).r;
}
#HLSL_END