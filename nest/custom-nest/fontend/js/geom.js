/**
 * True-shape helpers: transform, AABB, overlap, minimum outline distance.
 */
(function (global) {
    'use strict';

    var CN = global.CustomNest = global.CustomNest || {};

    function clonePts(pts) {
        var out = [], i;
        for (i = 0; i < pts.length; i++) out.push({ x: pts[i].x, y: pts[i].y });
        return out;
    }

    function bbox(pts) {
        var minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity, i;
        for (i = 0; i < pts.length; i++) {
            if (pts[i].x < minX) minX = pts[i].x;
            if (pts[i].y < minY) minY = pts[i].y;
            if (pts[i].x > maxX) maxX = pts[i].x;
            if (pts[i].y > maxY) maxY = pts[i].y;
        }
        return { x: minX, y: minY, w: maxX - minX, h: maxY - minY, r: maxX, b: maxY };
    }

    function aabbGap(a, b) {
        var dx = 0, dy = 0;
        if (a.r < b.x) dx = b.x - a.r;
        else if (b.r < a.x) dx = a.x - b.r;
        if (a.b < b.y) dy = b.y - a.b;
        else if (b.b < a.y) dy = a.y - b.b;
        return Math.sqrt(dx * dx + dy * dy);
    }

    function aabbNear(a, b, pad) {
        return !(a.r + pad < b.x || b.r + pad < a.x || a.b + pad < b.y || b.b + pad < a.y);
    }

    /** Rotate around bbox center, then sit at top-left (tx, ty). Optional X flip first. */
    function placePoly(localPts, tx, ty, rotationDeg, flipX) {
        var src = clonePts(localPts);
        var i, bb0 = bbox(src);
        var cx = bb0.x + bb0.w * 0.5;
        var cy = bb0.y + bb0.h * 0.5;
        if (flipX) {
            for (i = 0; i < src.length; i++) src[i].x = 2 * cx - src[i].x;
        }
        var rad = (rotationDeg || 0) * Math.PI / 180;
        var cos = Math.cos(rad), sin = Math.sin(rad);
        if (rotationDeg) {
            for (i = 0; i < src.length; i++) {
                var dx = src[i].x - cx, dy = src[i].y - cy;
                src[i].x = cx + dx * cos - dy * sin;
                src[i].y = cy + dx * sin + dy * cos;
            }
        }
        var bb = bbox(src);
        var ox = tx - bb.x, oy = ty - bb.y;
        for (i = 0; i < src.length; i++) {
            src[i].x += ox;
            src[i].y += oy;
        }
        var outBb = bbox(src);
        return { points: src, bbox: outBb };
    }

    function translatePoly(pts, dx, dy) {
        var out = [], i;
        for (i = 0; i < pts.length; i++) out.push({ x: pts[i].x + dx, y: pts[i].y + dy });
        return { points: out, bbox: bbox(out) };
    }

    function pointInPoly(p, pts) {
        var inside = false, i, j, xi, yi, xj, yj;
        for (i = 0, j = pts.length - 1; i < pts.length; j = i++) {
            xi = pts[i].x; yi = pts[i].y;
            xj = pts[j].x; yj = pts[j].y;
            if (((yi > p.y) !== (yj > p.y)) &&
                (p.x < (xj - xi) * (p.y - yi) / ((yj - yi) || 1e-12) + xi)) {
                inside = !inside;
            }
        }
        return inside;
    }

    function segsIntersect(a, b, c, d) {
        function cross(p, q, r) {
            return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x);
        }
        function onSeg(p, q, r) {
            return q.x <= Math.max(p.x, r.x) + 1e-8 && q.x >= Math.min(p.x, r.x) - 1e-8 &&
                q.y <= Math.max(p.y, r.y) + 1e-8 && q.y >= Math.min(p.y, r.y) - 1e-8;
        }
        var d1 = cross(a, b, c), d2 = cross(a, b, d), d3 = cross(c, d, a), d4 = cross(c, d, b);
        if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) &&
            ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))) return true;
        if (Math.abs(d1) < 1e-8 && onSeg(a, c, b)) return true;
        if (Math.abs(d2) < 1e-8 && onSeg(a, d, b)) return true;
        if (Math.abs(d3) < 1e-8 && onSeg(c, a, d)) return true;
        if (Math.abs(d4) < 1e-8 && onSeg(c, b, d)) return true;
        return false;
    }

    function polygonsIntersect(A, B) {
        var i, j, n = A.length, m = B.length;
        for (i = 0; i < n - 1; i++) {
            for (j = 0; j < m - 1; j++) {
                if (segsIntersect(A[i], A[i + 1], B[j], B[j + 1])) return true;
            }
        }
        var stepA = Math.max(1, Math.floor(n / 16));
        var stepB = Math.max(1, Math.floor(m / 16));
        for (i = 0; i < n; i += stepA) {
            if (pointInPoly(A[i], B)) return true;
        }
        for (j = 0; j < m; j += stepB) {
            if (pointInPoly(B[j], A)) return true;
        }
        return false;
    }

    function distPointSeg(p, a, b) {
        var dx = b.x - a.x, dy = b.y - a.y;
        var len2 = dx * dx + dy * dy;
        if (len2 < 1e-16) {
            dx = p.x - a.x; dy = p.y - a.y;
            return Math.sqrt(dx * dx + dy * dy);
        }
        var t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        dx = p.x - (a.x + t * dx);
        dy = p.y - (a.y + t * dy);
        return Math.sqrt(dx * dx + dy * dy);
    }

    /**
     * Outline-to-outline gap. 0 = overlap / touch interior.
     * Far AABB pairs skip the O(n·m) loop.
     */
    function minDistance(A, B) {
        var ga = aabbGap(A.bbox, B.bbox);
        if (ga > 4) return ga;
        if (polygonsIntersect(A.points, B.points)) return 0;
        var min = Infinity, i, j;
        var Ap = A.points, Bp = B.points;
        for (i = 0; i < Ap.length; i++) {
            for (j = 0; j < Bp.length - 1; j++) {
                var d = distPointSeg(Ap[i], Bp[j], Bp[j + 1]);
                if (d < min) min = d;
            }
        }
        for (i = 0; i < Bp.length; i++) {
            for (j = 0; j < Ap.length - 1; j++) {
                var d2 = distPointSeg(Bp[i], Ap[j], Ap[j + 1]);
                if (d2 < min) min = d2;
            }
        }
        return min;
    }

    function violatesGap(piece, obstacles, minGap) {
        var i, o, d;
        for (i = 0; i < obstacles.length; i++) {
            o = obstacles[i];
            if (!aabbNear(piece.bbox, o.bbox, minGap + 0.35)) continue;
            d = minDistance(piece, o);
            if (d < minGap - 1e-4) return true;
        }
        return false;
    }

    CN.geom = {
        bbox: bbox,
        aabbGap: aabbGap,
        aabbNear: aabbNear,
        placePoly: placePoly,
        translatePoly: translatePoly,
        minDistance: minDistance,
        violatesGap: violatesGap,
        polygonsIntersect: polygonsIntersect
    };
})(typeof window !== 'undefined' ? window : globalThis);
