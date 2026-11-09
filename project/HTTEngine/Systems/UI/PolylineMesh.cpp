#include "PolylineMesh.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

#include "HTTEngine/Graphics/Renderer/DrawRequest.h"
#include "HTTEngine/Math/Geometry/Polygon.h"
#include "HTTEngine/Math/MathUtility.h"

namespace {
constexpr float kEdgeMargin = 2.0f;                    // 輪郭の外へ広げる幅（ピクセル。なめらかな縁がメッシュの外で切れないように。Vector Shape と同じ）
constexpr float kMinLength = 1.0e-3f;                  // これより短い辺は無いことにする（向きが決まらない）
constexpr float kMinTurn = 1.0e-3f;                    // これより小さく曲がる角は、まっすぐとして扱う（ラジアン）
constexpr float kMaxTurn = MathUtility::kPi - 2.0e-4f; // 曲がる角度の上限（ちょうど折り返すと tan が無限になるので、少し手前で止める）
constexpr float kArcStep = MathUtility::kPi * 0.25f;   // 角の扇を、この角度（45 度）ごとに分ける
constexpr uint32_t kMaxArcSteps = 4;                   // 扇を分ける数の上限（180 度 ÷ 45 度）
constexpr float kMinLoopSpacing = 1.0e-4f;             // 塗りの内側の点が、これより近ければ1つにまとめる（三角形に分けるときに、重なった点で失敗しないように）

/// <summary>
/// 続けて同じ所にある点を除いた、点の並び
/// </summary>
struct CleanPath {
	std::array<Vector2, kMaxPolylinePoints> points{}; // 点
	std::array<float, kMaxPolylinePoints> radii{};    // 角の半径
	int count = 0;                                    // 点の数
	bool isClosed = false;                            // 閉じた形（点が 3 つより少なければ、開いた線として扱う）
};

/// <summary>
/// 1つの角の形（線の太さによらない。角の円の中心・半径と、曲がる向き）
/// </summary>
struct Corner {
	Vector2 point;          // 元の点
	Vector2 incoming;       // 入ってくる辺の向き（長さ 1）
	Vector2 center;         // 角の円の中心（半径 0 なら元の点）
	float radius = 0.0f;    // 角の半径（辺に収まるように縮めた後）
	float turn = 1.0f;      // 曲がる向き（+1：左 / -1：右）
	float angle = 0.0f;     // 曲がる角度（ラジアン。0〜π）
	Vector2 inNormal;       // 入ってくる辺の、曲がる側（内側）の法線
	Vector2 outNormal;      // 出ていく辺の、曲がる側（内側）の法線
	float joinLimit = 0.0f; // 内側の線どうしが交わる点までの距離の上限（その点が辺の外へ出ないように）
};

/// <summary>
/// 1つの角を、幅 h（輪郭から外へ広げる量）で組み立てた点
/// </summary>
struct CornerRing {
	std::array<Vector2, kMaxArcSteps + 1> outer{}; // 外側の点（円に外から接する多角形の頂点。steps + 1 個）
	std::array<Vector2, kMaxArcSteps + 1> inner{}; // 内側の点（円の中心の側。isFolded でないときだけ使う。steps + 1 個）
	uint32_t steps = 0;                            // 扇を分けた数（まっすぐなら 0）
	bool isFolded = false;                         // 幅が半径より大きく、内側の輪郭が円の中心を越える
	Vector2 join;                                  // isFolded のとき、内側の2本の線が交わる点
};

/// <summary>
/// 直線の部分の端の境目（左から右へ並べた 2〜3 個の点）
/// </summary>
struct EdgeEnd {
	std::array<Vector2, 3> points{};
	uint32_t count = 0;
	void Add(const Vector2& point) { points[count++] = point; }
};

//=============================================================================
// 小さな計算
//=============================================================================
// ===== 2D の向きを左へ 90 度回す =====
Vector2 RotateLeft(const Vector2& v) { return {-v.y, v.x}; }

//=============================================================================
// 点の並びを整える
//=============================================================================
CleanPath MakeCleanPath(const PolylineShape& shape) {
	CleanPath path;
	const int count = std::clamp(shape.count, 0, kMaxPolylinePoints);
	for (int i = 0; i < count; ++i) {
		// 前の点と同じ所なら飛ばす（辺の向きが決まらない）
		if (path.count > 0 && Length(shape.points[i] - path.points[path.count - 1]) <= kMinLength) {
			continue;
		}
		path.points[path.count] = shape.points[i];
		path.radii[path.count] = shape.radii[i];
		++path.count;
	}
	path.isClosed = shape.isClosed;
	// 閉じた形で、最後の点が最初の点と同じなら、最後の点を除く
	if (path.isClosed && path.count >= 2 && Length(path.points[0] - path.points[path.count - 1]) <= kMinLength) {
		--path.count;
	}
	if (path.count < 3) {
		path.isClosed = false; // 2点を閉じても形にならない
	}
	return path;
}

//=============================================================================
// 角
//=============================================================================
// ===== 点 point の角を作る（prev・next はとなりの点。availableIn・availableOut は、角が使ってよい辺の長さ）=====
//
//        prev ──────────●tangentIn
//                         ╲  ← 半径 radius の円弧（中心 center は曲がる側）
//                          ●tangentOut
//                          │
//                         next
//   元の点から tangentIn・tangentOut までの長さは「半径 × tan(曲がる角度 ÷ 2)」。辺に収まらなければ半径を縮める
//
Corner MakeCorner(const Vector2& prev, const Vector2& point, const Vector2& next, float radius, float availableIn, float availableOut) {
	Corner corner;
	corner.point = point;
	corner.incoming = Normalize(point - prev);
	const Vector2 outgoing = Normalize(next - point);
	const float cross = Cros(corner.incoming, outgoing);
	corner.turn = (cross >= 0.0f) ? 1.0f : -1.0f;
	corner.angle = (std::min)(std::atan2(std::abs(cross), Dot(corner.incoming, outgoing)), kMaxTurn);
	corner.inNormal = RotateLeft(corner.incoming) * corner.turn;
	corner.outNormal = RotateLeft(outgoing) * corner.turn;
	const float halfAngle = corner.angle * 0.5f;
	// 内側の線どうしが交わる点を、短い方の辺の長さまでに止める（その点の、辺の上での位置が「距離 × sin(角度 ÷ 2)」）
	corner.joinLimit = (std::min)(Length(point - prev), Length(next - point)) / (std::max)(std::sin(halfAngle), 1.0e-6f);
	corner.center = point;
	if (corner.angle < kMinTurn) {
		return corner; // まっすぐ：曲げない
	}
	const float tanHalf = std::tan(halfAngle);
	const float tangent = (std::min)({(std::max)(radius, 0.0f) * tanHalf, availableIn, availableOut});
	corner.radius = tangent / tanHalf;
	corner.center = point - corner.incoming * tangent + corner.inNormal * corner.radius;
	return corner;
}

// ===== 角を、幅 h で組み立てる（h は輪郭から外へ広げる量。線なら太さの半分＋縁、塗りの縁なら縁の分だけ）=====
// 外側の点は、半径 (r + h) の円に外から接する多角形の頂点（多角形の辺が円に接するので、円を全部覆う。45 度ごとなら 1.08 倍の所）
// 内側の点は半径 (r - h)。r < h のときは内側の輪郭が中心を越える（isFolded）ので、中心と「内側の2本の線が交わる点」を使う
// isOuterEndExact：外側の両端の点を、ちょうど半径 (r + h) の所に置く（塗りの内側の輪に使う側。1.08 倍の所だと、
// 反対に曲がるとなりの角の点を越えて、輪が交差することがある。内側は塗りの内側が全部塗るので、帯が少し欠けてもよい）
CornerRing MakeRing(const Corner& corner, float h, bool isOuterEndExact = false) {
	CornerRing ring;
	ring.steps = (corner.angle < kMinTurn) ? 0u : (std::min)(static_cast<uint32_t>(std::ceil(corner.angle / kArcStep)), kMaxArcSteps);
	const float step = (ring.steps > 0) ? corner.angle / static_cast<float>(ring.steps) : 0.0f;
	const float outerRadius = (corner.radius + h) / std::cos(step * 0.5f);
	// 円の中心から見た向きを、入ってくる辺に接する点の向きから step ずつ回す（cos・sin は1回だけ求めて、回す計算を繰り返す）
	const float stepCos = std::cos(corner.turn * step);
	const float stepSin = std::sin(corner.turn * step);
	Vector2 direction = -corner.inNormal;
	ring.isFolded = corner.radius < h;
	for (uint32_t k = 0; k <= ring.steps; ++k) {
		const bool isEnd = (k == 0 || k == ring.steps);
		ring.outer[k] = corner.center + direction * ((isOuterEndExact && isEnd) ? corner.radius + h : outerRadius);
		ring.inner[k] = corner.center + direction * (corner.radius - h);
		direction = {direction.x * stepCos - direction.y * stepSin, direction.x * stepSin + direction.y * stepCos};
	}
	if (ring.isFolded) {
		// 角の二等分線の上で、元の点から h ÷ cos(角度 ÷ 2) の所（ちょうど折り返すときは、入ってきた辺を戻る向き）
		Vector2 bisector = Normalize(corner.inNormal + corner.outNormal);
		if (LengthSq(bisector) == 0.0f) {
			bisector = -corner.incoming;
		}
		const float distance = (std::min)(h / std::cos(corner.angle * 0.5f), corner.joinLimit);
		ring.join = corner.point + bisector * distance;
	}
	return ring;
}

// ===== 角と直線の部分の境目（入ってくる辺側なら点 0、出ていく辺側なら点 steps の所）を、左から右へ =====
// 曲がる側（内側）が左なら、内 → 外の順が左 → 右
EdgeEnd MakeCornerEnd(const Corner& corner, const CornerRing& ring, bool isIncoming) {
	const uint32_t k = isIncoming ? 0u : ring.steps;
	EdgeEnd end;
	std::array<Vector2, 3> innerToOuter{};
	uint32_t count = 0;
	if (ring.isFolded) {
		innerToOuter = {ring.join, corner.center, ring.outer[k]};
		count = 3;
	} else {
		innerToOuter = {ring.inner[k], ring.outer[k]};
		count = 2;
	}
	for (uint32_t i = 0; i < count; ++i) {
		end.Add(innerToOuter[(corner.turn > 0.0f) ? i : count - 1 - i]);
	}
	return end;
}

// ===== 開いた線の端と直線の部分の境目（端の点から inset だけ線の中へ入った所。左から右へ）=====
// direction は直線の部分の進む向き。inset は進む向きに＋（始まりの端は＋、終わりの端は−を渡す）
EdgeEnd MakeCapEnd(const Vector2& point, const Vector2& direction, float inset, float h) {
	const Vector2 base = point + direction * inset;
	const Vector2 side = RotateLeft(direction) * h;
	EdgeEnd end;
	end.Add(base + side);
	end.Add(base - side);
	return end;
}

//=============================================================================
// メッシュに足す
//=============================================================================
/// <summary>
/// 部分ごとに、頂点と三角形の番号を足す係
/// </summary>
class MeshWriter {
public:
	explicit MeshWriter(PolylineMeshData& mesh) : mesh_(mesh) {}

