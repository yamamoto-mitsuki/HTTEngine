#META
stage:   PS
profile: ps_6_0
#META_END

#PROGRAM
category: Model
shading:  HalfLambert
vs:       Object3dVS
#PROGRAM_END

#HLSL
#include "Object3d.hlsli"
#include "ModelSurface.hlsli"
#include "Buffers/Camera.hlsli"
#include "Buffers/Light.hlsli"
#include "Fog/Fog.hlsli"

// 円周率
static const float PI = 3.14159265f;


PixelShaderOutput main(VertexShaderOutput input) {
    // 面の色（tint × マテリアルの色 × テクスチャ。切り抜いたピクセルは、光の計算の前に捨てる）
    ModelSurface surface = GetModelSurface(input.texcoord);

    PixelShaderOutput output;
    float3 N = normalize(input.normal);
    // 最終的な拡散光
    float3 diffuseLighting = { 0.0f, 0.0f, 0.0f };
    
    // ===== DirectionlLight =====
    {
        float3 lightDir = normalize(-gDirectionalLight.direction);
        float cos = pow(dot(N, lightDir) * 0.5f + 0.5f, 2.0f);
        
        diffuseLighting += gDirectionalLight.color.rgb * gDirectionalLight.intensity * cos;
    }
    
    // ===== PointLights =====
    {
        for (int i = 0; i < gPointLights.count;  ++i)
        {
            PointLight light = gPointLights.lights[i];
            float3 lightDir = normalize(input.worldPosition - light.position);
            lightDir = normalize(light.position - input.worldPosition);
            float cos = pow(dot(N, lightDir) * 0.5f + 0.5f, 2.0f);
            // 減衰
            float radius = max(light.radius, 0.0001f);
            float decay = max(light.decay, 0.0f);
            float distance = length(light.position - input.worldPosition);
            float factor = pow(saturate(-distance / radius + 1.0f), decay);
            
            diffuseLighting += light.color.rgb * light.intensity * cos * factor;
        }
    }
    // ===== SpotLights =====
     {
        for (int i = 0; i < gSpotLights.count; ++i)
        {
            SpotLight light = gSpotLights.lights[i];
            float3 toLight = light.position - input.worldPosition;
            float distance = length(toLight);
            float3 lightDir = toLight / max(distance, 0.0001f);
            float cos = pow(dot(N, lightDir) * 0.5f + 0.5f, 2.0f);
            // 減衰（距離と円錐）
            float factor = SpotLightFactor(light, lightDir, distance);
            
            diffuseLighting += light.color.rgb * light.intensity * cos * factor;
        }
    }
     
    // Output
    output.color.rgb = surface.baseColor * diffuseLighting + gMaterial.emissive;
    output.color.rgb = ApplyFog(output.color.rgb, input.worldPosition); // 霧（色が決まった後に混ぜる）
    output.color.a = surface.alpha;
    if (output.color.a == 0.0)
    {
        discard;
    }
    
    return output;
}

#HLSL_END
