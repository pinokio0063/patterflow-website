/**
 * SVG path → closed polygon (inches after grade).
 * Flatten Beziers, then Ramer–Douglas–Peucker so true-gap tests stay fast.
 */
(function (global) {
    'use strict';

    var CN = global.CustomNest = global.CustomNest || {};

    var PATH_TOKEN = /([MmLlHhVvCcSsQqTtAaZz])|([+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)/g;

    function tokenize(d) {
        var tokens = [];
        var m;
        PATH_TOKEN.lastIndex = 0;
        while ((m = PATH_TOKEN.exec(d))) {
            if (m[1]) tokens.push({ t: 'c', v: m[1] });
            else tokens.push({ t: 'n', v: parseFloat(m[2]) });
        }
        return tokens;
    }

    function lerp(a, b, t) { return a + (b - a) * t; }

    function cubicPoint(p0, p1, p2, p3, t) {
        var u = 1 - t;
        return {
            x: u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x + t * t * t * p3.x,
            y: u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y + t * t * t * p3.y
        };
    }

    function quadPoint(p0, p1, p2, t) {
        var u = 1 - t;
        return {
            x: u * u * p0.x + 2 * u * t * p1.x + t * t * p2.x,
            y: u * u * p0.y + 2 * u * t * p1.y + t * t * p2.y
        };
    }

    function flattenCubic(out, p0, p1, p2, p3, steps) {
        var i;
        for (i = 1; i <= steps; i++) out.push(cubicPoint(p0, p1, p2, p3, i / steps));
    }

    function flattenQuad(out, p0, p1, p2, steps) {
        var i;
        for (i = 1; i <= steps; i++) out.push(quadPoint(p0, p1, p2, i / steps));
    }

    function dist(a, b) {
        var dx = a.x - b.x, dy = a.y - b.y;
        return Math.sqrt(dx * dx + dy * dy);
    }

    function curveSteps(len, minSteps, maxSteps) {
        var n = Math.ceil(len / 8);
        if (n < minSteps) n = minSteps;
        if (n > maxSteps) n = maxSteps;
        return n;
    }

    /**
     * Convert one SVG path `d` into a list of subpath point arrays (SVG units).
     */
    function pathToSubpaths(d) {
        var tokens = tokenize(d || '');
        var i = 0;
        var cmd = 'M';
        var cx = 0, cy = 0, sx = 0, sy = 0;
        var pcx = 0, pcy = 0;
        var lastCubic = false;
        var lastQuad = false;
        var sub = [];
        var subs = [];

        function num() {
            if (i >= tokens.length || tokens[i].t !== 'n') return 0;
            return tokens[i++].v;
        }

        function pushPt(x, y) {
            cx = x; cy = y;
            sub.push({ x: x, y: y });
        }

        function closeSub() {
            if (sub.length > 2) {
                var a = sub[0], b = sub[sub.length - 1];
                if (dist(a, b) > 1e-4) sub.push({ x: a.x, y: a.y });
                subs.push(sub);
            }
            sub = [];
        }

        while (i < tokens.length) {
            if (tokens[i].t === 'c') {
                cmd = tokens[i++].v;
            }
            if (cmd === 'Z' || cmd === 'z') {
                pushPt(sx, sy);
                closeSub();
                cx = sx; cy = sy;
                lastCubic = false;
                lastQuad = false;
                continue;
            }
            if (cmd === 'M' || cmd === 'm') {
                closeSub();
                var mx = num(), my = num();
                if (cmd === 'm') { mx += cx; my += cy; }
                pushPt(mx, my);
                sx = mx; sy = my;
                lastCubic = false;
                lastQuad = false;
                cmd = cmd === 'M' ? 'L' : 'l';
                continue;
            }
            if (cmd === 'L' || cmd === 'l') {
                var lx = num(), ly = num();
                if (cmd === 'l') { lx += cx; ly += cy; }
                pushPt(lx, ly);
                lastCubic = false;
                lastQuad = false;
                continue;
            }
            if (cmd === 'H' || cmd === 'h') {
                var hx = num();
                if (cmd === 'h') hx += cx;
                pushPt(hx, cy);
                lastCubic = false;
                lastQuad = false;
                continue;
            }
            if (cmd === 'V' || cmd === 'v') {
                var vy = num();
                if (cmd === 'v') vy += cy;
                pushPt(cx, vy);
                lastCubic = false;
                lastQuad = false;
                continue;
            }
            if (cmd === 'C' || cmd === 'c') {
                var x1 = num(), y1 = num(), x2 = num(), y2 = num(), x = num(), y = num();
                if (cmd === 'c') { x1 += cx; y1 += cy; x2 += cx; y2 += cy; x += cx; y += cy; }
                var p0 = { x: cx, y: cy }, p1 = { x: x1, y: y1 }, p2 = { x: x2, y: y2 }, p3 = { x: x, y: y };
                flattenCubic(sub, p0, p1, p2, p3, curveSteps(dist(p0, p1) + dist(p1, p2) + dist(p2, p3), 6, 24));
                cx = x; cy = y;
                pcx = x2; pcy = y2;
                lastCubic = true;
                lastQuad = false;
                continue;
            }
            if (cmd === 'S' || cmd === 's') {
                var x2s = num(), y2s = num(), xs = num(), ys = num();
                if (cmd === 's') { x2s += cx; y2s += cy; xs += cx; ys += cy; }
                /* SVG: S reflects only if the previous command was C or S. */
                var x1s = lastCubic ? (2 * cx - pcx) : cx;
                var y1s = lastCubic ? (2 * cy - pcy) : cy;
                var s0 = { x: cx, y: cy }, s1 = { x: x1s, y: y1s }, s2 = { x: x2s, y: y2s }, s3 = { x: xs, y: ys };
                flattenCubic(sub, s0, s1, s2, s3, curveSteps(dist(s0, s1) + dist(s1, s2) + dist(s2, s3), 6, 24));
                cx = xs; cy = ys;
                pcx = x2s; pcy = y2s;
                lastCubic = true;
                lastQuad = false;
                continue;
            }
            if (cmd === 'Q' || cmd === 'q') {
                var qx1 = num(), qy1 = num(), qx = num(), qy = num();
                if (cmd === 'q') { qx1 += cx; qy1 += cy; qx += cx; qy += cy; }
                var q0 = { x: cx, y: cy }, q1 = { x: qx1, y: qy1 }, q2 = { x: qx, y: qy };
                flattenQuad(sub, q0, q1, q2, curveSteps(dist(q0, q1) + dist(q1, q2), 5, 18));
                cx = qx; cy = qy;
                pcx = qx1; pcy = qy1;
                lastQuad = true;
                lastCubic = false;
                continue;
            }
            if (cmd === 'T' || cmd === 't') {
                var tx = num(), ty = num();
                if (cmd === 't') { tx += cx; ty += cy; }
                var tx1 = lastQuad ? (2 * cx - pcx) : cx;
                var ty1 = lastQuad ? (2 * cy - pcy) : cy;
                var t0 = { x: cx, y: cy }, t1 = { x: tx1, y: ty1 }, t2 = { x: tx, y: ty };
                flattenQuad(sub, t0, t1, t2, curveSteps(dist(t0, t1) + dist(t1, t2), 5, 18));
                cx = tx; cy = ty;
                pcx = tx1; pcy = ty1;
                lastQuad = true;
                lastCubic = false;
                continue;
            }
            if (cmd === 'A' || cmd === 'a') {
                num(); num(); num(); num(); num();
                var ax = num(), ay = num();
                if (cmd === 'a') { ax += cx; ay += cy; }
                pushPt(ax, ay);
                lastCubic = false;
                lastQuad = false;
                continue;
            }
            i += 1;
        }
        closeSub();
        return subs;
    }

    function bboxOf(pts) {
        var minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
        var i;
        for (i = 0; i < pts.length; i++) {
            if (pts[i].x < minX) minX = pts[i].x;
            if (pts[i].y < minY) minY = pts[i].y;
            if (pts[i].x > maxX) maxX = pts[i].x;
            if (pts[i].y > maxY) maxY = pts[i].y;
        }
        return { x: minX, y: minY, w: maxX - minX, h: maxY - minY };
    }

    function areaAbs(pts) {
        var a = 0, i, j;
        for (i = 0, j = pts.length - 1; i < pts.length; j = i++) {
            a += pts[j].x * pts[i].y - pts[i].x * pts[j].y;
        }
        return Math.abs(a) * 0.5;
    }

    function rdp(points, epsilon) {
        if (points.length < 3) return points.slice();
        var first = points[0], last = points[points.length - 1];
        var idx = -1, maxD = 0, i;
        for (i = 1; i < points.length - 1; i++) {
            var d = pointLineDist(points[i], first, last);
            if (d > maxD) { maxD = d; idx = i; }
        }
        if (maxD > epsilon && idx !== -1) {
            var left = rdp(points.slice(0, idx + 1), epsilon);
            var right = rdp(points.slice(idx), epsilon);
            return left.slice(0, -1).concat(right);
        }
        return [first, last];
    }

    function pointLineDist(p, a, b) {
        var dx = b.x - a.x, dy = b.y - a.y;
        var len2 = dx * dx + dy * dy;
        if (len2 < 1e-12) return dist(p, a);
        var t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
        if (t < 0) t = 0;
        if (t > 1) t = 1;
        return dist(p, { x: a.x + t * dx, y: a.y + t * dy });
    }

    function simplifyClosed(pts, epsilon) {
        if (pts.length < 8) return pts;
        var closed = pts[0];
        var inner = pts.slice(0, -1);
        var simp = rdp(inner.concat([inner[0]]), epsilon);
        if (simp.length && dist(simp[0], simp[simp.length - 1]) > 1e-6) simp.push({ x: simp[0].x, y: simp[0].y });
        if (simp.length < 4) return pts;
        return simp;
    }

    function extractSvgMeta(svgText) {
        var idMatch = /<svg[^>]*\bid\s*=\s*["']([^"']+)["']/i.exec(svgText || '');
        var vbMatch = /viewBox\s*=\s*["']\s*([^\s"']+)\s+([^\s"']+)\s+([^\s"']+)\s+([^\s"']+)/i.exec(svgText || '');
        var paths = [];
        var re = /<path\b[^>]*\bd\s*=\s*["']([^"']+)["']/gi;
        var m;
        while ((m = re.exec(svgText || ''))) paths.push(m[1]);
        return {
            name: idMatch ? idMatch[1] : '',
            pathCount: paths.length,
            paths: paths,
            viewBox: vbMatch ? {
                x: parseFloat(vbMatch[1]),
                y: parseFloat(vbMatch[2]),
                w: parseFloat(vbMatch[3]),
                h: parseFloat(vbMatch[4])
            } : null
        };
    }

    /**
     * Largest closed outline from an SVG string, in raw SVG units.
     */
    function svgToOutline(svgText) {
        var meta = extractSvgMeta(svgText);
        var best = null, bestA = 0, i, s, j, pts;
        for (i = 0; i < meta.paths.length; i++) {
            var subs = pathToSubpaths(meta.paths[i]);
            for (s = 0; s < subs.length; s++) {
                pts = subs[s];
                var a = areaAbs(pts);
                if (a > bestA) { bestA = a; best = pts; }
            }
        }
        if (!best) return null;
        var bb = bboxOf(best);
        if (bb.w < 1e-6 || bb.h < 1e-6) return null;
        var simplified = simplifyClosed(best, Math.max(bb.w, bb.h) * 0.0025);
        var outBb = bboxOf(simplified);
        return {
            name: meta.name || 'PATH',
            pathCount: meta.pathCount,
            points: simplified,
            bbox: outBb,
            viewBox: meta.viewBox,
            /* Illustrator SVG: 72 user units = 1 inch. Chart size is not assumed. */
            inchW: outBb.w / 72,
            inchH: outBb.h / 72
        };
    }

    /** Fit outline bbox to chart W×H (non-uniform), SVG units → inches. */
    function gradeOutline(outline, targetW, targetH) {
        var bb = outline.bbox;
        var sx = targetW / bb.w;
        var sy = targetH / bb.h;
        var pts = [];
        var i;
        for (i = 0; i < outline.points.length; i++) {
            pts.push({
                x: (outline.points[i].x - bb.x) * sx,
                y: (outline.points[i].y - bb.y) * sy
            });
        }
        return {
            name: outline.name,
            points: pts,
            bbox: { x: 0, y: 0, w: targetW, h: targetH }
        };
    }

    CN.path = {
        svgToOutline: svgToOutline,
        extractSvgMeta: extractSvgMeta,
        gradeOutline: gradeOutline,
        bboxOf: bboxOf
    };
})(typeof window !== 'undefined' ? window : globalThis);