	// 凸の多角形を、最初の点からの扇で三角形に分けて足す。localOf は「位置 → その部分から見た位置」
	template<class LocalOf> void AddFan(std::span<const Vector2> polygon, PolylinePiece piece, const Vector2& param, const LocalOf& localOf) {
		const uint32_t count = static_cast<uint32_t>(polygon.size());
		if (count < 3) {
			return;
		}
		// 配列は1回で広げてから書く（1つずつ push_back するより Debug で速い。clear の後なら、広げてもメモリは取り直さない）
		const uint32_t base = static_cast<uint32_t>(mesh_.vertices.size());
		mesh_.vertices.resize(base + count);
		VertexPolylineData* vertex = mesh_.vertices.data() + base;
		for (uint32_t i = 0; i < count; ++i) {
			vertex[i] = {polygon[i], localOf(polygon[i]), param, static_cast<uint32_t>(piece)};
		}
		const size_t indexBase = mesh_.indices.size();
		mesh_.indices.resize(indexBase + (count - 2) * 3);
		uint32_t* index = mesh_.indices.data() + indexBase;
		for (uint32_t i = 1; i + 1 < count; ++i) {
			*index++ = base;
			*index++ = base + i;
			*index++ = base + i + 1;
		}
	}

	// 直線の部分：start（左→右）から end（右→左に戻る）へ回る多角形。local は (進む向き, 左へ) の距離
	void AddSegment(const EdgeEnd& start, const EdgeEnd& end, const Vector2& origin, const Vector2& direction, PolylinePiece piece, const Vector2& param) {
		std::array<Vector2, 6> polygon{};
		uint32_t count = 0;
		for (uint32_t i = 0; i < start.count; ++i) {
			polygon[count++] = start.points[i];
		}
		for (uint32_t i = end.count; i > 0; --i) {
			polygon[count++] = end.points[i - 1];
		}
		const Vector2 left = RotateLeft(direction);
		AddFan(std::span<const Vector2>(polygon.data(), count), piece, param, [&](const Vector2& p) { return Vector2{Dot(p - origin, direction), Dot(p - origin, left)}; });
	}

