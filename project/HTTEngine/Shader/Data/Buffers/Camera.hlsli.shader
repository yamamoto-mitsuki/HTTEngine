#META
stage: Include
#META_END

#HLSL
struct Camera
{
    float4x4 viewProj;        // ビュー×プロジェクション
    float32_t3 worldPosition; // カメラのワールド座標（鏡面反射用）
    float padA;
    float32_t3 right;         // カメラ右方向（ビルボード用）
    float padB;
    float32_t3 up;            // カメラ上方向（ビルボード用）
    float padC;
    // --- 霧（C++ の FogData と同じ並び。使うのは Fog.hlsli の ApplyFog）---
    float32_t3 fogColor;       // 距離の霧の色
    float fogStartDistance;    // カメラからこの距離（m）までは、距離の霧が無い
    float fogDensity;          // 距離の霧の濃さ（1m あたり）
    float fogMaxOpacity;       // 距離の霧の一番濃い所（0 なら距離の霧なし）
    float heightFogBase;       // 高さの霧が一番濃くなる高さ（ワールドの Y）
    float heightFogFalloff;    // 高さの霧が、上へ薄くなる速さ（1m あたり）
    float32_t3 heightFogColor; // 高さの霧の色
    float heightFogMaxOpacity; // 高さの霧の一番濃い所（0 なら高さの霧なし）
    // --- 画面から戻す（C++ の inverseViewProj）---
    float4x4 inverseViewProj;  // viewProj の逆行列（画面の位置と深度から、ワールドの位置へ戻す。デカールが使う）
    // --- 環境光（C++ の EnvironmentLightData と同じ並び。使うのは PBR.PS）---
    float environmentIntensity;           // 拡散（空から来る、ぼんやりした光）に掛ける
    float environmentReflectionIntensity; // 鏡面（空の映り込み）に掛ける
    float environmentPadA;
    float environmentPadB;
};
ConstantBuffer<Camera> gCamera : register(b0);
#HLSL_END