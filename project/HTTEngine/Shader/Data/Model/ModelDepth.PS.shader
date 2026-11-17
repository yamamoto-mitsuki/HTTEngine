#META
stage: PS
profile: ps_6_0
#META_END

#HLSL
// 深度だけのパス（深度プリパス・影の深度）のPS。色は出さず、色を塗るPSと同じピクセルを捨てるだけ
// VSは物のShaderのVSをそのまま使う
#include "Object3d.hlsli"
#include "ModelSurface.hlsli"

void main(VertexShaderOutput input) {
    ModelSurface surface = GetModelSurface(input.texcoord);
    if (surface.alpha == 0.0f) {
        discard;
    }
}
#HLSL_END