	// 角の部分：中心から見た位置を local に入れる（PS は円からの距離を測る）
	void AddCorner(const Corner& corner, const CornerRing& ring, PolylinePiece piece, const Vector2& param, bool usesChord) {
		const auto localOf = [&corner](const Vector2& p) { return p - corner.center; };
		if (ring.steps == 0) {
			return; // まっすぐ：角の部分は無い
		}
		// 折れている：中心からの扇（中心の近くも線の中）
		if (ring.isFolded) {
			std::array<Vector2, kMaxArcSteps + 2> fan{};
			fan[0] = corner.center;
			std::copy_n(ring.outer.begin(), ring.steps + 1, fan.begin() + 1);
			AddFan(std::span<const Vector2>(fan.data(), ring.steps + 2), piece, param, localOf);
			return;
		}
		// 内側を弦（点 0 と点 steps を結ぶ線）にする：塗りの縁で、内側を塗りの内側と1本の線で分けるとき
		if (usesChord) {
			std::array<Vector2, kMaxArcSteps + 3> polygon{};
			polygon[0] = ring.inner[0];
			std::copy_n(ring.outer.begin(), ring.steps + 1, polygon.begin() + 1);
			polygon[ring.steps + 2] = ring.inner[ring.steps];
			AddFan(std::span<const Vector2>(polygon.data(), ring.steps + 3), piece, param, localOf);
			return;
		}
		// 内側と外側の間を、45 度ごとの四角で埋める
		for (uint32_t k = 0; k < ring.steps; ++k) {
			const std::array<Vector2, 4> quad = {ring.inner[k], ring.outer[k], ring.outer[k + 1], ring.inner[k + 1]};
			AddFan(quad, piece, param, localOf);
		}
	}

