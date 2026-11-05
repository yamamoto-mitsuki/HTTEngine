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
#include "ModelSurface.hlsli"
#include "Buffers/Camera.hlsli"
#include "Fog/Fog.hlsli"


PixelShaderOutput main(VertexShaderOutput input) {
    // 面の色（tint × マテリアルの色 × テクスチャ。切り抜きもここ）
    ModelSurface surface = GetModelSurface(input.texcoord);

    // Color（光を当てないので、面の色そのまま）
    float32_t3 finalColor = ApplyFog(surface.baseColor, input.worldPosition); // 霧（光を使わない物にも掛ける。背景の板も、ここを通る）

    PixelShaderOutput output;
    output.color = float32_t4(finalColor, surface.alpha);
    if (output.color.a == 0.0)
    {
        discard;
    }
    return output;
}

#HLSL_END