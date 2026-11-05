#META
stage:   PS
profile: ps_6_0
#META_END

#PROGRAM
category: Model
shading:  Lambert
vs:       Object3dVS
#PROGRAM_END

#HLSL
#include "Object3d.hlsli"
#include "Buffers/Camera.hlsli"
#include "Buffers/Light.hlsli"
#include "Fog/Fog.hlsli"
#include "Resources/Texture.hlsli"

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
};
ConstantBuffer<Material> gMaterial : register(b20);

static const float PI = 3.141596535f;


PixelShaderOutput main(VertexShaderOutput input)
{
    float3 N = normalize(input.normal);
    float3 diffuseLighting = { 0.0f, 0.0f, 0.0f };

    // 平行光源
    {
        float3 lightDir = normalize(-gDirectionalLight.direction);
        float NdotL = saturate(dot(N,lightDir));
        diffuseLighting += gDirectionalLight.color.rgb * gDirectionalLight.intensity * NdotL;
    }
    // ポイントライト
     {
        for (int i = 0; i < gPointLights.count; ++i)
        {
            PointLight light = gPointLights.lights[i];
            float3 toLight = light.position - input.worldPosition;
            float distance = length(toLight);
            float3 lightDir = toLight / max(distance, 0.0001f);
            float NdotL = saturate(dot(N, lightDir));
            float factor = PointLightFactor(light, distance); // 距離による減衰
            diffuseLighting += light.color.rgb * light.intensity * NdotL * factor;
        }
    }
    // スポットライト
    {
        for (int i = 0; i < gSpotLights.count; ++i)
        {
            SpotLight light = gSpotLights.lights[i];
            float3 toLight = light.position - input.worldPosition;
            float distance = length(toLight);
            float3 lightDir = toLight / max(distance, 0.0001f);
            float NdotL = saturate(dot(N, lightDir));
            float factor = SpotLightFactor(light, lightDir, distance); // 距離と円錐による減衰
            diffuseLighting += light.color.rgb * light.intensity * NdotL * factor;
        }
    }

    float32_t4 transformedUV = mul(float32_t4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float32_t4 texColor      = gTextures[gMaterial.textureIndex].Sample(gSampler, transformedUV.xy);
    if (texColor.a <= gMaterial.alphaCutoff) // 切り抜き（alphaCutoffが0なら、ちょうど0のピクセルだけ捨てる）
    {
        discard;
    }
    float32_t4 baseColor = gMaterial.color * texColor;
    float32_t4 finalColor;
    finalColor.rgb = baseColor.rgb * diffuseLighting + gMaterial.emissive;
    finalColor.a = baseColor.a;

    PixelShaderOutput output;
    output.color = finalColor;
    output.color.rgb = ApplyFog(output.color.rgb, input.worldPosition); // 霧（色が決まった後に混ぜる）
    if (output.color.a == 0.0) { discard; }
    return output;
}
#HLSL_END