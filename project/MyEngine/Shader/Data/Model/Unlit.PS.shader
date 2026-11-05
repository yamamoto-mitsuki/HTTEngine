#META
stage:   PS
profile: ps_6_0
#META_END

#PROGRAM
category: Model
shading:  Unlit
vs:       Object3dVS
#PROGRAM_END

#HLSL
#include "Object3d.hlsli"
#include "Buffers/Camera.hlsli"
#include "Fog/Fog.hlsli"
#include "Resources/Texture.hlsli"

struct ModelMaterial {
    float32_t4   color;
    float4x4 uvTransform;
    float32_t3   ambient;
    float alphaCutoff;
    float32_t3   diffuse;    
    float padD;
    float32_t3   specular;   
    float shininess;
    float32_t3   emissive;   
    uint textureIndex;
};
ConstantBuffer<ModelMaterial> gMaterial : register(b20);


PixelShaderOutput main(VertexShaderOutput input) {
    // Texture
    float32_t4 transformedUV = mul(float32_t4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float32_t4 texColor      = gTextures[gMaterial.textureIndex].Sample(gSampler, transformedUV.xy);
    if (texColor.a <= gMaterial.alphaCutoff) // 切り抜き（alphaCutoffが0なら、ちょうど0のピクセルだけ捨てる）
    {
        discard;
    }
    
    // Color
    float32_t3 finalColor = gMaterial.color.rgb * texColor.rgb;
    float       finalAlpha = gMaterial.color.a * texColor.a;
    finalColor = ApplyFog(finalColor, input.worldPosition); // 霧（光を使わない物にも掛ける。背景の板も、ここを通る）
    
    PixelShaderOutput output;
    output.color = float32_t4(finalColor, finalAlpha);
    if (output.color.a == 0.0)
    {
        discard;
    }
    return output;
}

#HLSL_END