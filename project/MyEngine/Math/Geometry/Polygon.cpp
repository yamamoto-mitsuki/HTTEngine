#include "Polygon.h"

#include <cmath>
#include <cstddef>
#include <numeric>
#include <algorithm>

namespace Polygon2D {
namespace {
constexpr float kMinEdgeLengthSq = 1e-8f; // これより短い辺は「長さ 0」（同じ所に点が2つある）
constexpr float kMinArea = 1e-6f;         // これより小さい面積は「面積 0」（点が一直線に並んでいる）

// 3 点の回り方。＋なら a → b → c が左回り（反時計回り）、−なら右回り、0 なら一直線
float Orient(const Vector2& a, const Vector2& b, const Vector2& c) { return Cros(b - a, c - a); }

// a・b・c が一直線のとき、c が線分 ab の上（両端も入れる）にあるか
bool IsOnSegment(const Vector2& a, const Vector2& b, const Vector2& c) {
	return (std::min)(a.x, b.x) <= c.x && c.x <= (std::max)(a.x, b.x) && (std::min)(a.y, b.y) <= c.y && c.y <= (std::max)(a.y, b.y);
}

// 線分 p1p2 と q1q2 が交わるか（端が触れているだけでも交わる）
// 線分 q の両端が、線分 p をはさんで反対側にあり（回り方の符号が違う）、逆も同じなら交わっている
bool SegmentsIntersect(const Vector2& p1, const Vector2& p2, const Vector2& q1, const Vector2& q2) {
	const float d1 = Orient(q1, q2, p1);
	const float d2 = Orient(q1, q2, p2);
	const float d3 = Orient(p1, p2, q1);
	const float d4 = Orient(p1, p2, q2);
	if (((d1 > 0.0f && d2 < 0.0f) || (d1 < 0.0f && d2 > 0.0f)) && ((d3 > 0.0f && d4 < 0.0f) || (d3 < 0.0f && d4 > 0.0f))) {
		return true;
	}
	// 一直線に並んだ点が、相手の線分の上にある（端が触れている・重なっている）
	return (d1 == 0.0f && IsOnSegment(q1, q2, p1)) || (d2 == 0.0f && IsOnSegment(q1, q2, p2)) || (d3 == 0.0f && IsOnSegment(p1, p2, q1)) || (d4 == 0.0f && IsOnSegment(p1, p2, q2));
}

// 線分 ab の上で point に一番近い点（Collision::ClosestPointOnSegment の 2D 版）
Vector2 ClosestPointOnSegment(const Vector2& point, const Vector2& a, const Vector2& b) {
	const Vector2 ab = b - a;
	const float lengthSq = LengthSq(ab);
	if (lengthSq < kMinEdgeLengthSq) {
		return a; // 長さ0の辺（点）
	}
	const float t = std::clamp(Dot(point - a, ab) / lengthSq, 0.0f, 1.0f);
	return a + ab * t;
}

// point が三角形 abc（反時計回り）の中か。辺の上も中に入れる（耳の中に別の点が「触れている」だけでも、切り取らない）
bool IsPointInTriangle(const Vector2& point, const Vector2& a, const Vector2& b, const Vector2& c) { return Orient(a, b, point) >= 0.0f && Orient(b, c, point) >= 0.0f && Orient(c, a, point) >= 0.0f; }
} // namespace

//=============================================================================
// 面積・中か・一番近い辺
//=============================================================================
float SignedArea(std::span<const Vector2> points) {
	// 靴ひもの公式：辺ごとに「原点・始点・終点の三角形」の符号付きの面積を足す（外へはみ出した分は、逆向きの三角形で打ち消される）
	float twiceArea = 0.0f;
	for (size_t i = 0; i < points.size(); ++i) {
		twiceArea += Cros(points[i], points[(i + 1) % points.size()]);
	}
	return twiceArea * 0.5f;
}

bool IsPointInside(const Vector2& point, std::span<const Vector2> points) {
	// point から右へ線を伸ばし、辺と交わる回数を数える。奇数なら中（へこんだ形でも同じ）
	bool isInside = false;
	for (size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
		const Vector2& a = points[i];
		const Vector2& b = points[j];
		// 辺 ab が point の高さをまたいでいる（片方だけが上）なら、その高さで辺の X を出し、point より右か見る
		if ((a.y > point.y) != (b.y > point.y)) {
			const float crossX = a.x + (point.y - a.y) * (b.x - a.x) / (b.y - a.y);
			if (point.x < crossX) {
				isInside = !isInside;
			}
		}
	}
	return isInside;
}

Vector2 ClosestPointOnEdges(const Vector2& point, std::span<const Vector2> points, size_t* edgeIndex) {
	Vector2 closest = points.empty() ? point : points[0];
	float closestDistanceSq = -1.0f; // まだ無い
	for (size_t i = 0; i < points.size(); ++i) {
		const Vector2 onEdge = ClosestPointOnSegment(point, points[i], points[(i + 1) % points.size()]);
		const float distanceSq = LengthSq(point - onEdge);
		if (closestDistanceSq < 0.0f || distanceSq < closestDistanceSq) {
			closest = onEdge;
			closestDistanceSq = distanceSq;
			if (edgeIndex != nullptr) {
				*edgeIndex = i;
			}
		}
	}
	return closest;
}

Vector2 EdgeOutwardNormal(std::span<const Vector2> points, size_t edgeIndex) {
	// 辺の向き (dx, dy) の右側 (dy, -dx)。反時計回りの多角形では、右側が外
	const Vector2 edge = points[(edgeIndex + 1) % points.size()] - points[edgeIndex];
	return Normalize(Vector2{edge.y, -edge.x});
}

//=============================================================================
// 形がおかしくないか
//=============================================================================
bool IsSimple(std::span<const Vector2> points) {
	const size_t count = points.size();
	if (count < 3 || std::abs(SignedArea(points)) < kMinArea) {
		return false;
	}
	// --- 長さ 0 の辺（同じ所に点が2つ）---
	for (size_t i = 0; i < count; ++i) {
		if (LengthSq(points[(i + 1) % count] - points[i]) < kMinEdgeLengthSq) {
			return false;
		}
	}
	// --- 隣り合っていない辺どうしが交わっていないか（隣の辺とは、点を共有しているので必ず触れている）---
	for (size_t i = 0; i < count; ++i) {
		for (size_t j = i + 2; j < count; ++j) {
			if (i == 0 && j == count - 1) {
				continue; // 最後の辺と最初の辺は隣り合っている（点 0 を共有）
			}
			if (SegmentsIntersect(points[i], points[(i + 1) % count], points[j], points[(j + 1) % count])) {
				return false;
			}
		}
	}
	return true;
}

//=============================================================================
// 三角形に分ける（耳切り法）
//=============================================================================
// 点を毎フレーム動かす形（Polyline の塗り）でも使うので、調べる量を減らしてある
// ・切り取った点は、前と次の番号（prev・next）をつなぎ替えて飛ばす（配列から消して詰めない）
// ・耳の中に入りうるのは、へこんだ角の点だけ（とがった角の点が入るなら、辺どうしが交差している）。なので、へこんだ点だけ調べる
// ・切り取ったら、最初からではなく、切った所の手前から調べ直す
bool Triangulate(std::span<const Vector2> points, std::vector<uint32_t>& indices) {
	const uint32_t count = static_cast<uint32_t>(points.size());
	if (count < 3) {
		return false;
	}
	// --- 点の番号を反時計回りに並べ、となりの点のつながりを作る（位置 i の点は points[order[i]]）---
	std::vector<uint32_t> order(count);
	std::iota(order.begin(), order.end(), 0u); // 0, 1, 2, … と順に入れる
	if (SignedArea(points) < 0.0f) {
		std::ranges::reverse(order); // 時計回りなら逆に並べる
	}
	std::vector<uint32_t> prev(count);
	std::vector<uint32_t> next(count);
	for (uint32_t i = 0; i < count; ++i) {
		prev[i] = (i + count - 1) % count;
		next[i] = (i + 1) % count;
	}
	const auto at = [&](uint32_t i) -> const Vector2& { return points[order[i]]; };
	// へこんだ角（右回り）・一直線の角か。となりの点を切り取ると変わるので、そのたびに調べ直す
	const auto calcReflex = [&](uint32_t i) { return Orient(at(prev[i]), at(i), at(next[i])) <= 0.0f; };
	std::vector<uint8_t> isReflex(count);
	for (uint32_t i = 0; i < count; ++i) {
		isReflex[i] = calcReflex(i);
	}
	// 耳か：とがった角で、三角形の中（辺の上も入れる）に、ほかのへこんだ点が無い
	const auto isEar = [&](uint32_t i) {
		if (isReflex[i]) {
			return false;
		}
		const Vector2& a = at(prev[i]);
		const Vector2& b = at(i);
		const Vector2& c = at(next[i]);
		for (uint32_t other = next[next[i]]; other != prev[i]; other = next[other]) {
			if (isReflex[other] && IsPointInTriangle(at(other), a, b, c)) {
				return false;
			}
		}
		return true;
	};
	// 点 i を切り離す（前と次をつなぎ、となりの角の形を調べ直す）
	const auto unlink = [&](uint32_t i) {
		next[prev[i]] = next[i];
		prev[next[i]] = prev[i];
		isReflex[prev[i]] = calcReflex(prev[i]);
		isReflex[next[i]] = calcReflex(next[i]);
	};

	// --- 耳を1つずつ切り取る（点が n 個なら、三角形は n-2 個）---
	// 耳：となりの2点と結んだ三角形が、とがっていて（左回り）、中にほかの点が入っていない角。切り取っても、残りは多角形のまま
	uint32_t remaining = count;
	uint32_t current = 0;
	uint32_t missCount = 0; // 続けて耳でなかった点の数（1周しても耳が無いかを数える）
	while (remaining > 3) {
		if (isEar(current)) {
			indices.insert(indices.end(), {order[prev[current]], order[current], order[next[current]]});
			const uint32_t before = prev[current];
			unlink(current);
			--remaining;
			current = before; // 切った所の手前から調べ直す（形が変わったのは、となりの2点だけ）
			missCount = 0;
			continue;
		}
		current = next[current];
		if (++missCount < remaining) {
			continue;
		}
		// --- 1周しても耳が無い：一直線の角（三角形にしても面積 0）があれば、三角形を作らずに外す。それも無ければ形がおかしい ---
		uint32_t straight = current;
		while (Orient(at(prev[straight]), at(straight), at(next[straight])) != 0.0f) {
			straight = next[straight];
			if (straight == current) {
				return false;
			}
		}
		current = prev[straight];
		unlink(straight);
		--remaining;
		missCount = 0;
	}
	indices.insert(indices.end(), {order[prev[current]], order[current], order[next[current]]});
	return true;
}
} // namespace Polygon2D