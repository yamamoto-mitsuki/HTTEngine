#META
stage: Include
#META_END

#HLSL
// Hitbox Telegraph（床の予告）の描き手が、描く物ごとに送る値（C++ の HitboxTelegraphData と同じ並び。16 バイトずつ）
// 予告の Shader は、"Decal/HitShape.hlsli" の後にこれを include して、当たりと同じ式で塗る
struct HitboxTelegraph
{
    float32_t3 origin;  // 形の中心（ワールド。底の真ん中）
    float radius;       // 円・扇の外側の半径
    float32_t3 right;   // 形のローカル +X（長さ 1）
    float innerRadius;  // 円・扇の内側の半径（0 なら穴なし）
    float32_t3 up;      // 形のローカル +Y（長さ 1）
    float halfAngle;    // 扇の開きの半分（ラジアン。円は π）
    float32_t3 forward; // 形のローカル +Z（長さ 1）
    float lineLength;   // 線の長さ
    float width;        // 線の幅
    float height;       // 形の高さ（底から上へ）
    uint32_t shapeType; // 形の種類（kHitShapeCircle・Sector・Line）
    uint32_t phase;     // 段階（C++ の HitboxPhase と同じ番号。1 が Warning、2 が Active）
    float progress;     // Warning の進み具合（0→1。Active の間は 1、Warning の長さが 0 なら 0 のまま）
    float remaining;    // Warning の残りの秒
    float phaseTime;    // 今の段階に入ってからの秒
    float padA;
};
ConstantBuffer<HitboxTelegraph> gDrawData : register(b22);

static const uint32_t kHitboxPhaseWarning = 1; // C++ の HitboxPhase::Warning
static const uint32_t kHitboxPhaseActive = 2;  // C++ の HitboxPhase::Active

// ワールドの点 → 形のローカル（origin が原点、向きは right・up・forward）
float32_t3 ToHitShapeLocal(float32_t3 world)
{
    float32_t3 offset = world - gDrawData.origin;
    return float32_t3(dot(offset, gDrawData.right), dot(offset, gDrawData.up), dot(offset, gDrawData.forward));
}

// 床の面での符号付き距離（外ならプラス、中ならマイナス）
float CalcTelegraphDistance(float32_t3 local)
{
    return CalcHitShapeDistance2D(gDrawData.shapeType, local.xz, gDrawData.radius, gDrawData.innerRadius, gDrawData.halfAngle, gDrawData.width, gDrawData.lineLength);
}

// 根元から先までの、どこにいるか（0 が根元、1 が先。円・扇は中心からの距離 ÷ 半径、線は前への長さ ÷ 長さ）
float CalcTelegraphReach(float32_t3 local)
{
    if (gDrawData.shapeType == kHitShapeLine)
    {
        return local.z / max(gDrawData.lineLength, 1.0e-4f);
    }
    return length(local.xz) / max(gDrawData.radius, 1.0e-4f);
}

// 根元から先までの長さ（m。進み具合の境目をなめらかにするのに使う）
float GetTelegraphReachLength()
{
    return (gDrawData.shapeType == kHitShapeLine) ? gDrawData.lineLength : gDrawData.radius;
}
#HLSL_END
