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
#include "ModelSurface.hlsli"
#include "Buffers/Camera.hlsli"
#include "Buffers/Light.hlsli"
#include "Fog/Fog.hlsli"

static const float PI = 3.141596535f;


PixelShaderOutput main(VertexShaderOutput input)
{
    // 面の色（tint × マテリアルの色 × テクスチャ。切り抜いたピクセルは、光の計算の前に捨てる）
    ModelSurface surface = GetModelSurface(input.texcoord);

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

    float32_t4 finalColor;
    finalColor.rgb = surface.baseColor * diffuseLighting + gMaterial.emissive;
    finalColor.a = surface.alpha;

    PixelShaderOutput output;
    output.color = finalColor;
    output.color.rgb = ApplyFog(output.color.rgb, input.worldPosition); // 霧（色が決まった後に混ぜる）
    if (output.color.a == 0.0) { discard; }
    return output;
}
#HLSL_END