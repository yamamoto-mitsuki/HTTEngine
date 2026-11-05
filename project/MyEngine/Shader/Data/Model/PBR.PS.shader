#META
stage:   PS
profile: ps_6_0
#META_END

#PROGRAM
category: Model
shading:  PBR
vs:       Object3dVS
#PROGRAM_END

#HLSL
#include "Object3d.hlsli"
#include "Buffers/Camera.hlsli"
#include "Buffers/Light.hlsli"
#include "Fog/Fog.hlsli"
#include "Resources/Texture.hlsli"
#include "Resources/TextureCube.hlsli"

// マテリアル
struct Material
{
    float32_t4 color;
    float4x4 uvTransform;
    float32_t3 ambient;
    float alphaCutoff;
    float32_t3 diffuse;
    float padD;
    float32_t3 specular;
    float shininess;
    float32_t3 emissive;
    uint textureIndex;
    float metallic;
    float roughness;
    float padP0;
    float padP1;
};
ConstantBuffer<Material> gMaterial : register(b20);

// IBLの索引
struct IBLParams
{
    uint irradianceIndex;
    uint prefilterIndex;
    uint brdfLutIndex;
    uint prefilterMipCount;
    uint enabled;
    float3 _pad;
};
ConstantBuffer<IBLParams> gIBL : register(b21);


static const float PI = 3.14159265359f;

//=============================================================================
// D項: 法線分布関数（GGX/Trowbridge-Reitz）
// マイクロファセット（微細な凹凸面）のうち、どれだけがハーフベクトルHの方向を
// 向いているかの密度。これがハイライトの「形」を決める。
// roughnessが小さい→分布が尖る→鋭く明るいハイライト
// roughnessが大きい→分布が広がる→ぼんやり広いハイライト
//=============================================================================
float DistributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / max(PI * d * d, 0.0001f); // 0除算防止
}