	uint32_t GetIndexCount() const { return static_cast<uint32_t>(mesh_.indices.size()); }

private:
	PolylineMeshData& mesh_;
};

//=============================================================================
// 全部の角
//=============================================================================
/// <summary>
/// 折れ線の全部の角（開いた線の両端は、角ではない）
/// </summary>
struct PathCorners {
	std::array<Corner, kMaxPolylinePoints> corners{};
	float startInset = 0.0f; // 開いた線：始まりの端を、端の形の分だけ中へ入れる長さ
	float endInset = 0.0f;   // 開いた線：終わりの端を中へ入れる長さ
};

PathCorners MakeCorners(const CleanPath& path) {
	PathCorners result;
	const int n = path.count;
	const auto lengthOf = [&path, n](int from) { return Length(path.points[(from + 1) % n] - path.points[from]); };
	if (!path.isClosed) {
		// 端の形のなめらかな所（端の点の前後 kEdgeMargin）を、直線の部分と分ける。短い辺なら 4 分の 1 まで
		result.startInset = (std::min)(kEdgeMargin, lengthOf(0) * 0.25f);
		result.endInset = (std::min)(kEdgeMargin, lengthOf(n - 2) * 0.25f);
	}
	const int first = path.isClosed ? 0 : 1;
	const int last = path.isClosed ? n - 1 : n - 2;
	for (int i = first; i <= last; ++i) {
		const int prev = (i + n - 1) % n;
		const int next = (i + 1) % n;
		// 角が使ってよい長さ：となりも角なら辺の半分（2つの角で分ける）、となりが開いた線の端なら端の分を残した全部
		const float lengthIn = lengthOf(prev);
		const float lengthOut = lengthOf(i);
		const float availableIn = (!path.isClosed && prev == 0) ? lengthIn - result.startInset : lengthIn * 0.5f;
		const float availableOut = (!path.isClosed && next == n - 1) ? lengthOut - result.endInset : lengthOut * 0.5f;
		result.corners[i] = MakeCorner(path.points[prev], path.points[i], path.points[next], path.radii[i], availableIn, availableOut);
	}
	return result;
}

//=============================================================================
// 線
//=============================================================================
void BuildStroke(const CleanPath& path, const PathCorners& pathCorners, const PolylineShape& shape, MeshWriter& writer) {
	const int n = path.count;
	const float h = shape.halfWidth + kEdgeMargin;
	const auto& corners = pathCorners.corners;
	std::array<CornerRing, kMaxPolylinePoints> rings{};
	const int first = path.isClosed ? 0 : 1;
	const int last = path.isClosed ? n - 1 : n - 2;
	// ===== 角 =====
	for (int i = first; i <= last; ++i) {
		rings[i] = MakeRing(corners[i], h);
		writer.AddCorner(corners[i], rings[i], PolylinePiece::Corner, {corners[i].radius, 0.0f}, false);
	}
	// ===== 直線の部分（点 i → 点 i + 1）=====
	const int segmentCount = path.isClosed ? n : n - 1;
	for (int i = 0; i < segmentCount; ++i) {
		const int next = (i + 1) % n;
		const Vector2 direction = Normalize(path.points[next] - path.points[i]);
		const bool isFirst = !path.isClosed && i == 0;
		const bool isLast = !path.isClosed && next == n - 1;
		const EdgeEnd start = isFirst ? MakeCapEnd(path.points[i], direction, pathCorners.startInset, h) : MakeCornerEnd(corners[i], rings[i], false);
		const EdgeEnd end = isLast ? MakeCapEnd(path.points[next], direction, -pathCorners.endInset, h) : MakeCornerEnd(corners[next], rings[next], true);
		writer.AddSegment(start, end, path.points[i], direction, PolylinePiece::Segment, {0.0f, 0.0f});
	}
	if (path.isClosed) {
		return;
	}
	// ===== 開いた線の両端 =====
	// 端の点から中へ inset、外へ extent の四角。local は (端から外へ, 横へ)、param.x は端の形
	const float extent = (shape.cap == PolylineCap::Butt) ? kEdgeMargin : shape.halfWidth + kEdgeMargin;
	const Vector2 capParam = {static_cast<float>(shape.cap), 0.0f};
	const auto addCap = [&](const Vector2& point, const Vector2& outward, float inset) {
		const Vector2 side = RotateLeft(outward) * h;
		const Vector2 inside = point - outward * inset;
		const Vector2 outside = point + outward * extent;
		const std::array<Vector2, 4> quad = {inside + side, outside + side, outside - side, inside - side};
		writer.AddFan(quad, PolylinePiece::Cap, capParam, [&](const Vector2& p) { return Vector2{Dot(p - point, outward), Dot(p - point, RotateLeft(outward))}; });
	};
	addCap(path.points[0], Normalize(path.points[0] - path.points[1]), pathCorners.startInset);
	addCap(path.points[n - 1], Normalize(path.points[n - 1] - path.points[n - 2]), pathCorners.endInset);
}

//=============================================================================
// 塗り
//=============================================================================
// 縁（輪郭の内外に kEdgeMargin の帯）を距離でなめらかにし、帯の内側の輪を三角形に分けて塗る
// 失敗（辺どうしが交差して三角形に分けられない）なら false で、何も足さない
bool BuildFill(const CleanPath& path, const PathCorners& pathCorners, MeshWriter& writer, PolylineMeshData& mesh) {
	const int n = path.count;
	const float area = Polygon2D::SignedArea(std::span<const Vector2>(path.points.data(), static_cast<size_t>(n)));
	if (std::abs(area) <= kMinLength) {
		return false; // 面積が無い
	}
	const bool isInsideLeft = area > 0.0f; // 左回りなら、進む向きの左が内側
	const auto& corners = pathCorners.corners;
	std::array<CornerRing, kMaxPolylinePoints> rings{};

	// ===== 1. 帯の内側の輪（塗りの内側の多角形）=====
	// 内側へ曲がる角（とがった角）は円の中心の側の点、外側へ曲がる角（へこんだ角）は外の点を使う
	std::vector<Vector2> loop;
	loop.reserve(static_cast<size_t>(n) * (kMaxArcSteps + 1));
	const auto addLoopPoint = [&loop](const Vector2& point) {
		if (loop.empty() || Length(point - loop.back()) > kMinLoopSpacing) {
			loop.push_back(point);
		}
	};
	for (int i = 0; i < n; ++i) {
		const bool isConvex = (corners[i].turn > 0.0f) == isInsideLeft;
		rings[i] = MakeRing(corners[i], kEdgeMargin, !isConvex); // へこんだ角は、外側の点が塗りの内側の輪になる
		if (isConvex && rings[i].isFolded) {
			addLoopPoint(rings[i].join);
		} else if (isConvex) {
			addLoopPoint(rings[i].inner[0]);
			addLoopPoint(rings[i].inner[rings[i].steps]);
		} else {
			for (uint32_t k = 0; k <= rings[i].steps; ++k) {
				addLoopPoint(rings[i].outer[k]);
			}
		}
	}
	if (loop.size() >= 2 && Length(loop.front() - loop.back()) <= kMinLoopSpacing) {
		loop.pop_back();
	}
	std::vector<uint32_t> loopIndices;
	if (!Polygon2D::Triangulate(loop, loopIndices)) {
		return false;
	}

	// ===== 2. 内側（全部塗る）=====
	const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
	for (const Vector2& point : loop) {
		mesh.vertices.push_back({
		    point, {0.0f, 0.0f},
             {0.0f, 0.0f},
             static_cast<uint32_t>(PolylinePiece::FillInside)
        });
	}
	for (uint32_t index : loopIndices) {
		mesh.indices.push_back(base + index);
	}

	// ===== 3. 縁の帯（PS は符号付きの距離を測る。param.y を掛けると、塗りの外が＋になる）=====
	for (int i = 0; i < n; ++i) {
		const bool isConvex = (corners[i].turn > 0.0f) == isInsideLeft;
		writer.AddCorner(corners[i], rings[i], PolylinePiece::FillCorner, {corners[i].radius, isConvex ? 1.0f : -1.0f}, isConvex);
	}
	const Vector2 edgeParam = {0.0f, isInsideLeft ? -1.0f : 1.0f}; // 左（local.y が＋）が内側なら、符号を反対にする
	for (int i = 0; i < n; ++i) {
		const int next = (i + 1) % n;
		const Vector2 direction = Normalize(path.points[next] - path.points[i]);
		writer.AddSegment(MakeCornerEnd(corners[i], rings[i], false), MakeCornerEnd(corners[next], rings[next], true), path.points[i], direction, PolylinePiece::FillEdge, edgeParam);
	}
	return true;
}
} // namespace

//=============================================================================
// メッシュを作る
//=============================================================================
PolylineMeshInfo PolylineMeshBuilder::Build(const PolylineShape& shape, PolylineMeshData& mesh) {
	mesh.vertices.clear(); // clear は大きさ（capacity）を残すので、作り直してもメモリを取り直さない
	mesh.indices.clear();
	PolylineMeshInfo info;
	const CleanPath path = MakeCleanPath(shape);
	if (path.count < 2) {
		return info;
	}
	const PathCorners corners = MakeCorners(path);
	MeshWriter writer(mesh);
	// --- 塗りが先（同じ1回の描画で、線が塗りの上に重なるように）---
	if (shape.hasFill && path.isClosed) {
		info.isFillValid = BuildFill(path, corners, writer, mesh);
	}
	info.fillIndexCount = writer.GetIndexCount();
	if (shape.halfWidth > 0.0f) {
		BuildStroke(path, corners, shape, writer);
	}
	info.strokeIndexCount = writer.GetIndexCount() - info.fillIndexCount;
	return info;
}