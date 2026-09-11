#pragma once
#include <cmath>
#include <vector>

namespace pf {

struct Pt {
    double x = 0, y = 0;
};

struct BBox {
    double x = 0, y = 0, w = 0, h = 0, r = 0, b = 0;
};

inline BBox bboxOf(const std::vector<Pt>& pts) {
    BBox bb;
    if (pts.empty()) return bb;
    double minX = pts[0].x, minY = pts[0].y, maxX = pts[0].x, maxY = pts[0].y;
    for (size_t i = 1; i < pts.size(); i++) {
        if (pts[i].x < minX) minX = pts[i].x;
        if (pts[i].y < minY) minY = pts[i].y;
        if (pts[i].x > maxX) maxX = pts[i].x;
        if (pts[i].y > maxY) maxY = pts[i].y;
    }
    bb.x = minX; bb.y = minY; bb.r = maxX; bb.b = maxY;
    bb.w = maxX - minX; bb.h = maxY - minY;
    return bb;
}

inline double aabbGap(const BBox& a, const BBox& b) {
    double dx = 0, dy = 0;
    if (a.r < b.x) dx = b.x - a.r;
    else if (b.r < a.x) dx = a.x - b.r;
    if (a.b < b.y) dy = b.y - a.b;
    else if (b.b < a.y) dy = a.y - b.b;
    return std::sqrt(dx * dx + dy * dy);
}

inline bool aabbNear(const BBox& a, const BBox& b, double pad) {
    return !(a.r + pad < b.x || b.r + pad < a.x || a.b + pad < b.y || b.b + pad < a.y);
}

struct Poly {
    std::vector<Pt> points;
    BBox bbox;
};

inline Poly placePoly(const std::vector<Pt>& localPts, double tx, double ty, double rotationDeg, bool flipX) {
    std::vector<Pt> src = localPts;
    BBox bb0 = bboxOf(src);
    double cx = bb0.x + bb0.w * 0.5;
    double cy = bb0.y + bb0.h * 0.5;
    if (flipX) {
        for (size_t i = 0; i < src.size(); i++) src[i].x = 2 * cx - src[i].x;
    }
    if (rotationDeg != 0) {
        const double rad = rotationDeg * 3.14159265358979323846 / 180.0;
        const double c = std::cos(rad), s = std::sin(rad);
        for (size_t i = 0; i < src.size(); i++) {
            double dx = src[i].x - cx, dy = src[i].y - cy;
            src[i].x = cx + dx * c - dy * s;
            src[i].y = cy + dx * s + dy * c;
        }
    }
    BBox bb = bboxOf(src);
    double ox = tx - bb.x, oy = ty - bb.y;
    for (size_t i = 0; i < src.size(); i++) {
        src[i].x += ox;
        src[i].y += oy;
    }
    Poly out;
    out.points = std::move(src);
    out.bbox = bboxOf(out.points);
    return out;
}

inline Poly translatePoly(const std::vector<Pt>& pts, double dx, double dy) {
    Poly out;
    out.points.resize(pts.size());
    for (size_t i = 0; i < pts.size(); i++) {
        out.points[i].x = pts[i].x + dx;
        out.points[i].y = pts[i].y + dy;
    }
    out.bbox = bboxOf(out.points);
    return out;
}

inline bool pointInPoly(const Pt& p, const std::vector<Pt>& pts) {
    bool inside = false;
    size_t n = pts.size();
    if (n < 3) return false;
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        double xi = pts[i].x, yi = pts[i].y, xj = pts[j].x, yj = pts[j].y;
        if (((yi > p.y) != (yj > p.y)) &&
            (p.x < (xj - xi) * (p.y - yi) / ((yj - yi) != 0 ? (yj - yi) : 1e-12) + xi)) {
            inside = !inside;
        }
    }
    return inside;
}

inline double cross3(const Pt& p, const Pt& q, const Pt& r) {
    return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x);
}

inline bool onSeg(const Pt& p, const Pt& q, const Pt& r) {
    return q.x <= std::max(p.x, r.x) + 1e-8 && q.x >= std::min(p.x, r.x) - 1e-8 &&
           q.y <= std::max(p.y, r.y) + 1e-8 && q.y >= std::min(p.y, r.y) - 1e-8;
}

