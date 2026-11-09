#META
stage:   PS
profile: ps_6_0
#META_END

#HLSL
TextureCube<float4> gEnv : register(t0);
SamplerState gSampler : register(s0);

struct VSOutput
{
    float4 pos : SV_Position;
    float3 dir : TEXCOORD0;
};

// 円周率
static const float PI = 3.14159265359f;


float4 main(VSOutput input) : SV_TARGET
{
    float3 N = normalize(input.dir);
    
    // Nを軸にした接空間を作る（真上付近の破綻を回避）
    float3 up = abs(N.y) < 0.999f ? float3(0, 1, 0) : float3(0, 0, 1);
    float3 right = normalize(cross(up, N));
    up = normalize(cross(N, right));
    
    float3 irradiance = float3(0, 0, 0);
    float sampleDelta = 0.05f;
    float nrSamples = 0.0f;
    
    // 半球（φ:0..2π, θ:0..π/2）をサンプル
    for (float phi = 0.0f; phi < 2.0f * PI; phi += sampleDelta)
    {
        for (float theta = 0.0f; theta < 0.5f * PI; theta += sampleDelta)
        {
			// 接空間の方向 → ワールド
            float3 t = float3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            float3 sampleVec = t.x * right + t.y * up + t.z * N;
			// 動的ループなのでSampleLevel(…,0)。Sample()は勾配未定義でNG
            irradiance += gEnv.SampleLevel(gSampler, sampleVec, 0).rgb * cos(theta) * sin(theta);
            nrSamples++;
        }
    }
    irradiance = PI * irradiance * (1.0f / nrSamples);
    return float4(irradiance, 1.0f);
}
#HLSL_END