//=============================================================================
// G項: 幾何減衰（Smith法 + Schlick-GGX近似）     粗さの計算
// マイクロファセット同士が光を遮り合う効果。粗い面ほど、また視線/ライトが
// 浅い角度になるほど、凹凸の影で反射が減る。
// これが無いと、粗い面や輪郭付近が物理的にありえない明るさになる。
//=============================================================================
float GeometrySchlickGGX(float NdotX, float roughness) {
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    return NdotX / (NdotX * (1.0f - k) + k);
}
float GeomtrySmith(float NdotV, float NdotL, float roughness) {
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

//=============================================================================
// F項: フレネル反射（Schlick近似）
// 見る角度が浅くなるほど反射率が上がる現象。F0は「正面から見たときの反射率」。
// 非金属: F0はほぼ無彩色の4%。金属: F0がアルベド色そのもの（金なら黄色い反射）
//=============================================================================
float32_t3 FresnalSchlick(float VdotH, float32_t3 F0) {
    return F0 + (1.0f - F0) * pow(saturate(1.0f -VdotH), 5.0f);
}
// roughness考慮フレネル（IBL用）
float32_t3 FresnelSchlickRoughness(float cosTheta, float32_t3 F0, float roughness)
{
    float32_t3 r = max((float32_t3) (1.0f - roughness), F0);
    return F0 + (r - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

//=============================================================================
// 1つのライトからの出射輝度を計算する（Cook-Torrance BRDF）
// L        : 表面→ライトへ向かう方向（正規化済み）
// radiance : ライトの色 * 強度 * 減衰。ライトの種類の違いはここに集約される
// 平行光源もポイントライトも、Lとradianceさえ作ればこの1つの関数で計算できる
//=============================================================================
float32_t3 CookTorranceLighting(float32_t3 N, float32_t3 V, float32_t3 L, float32_t3 albedo, float32_t3 F0, float32_t3 radiance)
{
    float32_t3 H = normalize(L + V);
    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));
    
    // --- Cook-Torrance スペキュラBRDF: D*G*F / (4 * NdotL * NdotV) ---
    float D = DistributionGGX(NdotH, gMaterial.roughness);
    float G = GeomtrySmith(NdotV, NdotL, gMaterial.roughness);
    float32_t3 F = FresnalSchlick(VdotH, F0);
    float32_t3 specular = (D * G * F) / max(4.0f * NdotL * NdotV, 0.00001f);

    // --- 拡散（エネルギー保存則） ---
    // Fで反射に使われた光の残り（1 - F）だけが拡散に回る。金属は拡散ゼロ
    // / PI はLambert拡散の正規化（半球だけ積分すると1になる）
    float32_t3 kD = (1.0f - F) * (1.0f - gMaterial.metallic);
    float32_t3 diffuse = kD * albedo / PI;

    return (diffuse + specular) * radiance * NdotL;
}


//=============================================================================
// IBLの環境光（拡散＋鏡面、split-sum）
//=============================================================================
float32_t3 CalcIBL(float32_t3 N, float32_t3 V, float32_t3 albedo, float32_t3 F0, float metallic, float roughness)
{
    float NdotV = saturate(dot(N, V));
    float32_t3 R = reflect(-V, N);

    float32_t3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
    float32_t3 kD = (1.0f - F) * (1.0f - metallic);

    // 拡散：irradianceキューブをNで引く
    float32_t3 irradiance = gTexturesCube[gIBL.irradianceIndex].Sample(gSampler, N).rgb;
    float32_t3 diffuse = irradiance * albedo;

    // 鏡面：prefilterをR＋roughness→mipで、brdfを(NdotV,roughness)で引く
    float maxMip = float(gIBL.prefilterMipCount - 1);
    float32_t3 prefiltered = gTexturesCube[gIBL.prefilterIndex].SampleLevel(gSampler, R, roughness * maxMip).rgb;
    float2 brdf = gTextures[gIBL.brdfLutIndex].Sample(gSampler, float2(NdotV, roughness)).rg;
    float32_t3 specular = prefiltered * (F * brdf.x + brdf.y);

    return kD * diffuse + specular;
}


PixelShaderOutput main(VertexShaderOutput input)
{
    // ===== 使用ベクトル =====
    float32_t3 N = normalize(input.normal);
    float32_t3 V = normalize(gCamera.worldPosition - input.worldPosition); // 視点へ向かう
    
    // ===== アルベド（素の色） =====
    float32_t4 transformdUV = mul(float32_t4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float32_t4 texColor = gTextures[gMaterial.textureIndex].Sample(gSampler, transformdUV.xy);
    if (texColor.a <= gMaterial.alphaCutoff) // 切り抜き（alphaCutoffが0なら、ちょうど0のピクセルだけ捨てる）
    {
        discard;
    }
    float32_t3 albedo = gMaterial.color.rgb * texColor.rgb;
    
    // ===== metallic計算 =====
    // 非金属(metallic=0): F0=4%グレー、albedoは拡散反射として使う
    // 金属(metallic=1):   F0=albedo（反射に色がつく）、拡散は0
    float32_t3 F0 = lerp(float32_t3(0.04f, 0.04f, 0.04f), albedo, gMaterial.metallic);
    
    // ===== 出射輝度（全ライトの合計） =====
    float32_t3 Lo = float32_t3(0.0f, 0.0f, 0.0f);
    
    // --- 平行光源 ---
    {
        float32_t3 L = normalize(-gDirectionalLight.direction); // 表面→ライトへ向かう方向
        float32_t3 radiance = gDirectionalLight.color.rgb * gDirectionalLight.intensity;
        Lo += CookTorranceLighting(N, V, L, albedo, F0, radiance);
    }
    // --- ポイントライト ---
    for (int i = 0; i < gPointLights.count; ++i)
    {
        PointLight light = gPointLights.lights[i];
        // 表面→ライトへ向かう方向と距離
        float32_t3 toLight = light.position - input.worldPosition;
        float distance = length(toLight);
        float32_t3 L = toLight / max(distance, 0.0001f);
        // 減衰込みの輝度で、平行光源と同じBRDFを足すだけ
        float32_t3 radiance = light.color.rgb * light.intensity * PointLightFactor(light, distance);
        Lo += CookTorranceLighting(N, V, L, albedo, F0, radiance);
    }
    // --- スポットライト ---
    {
        for (int i = 0; i < gSpotLights.count; ++i)
        {
            SpotLight light = gSpotLights.lights[i];
            // 表面→ライトへ向かう方向と距離
            float32_t3 toLight = light.position - input.worldPosition;
            float distance = length(toLight);
            float32_t3 L = toLight / max(distance, 0.0001f);
            // 減衰（距離と円錐）込みの輝度で、ポイントライトと同じBRDFを足すだけ
            float32_t3 radiance = light.color.rgb * light.intensity * SpotLightFactor(light, L, distance);
            Lo += CookTorranceLighting(N, V, L, albedo, F0, radiance);
        }
    }
    
    // 環境光は今は定数で代用
    // 環境光（IBL。無効時は定数フォールバック）
    float32_t3 ambient;
    if (gIBL.enabled != 0)
    {
        ambient = CalcIBL(N, V, albedo, F0, gMaterial.metallic, gMaterial.roughness);
    }
    else
    {
        ambient = gMaterial.ambient * albedo;
    }
    
    PixelShaderOutput output;
    output.color = float32_t4(Lo + ambient + gMaterial.emissive, gMaterial.color.a * texColor.a);
    output.color.rgb = ApplyFog(output.color.rgb, input.worldPosition); // 霧（色が決まった後に混ぜる）
    if (output.color.a == 0.0)
    {
        discard;
    }
    return output;
}
#HLSL_END