inline bool segsIntersect(const Pt& a, const Pt& b, const Pt& c, const Pt& d) {
    double d1 = cross3(a, b, c), d2 = cross3(a, b, d), d3 = cross3(c, d, a), d4 = cross3(c, d, b);
    if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) &&
        ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))) return true;
    if (std::fabs(d1) < 1e-8 && onSeg(a, c, b)) return true;
    if (std::fabs(d2) < 1e-8 && onSeg(a, d, b)) return true;
    if (std::fabs(d3) < 1e-8 && onSeg(c, a, d)) return true;
    if (std::fabs(d4) < 1e-8 && onSeg(c, b, d)) return true;
    return false;
}

inline bool polygonsIntersect(const std::vector<Pt>& A, const std::vector<Pt>& B) {
    size_t n = A.size(), m = B.size();
    if (n < 2 || m < 2) return false;
    for (size_t i = 0; i + 1 < n; i++) {
        for (size_t j = 0; j + 1 < m; j++) {
            if (segsIntersect(A[i], A[i + 1], B[j], B[j + 1])) return true;
        }
    }
    size_t stepA = std::max<size_t>(1, n / 16);
    size_t stepB = std::max<size_t>(1, m / 16);
    for (size_t i = 0; i < n; i += stepA) {
        if (pointInPoly(A[i], B)) return true;
    }
    for (size_t j = 0; j < m; j += stepB) {
        if (pointInPoly(B[j], A)) return true;
    }
    return false;
}

inline double distPointSeg(const Pt& p, const Pt& a, const Pt& b) {
    double dx = b.x - a.x, dy = b.y - a.y;
    double len2 = dx * dx + dy * dy;
    if (len2 < 1e-16) {
        dx = p.x - a.x; dy = p.y - a.y;
        return std::sqrt(dx * dx + dy * dy);
    }
    double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    dx = p.x - (a.x + t * dx);
    dy = p.y - (a.y + t * dy);
    return std::sqrt(dx * dx + dy * dy);
}

/** Lower bound: distance from point to an axis-aligned box. */
inline double pointToBBoxDist(const Pt& p, const BBox& b) {
    double dx = 0, dy = 0;
    if (p.x < b.x) dx = b.x - p.x;
    else if (p.x > b.r) dx = p.x - b.r;
    if (p.y < b.y) dy = b.y - p.y;
    else if (p.y > b.b) dy = p.y - b.b;
    return std::sqrt(dx * dx + dy * dy);
}

/**
 * Same boolean as (minDistance(A,B) < thresh). Early-outs only when the
 * exact test cannot differ: AABB gap already ≥ thresh, or a hit is found.
 */
inline bool closerThan(const Poly& A, const Poly& B, double thresh) {
    const double ga = aabbGap(A.bbox, B.bbox);
    if (ga >= thresh) return false;
    if (ga <= 1e-9 && polygonsIntersect(A.points, B.points)) return true;
    auto hits = [&](const std::vector<Pt>& pts, const std::vector<Pt>& segs, const BBox& segBox) -> bool {
        for (size_t i = 0; i < pts.size(); i++) {
            if (pointToBBoxDist(pts[i], segBox) >= thresh) continue;
            for (size_t j = 0; j + 1 < segs.size(); j++) {
                if (distPointSeg(pts[i], segs[j], segs[j + 1]) < thresh) return true;
            }
        }
        return false;
    };
    return hits(A.points, B.points, B.bbox) || hits(B.points, A.points, A.bbox);
}

inline double minDistance(const Poly& A, const Poly& B) {
    double ga = aabbGap(A.bbox, B.bbox);
    if (ga > 4) return ga;
    if (polygonsIntersect(A.points, B.points)) return 0;
    double min = 1e300;
    for (size_t i = 0; i < A.points.size(); i++) {
        for (size_t j = 0; j + 1 < B.points.size(); j++) {
            double d = distPointSeg(A.points[i], B.points[j], B.points[j + 1]);
            if (d < min) min = d;
        }
    }
    for (size_t i = 0; i < B.points.size(); i++) {
        for (size_t j = 0; j + 1 < A.points.size(); j++) {
            double d = distPointSeg(B.points[i], A.points[j], A.points[j + 1]);
            if (d < min) min = d;
        }
    }
    return min;
}

inline bool violatesGap(const Poly& piece, const std::vector<const Poly*>& obstacles, double minGap) {
    for (size_t i = 0; i < obstacles.size(); i++) {
        const Poly* o = obstacles[i];
        if (!aabbNear(piece.bbox, o->bbox, minGap + 0.35)) continue;
        if (closerThan(piece, *o, minGap - 1e-4)) return true;
    }
    return false;
}

} // namespace pf
