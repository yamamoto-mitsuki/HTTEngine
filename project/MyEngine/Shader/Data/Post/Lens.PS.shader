#META
stage: PS
profile : ps_6_0
#META_END

#HLSL
#include "Resources/Texture.hlsli"
#include "Post/ToneMap.hlsli"

struct LensParam
{
    float distortion; // 画面端の歪み。正で外へ膨らむ
    float aberration; // 色収差。端ほどRGBがずれる
    float radialBlur; // 中心へ向かって流れるブラー
    float vignette; // 端の暗さ
    uint32_t srcIndex;
    float windLines; // 風の線の濃さ（0 なら描かない）
    float windLineTime; // 風の線の流れる時間（秒）
    float aspect; // 画面の横 ÷ 縦
    float2 windLineDirection; // 風の線の流れる向き（画面の右・上が＋）
    float exposure;   // 露出（トーンマップの前に掛ける倍率）
    uint32_t toneMap; // トーンマップの種類（ToneMap.hlsli の kToneMap〇〇）
};
ConstantBuffer<LensParam> gLens : register(b0);

// ===== 風の線の形 =====
static const float kWindRows = 18.0f; // 画面の高さに、線の列が何本入るか
static const float kWindRowRatio = 0.55f; // 線のある列の割合（残りの列には線が無い）
static const float kWindLineWidth = 0.12f; // 線の太さ（列の幅に対する割合。真ん中から片側）
static const float kWindLineColor = 0.7f; // 線の明るさ（白へ寄せる割合。1 で真っ白）

// 0〜1 の、ばらばらに見える数（同じ n なら同じ数）。sin の大きな値の小数部分を使う、よくある作り方
float Hash(float n)
{
    return frac(sin(n * 12.9898f) * 43758.5453f);
}

// その画素の、風の線の濃さ（0〜1）
// 画面を風の向きに沿った細い列に分けて、列ごとに乱数で「線があるか・速さ・間・長さ」を決める
// 線は前（風下）の端が一番濃く、後ろへ薄くなる。列の中の位置で太さを付ける
float WindLines(float32_t2 uv)
{
    // 縦横の比をそろえた座標（画面の高さが 1）。uv の Y は下向きなので、上を＋にする
    float32_t2 p = float32_t2((uv.x - 0.5f) * gLens.aspect, 0.5f - uv.y);
    float32_t2 along = gLens.windLineDirection; // 流れる向き
    float32_t2 across = float32_t2(-along.y, along.x); // 横（左へ 90 度）
    float a = dot(p, along); // 流れる向きに測った位置
    float c = dot(p, across) * kWindRows; // 横に測った位置（列の幅が 1）
    float row = floor(c); // 何番目の列か
    // 列ごとの乱数（足す数を変えて、別々の乱数にする）
    float r0 = Hash(row);
    float r1 = Hash(row + 31.7f);
    float r2 = Hash(row + 71.3f);
    float r3 = Hash(row + 113.1f);
    float speed = lerp(1.2f, 2.4f, r1); // 流れる速さ（画面の高さ / 秒）
    float period = lerp(1.0f, 2.2f, r2); // 線が来る間（画面の高さ）
    float len = lerp(0.12f, 0.35f, r3); // 線の長さ（画面の高さ）
    // 時間で流す：a から「進んだ長さ」を引いて、period ごとにくり返す（frac）。local は線の後ろの端から前へ測った長さ
    float s = (a - gLens.windLineTime * speed) / period + r0 * 7.0f;
    float local = frac(s) * period;
    float head = (local < len) ? local / len : 0.0f; // 後ろの端 0 → 前の端 1（前ほど濃い）。線の外は 0
    // 列の中の位置で太さを付ける（真ん中が濃い。列ごとに少し上下にずらして、等間隔に見えないように）
    float inRow = frac(c) - 0.5f + (r1 - 0.5f) * 0.4f;
    float width = 1.0f - smoothstep(0.0f, kWindLineWidth, abs(inRow));
    float exists = (r0 < kWindRowRatio) ? 1.0f : 0.0f; // 線の無い列
    return head * width * exists;
}

struct VSOutput
{
    float32_t4 position : SV_Position;
    float32_t2 texcoord : TEXCOORD0;
};

float32_t4 main(VSOutput input) : SV_TARGET
{
    float32_t2 uv = input.texcoord;
    float32_t2 offset = uv - 0.5f; // 画面中心からのずれ
    float dist2 = dot(offset, offset); // 中心=0、四隅=0.5。二乗なので端だけ強く効く

    // ===== 歪み：中心はそのまま、端ほど押し出す =====
    float32_t2 warped = saturate(uv + offset * dist2 * gLens.distortion);

    // ===== 色収差：RGBを歪みと同じ向きにずらす =====
    float shift = gLens.aberration * dist2;
    float32_t3 color;
    color.r = gTextures[gLens.srcIndex].Sample(gSamplerClamp, saturate(warped + offset * shift)).r;
    color.g = gTextures[gLens.srcIndex].Sample(gSamplerClamp, warped).g;
    color.b = gTextures[gLens.srcIndex].Sample(gSamplerClamp, saturate(warped - offset * shift)).b;

    // ===== 放射ブラー：中心へ向かって数回サンプルして重ねる =====
    // 外へ向かって拾うと画面外を舐めるので、必ず中心側へ引く
    if (gLens.radialBlur > 0.001f)
    {
        float32_t3 sum = color;
        for (int i = 1; i < 6; ++i)
        {
            float t = float(i) / 5.0f;
            sum += gTextures[gLens.srcIndex].Sample(gSamplerClamp, saturate(warped - offset * (t * gLens.radialBlur))).rgb;
        }
        color = sum * (1.0f / 6.0f);
    }
    
     // ===== 露出・トーンマップ：HDR の明るさを、画面の 0〜1 に収める =====
    // ここまでは HDR のまま（明るい所ほど色収差・放射ブラーの筋も明るい）。風の線・ビネットは収めた後に掛ける（今までの見た目と同じ）
    color = ApplyToneMap(color * gLens.exposure, gLens.toneMap);
    
     // ===== 風の線：白へ寄せる（足すと明るい所で白く飛ぶので、lerp で寄せる）=====
    if (gLens.windLines > 0.001f)
    {
        // line は HLSL の予約語（ジオメトリシェーダーの「線」の型）なので、変数の名前に使えない
        float streak = WindLines(warped) * gLens.windLines;
        color = lerp(color, float32_t3(1.0f, 1.0f, 1.0f), saturate(streak * kWindLineColor));
    }

    // ===== ビネット：端を落として中心へ視線を集める =====
    color *= 1.0f - saturate(dist2 * gLens.vignette);

    return float32_t4(color, 1.0f);
}
#HLSL_END