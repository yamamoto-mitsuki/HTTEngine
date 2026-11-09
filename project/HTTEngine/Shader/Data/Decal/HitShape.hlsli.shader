#META
stage: Include
#META_END

#HLSL
// 攻撃の形の、床の面（ローカルの XZ）の中での符号付き距離（形の外なら縁までの距離、中ならマイナス）
// C++ の Math/Geometry/HitShape.cpp（CalcSectorDistance・CalcLineDistance・WorldHitShape::CalcSignedDistance2D）と同じ式
// 当たり・ジャスト判定・ギズモと同じ式で塗るので、塗った所と当たる所がずれない。どちらかを直したら、もう片方も同じに直す

static const float kHitShapePi = 3.14159265f; // 円周率（円は「開き 360 度の扇」＝開きの半分が π）

// 形の種類（C++ の HitShapeType と同じ番号。床に置く形だけ）
static const uint32_t kHitShapeCircle = 0; // 円（内側の半径でドーナツ）
static const uint32_t kHitShapeSector = 1; // 扇（前 +Z を真ん中に開く）
static const uint32_t kHitShapeLine = 2;   // 線（幅 width の長方形。z = 0 から length まで）

// ===== 円・ドーナツ・扇・扇のドーナツ（どれも「半径 ri〜R、前 +Z から ±a の範囲」の形）=====
// 左右が同じ形なので、x を正の側にそろえて考える。q＝(|x|, z)、φ＝q の +Z からの角度
//   φ ≦ a（開きの中）：外側の弧・内側の弧・横の辺のうち、一番近い物までの距離
//   φ ＞ a（開きの外）：横の辺（ri・e 〜 R・e の線分。e は開きの端の向き）までの距離
float CalcSectorDistance(float32_t2 p, float outerRadius, float innerRadius, float halfAngle)
{
    float32_t2 q = float32_t2(abs(p.x), p.y);
    float radial = length(q);
    // 弧だけを見た距離（中ならマイナス）。内側の弧は、内側の半径があるときだけ
    float radialDistance = (innerRadius > 0.0f) ? max(radial - outerRadius, innerRadius - radial) : radial - outerRadius;
    if (halfAngle >= kHitShapePi)
    {
        return radialDistance; // 円・ドーナツ（横の辺が無い）
    }
    // 横の辺（開きの端の向き e に、内側の半径から外側の半径までの線分）までの距離
    float32_t2 edge = float32_t2(sin(halfAngle), cos(halfAngle));
    float along = clamp(dot(q, edge), innerRadius, outerRadius);
    float edgeDistance = length(q - edge * along);
    float phi = atan2(q.x, q.y); // +Z からの角度（0〜π）
    if (phi > halfAngle)
    {
        return edgeDistance; // 開きの外：一番近いのは横の辺
    }
    if (radialDistance > 0.0f)
    {
        return radialDistance; // 開きの中で、弧の外
    }
    return -min(-radialDistance, edgeDistance); // 中：一番近い縁までの距離をマイナスで
}

// ===== 線（幅 width の長方形。z = 0 から length まで）=====
float CalcLineDistance(float32_t2 p, float width, float lineLength)
{
    float32_t2 halfSize = float32_t2(width * 0.5f, lineLength * 0.5f);
    float32_t2 d = float32_t2(abs(p.x) - halfSize.x, abs(p.y - halfSize.y) - halfSize.y);
    return length(max(d, 0.0f)) + min(max(d.x, d.y), 0.0f);
}

// ===== 種類で分ける（p はローカルの (x, z)）=====
float CalcHitShapeDistance2D(uint32_t shapeType, float32_t2 p, float radius, float innerRadius, float halfAngle, float width, float lineLength)
{
    if (shapeType == kHitShapeLine)
    {
        return CalcLineDistance(p, width, lineLength);
    }
    return CalcSectorDistance(p, radius, innerRadius, halfAngle); // 円も扇も同じ式（円は開きの半分が π）
}
#HLSL_END
