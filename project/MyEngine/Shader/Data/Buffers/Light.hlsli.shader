#META
stage: Include
#META_END

#HLSL
// 平行光源
struct DirectionalLight
{
    float32_t4 color;
    float32_t3 direction;
    float intensity;
};
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b2);


// ポイントライト
struct PointLight
{
    float32_t4 color;
    float32_t3 position;
    float intensity;
    float radius;
    float decay;
    float padA;
    float padB;
};

static const int kMaxPointLights = 64; // C++ kMaxPointLights と一致させること
struct PointLightLists
{
    PointLight lights[kMaxPointLights];
    uint count;
    float padA;
    float padB;
    float padC;
};
ConstantBuffer<PointLightLists> gPointLights : register(b3);

//=============================================================================
// ポイントライト1つ分の「光が届く割合」（0〜1）
// distance : 表面からライトまでの距離
// radiusで0になる。decayが大きいほど、近くだけ明るくなる
//=============================================================================
float PointLightFactor(PointLight light, float distance)
{
    float radius = max(light.radius, 0.0001f);
    float decay = max(light.decay, 0.0f);
    return pow(saturate(-distance / radius + 1.0f), decay);
}


// スポットライト
struct SpotLight
{
    float32_t4 color;
    float32_t3 position;
    float intensity;
    float32_t3 direction; // 照らす向き（正規化済み）
    float range;          // 光の届く最大距離
    float decay;          // 減衰率
    float cosOuter;       // 外側の角度のcos。これより外は照らさない
    float cosInner;       // 内側の角度のcos。これより内は100%で照らす
    float padA;
};

static const int kMaxSpotLights = 32; // C++ kMaxSpotLights と一致させること
struct SpotLightLists
{
    SpotLight lights[kMaxSpotLights];
    uint count;
    float padA;
    float padB;
    float padC;
};
ConstantBuffer<SpotLightLists> gSpotLights : register(b4);

//=============================================================================
// スポットライト1つ分の「光が届く割合」（0〜1）
// L        : 表面→ライトへ向かう方向（正規化済み）
// distance : 表面からライトまでの距離
// 距離による減衰（ポイントライトと同じ式） × 円錐による減衰
//=============================================================================
float SpotLightFactor(SpotLight light, float32_t3 L, float distance)
{
    // --- 距離（rangeで0になる） ---
    float range = max(light.range, 0.0001f);
    float decay = max(light.decay, 0.0f);
    float distanceFactor = pow(saturate(-distance / range + 1.0f), decay);

    // --- 円錐 ---
    // 「ライトの向き」と「ライト→表面の向き」が作る角度のcos。円錐の中心ほど1に近い
    float cosAngle = dot(light.direction, -L);
    // 外側の角度(cosOuter)で0、内側の角度(cosInner)で1になるように、間を直線でつなぐ
    float coneFactor = saturate((cosAngle - light.cosOuter) / max(light.cosInner - light.cosOuter, 0.0001f));

    return distanceFactor * coneFactor;
}
#HLSL_END