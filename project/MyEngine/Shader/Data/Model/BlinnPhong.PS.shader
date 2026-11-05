#META
stage:   PS
profile: ps_6_0
#META_END

#PROGRAM
category: Model
shading:  BlinnPhong
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



PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float3 N = normalize(input.normal);
    // 最終的な拡散光, 鏡面反射
    float3 diffuseLighting = { 0.0f, 0.0f, 0.0f };
    float3 specularLighting = { 0.0f, 0.0f, 0.0f };
    
    
    // ===== DirectionlLight =====
    {
        // --- HalfLambert ---
        float3 lightDir = normalize(-gDirectionalLight.direction);
        float cos = pow(dot(N, lightDir) * 0.5f + 0.5f, 2.0f);
        // 拡散反射
        diffuseLighting += gDirectionalLight.color.rgb * cos * gDirectionalLight.intensity;
        
        // --- BlinnPhong ---
        float3 toEye = normalize(gCamera.worldPosition - input.worldPosition);
        float3 halfVector = normalize(lightDir + toEye);
        float NdotH = dot(N, halfVector);
        float specularPow = pow(saturate(NdotH), gMaterial.shininess); // 反射強度
        // 鏡面反射
        specularLighting += gDirectionalLight.color.rgb * gDirectionalLight.intensity * specularPow;
    }
    // ===== PointLight =====
    {
        for (int i = 0; i < gPointLights.count; ++i)
        {
            // 1つ分のライト
            PointLight light = gPointLights.lights[i];
            
            // --- 減衰（radiusで0になる） ---
            float3 toLight = light.position - input.worldPosition;
            float distance = length(toLight);
            float3 lightDir = toLight / max(distance, 0.0001f);
            float factor = PointLightFactor(light, distance);
            
            // --- HalfLambert ---
            float cos = pow(dot(N, lightDir) * 0.5f + 0.5f, 2.0f);
            // 拡散反射
            diffuseLighting += light.color.rgb * cos * light.intensity * factor;
            
            // --- BlinnPhong ---
            float3 toEye = normalize(gCamera.worldPosition - input.worldPosition);
            float3 halfVector = normalize(lightDir + toEye);
            float NdotH = dot(N, halfVector);
            float specularPow = pow(saturate(NdotH), gMaterial.shininess); // 反射強度
            // 鏡面反射
            specularLighting += light.color.rgb * light.intensity * specularPow * factor;
        }
    }
    // ===== SpotLight =====
    {
        for (int i = 0; i < gSpotLights.count; ++i)
        {
            // 1つ分のライト
            SpotLight light = gSpotLights.lights[i];
            
            // --- 減衰（距離と円錐） ---
            float3 toLight = light.position - input.worldPosition;
            float distance = length(toLight);
            float3 lightDir = toLight / max(distance, 0.0001f);
            float factor = SpotLightFactor(light, lightDir, distance);
            
            // --- HalfLambert ---
            float cos = pow(dot(N, lightDir) * 0.5f + 0.5f, 2.0f);
            // 拡散反射
            diffuseLighting += light.color.rgb * cos * light.intensity * factor;
            
            // --- BlinnPhong ---
            float3 toEye = normalize(gCamera.worldPosition - input.worldPosition);
            float3 halfVector = normalize(lightDir + toEye);
            float NdotH = dot(N, halfVector);
            float specularPow = pow(saturate(NdotH), gMaterial.shininess); // 反射強度
            // 鏡面反射
            specularLighting += light.color.rgb * light.intensity * specularPow * factor;
        }
    }
    
    // ===== Texture =====
    float32_t4 transformedUV = mul(float32_t4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float32_t4 texColor = gTextures[gMaterial.textureIndex].Sample(gSampler, transformedUV.xy);
    if (texColor.a <= gMaterial.alphaCutoff) // 切り抜き（alphaCutoffが0なら、ちょうど0のピクセルだけ捨てる）
    {
        discard;
    }
    
    // ===== 出力色 =====
    diffuseLighting *= gMaterial.color.rgb * texColor.rgb; // 拡散反射は物の色（色 × テクスチャ）
    specularLighting *= gMaterial.specular; // ハイライトはマテリアルの Specular の色（黒ならハイライト無し）

    output.color.rgb = diffuseLighting + specularLighting + gMaterial.emissive; // 拡散反射 + 鏡面反射 + 自己発光
    output.color.rgb = ApplyFog(output.color.rgb, input.worldPosition); // 霧（色が決まった後に混ぜる）
    output.color.a = gMaterial.color.a * texColor.a;
    if (output.color.a == 0.0)
    {
        discard;
    }
    return output;
}
#HLSL_END