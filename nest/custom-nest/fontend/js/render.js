/**
 * Canvas draw: item thumbnails + marker pages (true-shape outlines).
 */
(function (global) {
    'use strict';

    var CN = global.CustomNest = global.CustomNest || {};

    var COLORS = {
        FRONT: { fill: 'rgba(46, 196, 182, 0.55)', stroke: '#2ec4b6' },
        FONT: { fill: 'rgba(46, 196, 182, 0.55)', stroke: '#2ec4b6' },
        BACK: { fill: 'rgba(61, 139, 253, 0.50)', stroke: '#3d8bfd' },
        SLEEVE: { fill: 'rgba(244, 162, 97, 0.55)', stroke: '#f4a261' }
    };

    function drawPoly(ctx, pts, fill, stroke, lineW) {
        if (!pts || pts.length < 2) return;
        ctx.beginPath();
        ctx.moveTo(pts[0].x, pts[0].y);
        var i;
        for (i = 1; i < pts.length; i++) ctx.lineTo(pts[i].x, pts[i].y);
        ctx.closePath();
        if (fill) { ctx.fillStyle = fill; ctx.fill(); }
        if (stroke) {
            ctx.strokeStyle = stroke;
            ctx.lineWidth = lineW || 1;
            ctx.stroke();
        }
    }

    function thumbnail(canvas, outline, colorKey, label) {
        if (!canvas || !outline) return;
        var ctx = canvas.getContext('2d');
        var w = canvas.width, h = canvas.height;
        ctx.clearRect(0, 0, w, h);
        ctx.fillStyle = '#141414';
        ctx.fillRect(0, 0, w, h);
        var bb = outline.bbox;
        var pad = 16;
        var sx = (w - pad * 2) / (bb.w || 1);
        var sy = (h - pad * 2 - 22) / (bb.h || 1);
        var s = Math.min(sx, sy);
        ctx.save();
        ctx.translate(pad + (w - pad * 2 - bb.w * s) * 0.5, pad);
        ctx.scale(s, s);
        ctx.translate(-bb.x, -bb.y);
        var c = COLORS[colorKey] || COLORS.FRONT;
        drawPoly(ctx, outline.points, c.fill, c.stroke, 2 / s);
        ctx.restore();
        ctx.fillStyle = '#f2f2f2';
        ctx.font = '700 13px Segoe UI, sans-serif';
        ctx.textAlign = 'center';
        ctx.fillText(label || outline.name || colorKey, w / 2, h - 8);
    }

    function fitView(pages, dia, viewW, viewH, pageFilter) {
        var list = pages || [];
        if (pageFilter > 0) list = list.filter(function (p) { return p.index === pageFilter; });
        var gap = 2.4;
        var totalH = 0, i;
        for (i = 0; i < list.length; i++) totalH += Math.max(list[i].height, 4) + 3.2;
        if (!totalH) totalH = 20;
        var worldW = dia + 6;
        var worldH = totalH + 4;
        var s = Math.min((viewW - 40) / worldW, (viewH - 40) / worldH);
        if (s > 14) s = 14;
        if (s < 2) s = 2;
        return { s: s, list: list, gap: gap };
    }

    function viewSize(canvas) {
        return {
            w: canvas._cssW || canvas.clientWidth || canvas.width,
            h: canvas._cssH || canvas.clientHeight || canvas.height
        };
    }

    function paint(canvas, result, pageFilter, hoverLabel) {
        if (!canvas) return;
        var ctx = canvas.getContext('2d');
        var vs = viewSize(canvas);
        var w = vs.w, h = vs.h;
        ctx.clearRect(0, 0, w, h);
        ctx.fillStyle = '#0e0e0e';
        ctx.fillRect(0, 0, w, h);

        if (!result || !result.ok || !result.pages.length) {
            ctx.fillStyle = '#666';
            ctx.font = '15px Segoe UI, sans-serif';
            ctx.textAlign = 'center';
            ctx.fillText(result && result.error ? result.error : 'Load parts + job, then Simulate.', w / 2, h / 2);
            return;
        }

        var view = fitView(result.pages, result.dia, w, h, pageFilter);
        var s = view.s;
        var yInch = 1.6;
        var i, p, j, piece;

        ctx.save();
        ctx.translate(28, 24);

        for (i = 0; i < view.list.length; i++) {
            p = view.list[i];
            var ph = Math.max(p.height, 4);
            var ox = 0;
            var oy = yInch * s;
            var pw = result.dia * s;
            var pph = ph * s;

            ctx.fillStyle = '#161616';
            ctx.strokeStyle = '#3a3a3a';
            ctx.lineWidth = 1;
            ctx.beginPath();
            ctx.rect(ox, oy, pw, pph);
            ctx.fill();
            ctx.stroke();

            ctx.fillStyle = 'rgba(255,80,80,0.12)';
            ctx.fillRect(ox + pw, oy, 8, pph);
            ctx.fillStyle = '#ff6b6b';
            ctx.font = '10px Segoe UI';
            ctx.textAlign = 'left';
            ctx.fillText('DIA ' + result.dia.toFixed(2) + '"', ox + pw + 12, oy + 12);

            ctx.fillStyle = '#8ad7c1';
            ctx.font = '700 12px Segoe UI';
            ctx.fillText('DOC ' + p.index + '   rows ' + p.rowFrom + '–' + p.rowTo +
                '   ' + p.height.toFixed(2) + ' in   ' + (p.height * 0.0254).toFixed(3) + ' m',
                ox, oy - 6);

            ctx.save();
            ctx.beginPath();
            ctx.rect(ox, oy, pw, pph);
            ctx.clip();
            ctx.translate(ox, oy);
            ctx.scale(s, s);
            ctx.translate(0, -p.contentY0);

            var all = p.bodies.concat(p.sleeves);
            for (j = 0; j < all.length; j++) {
                piece = all[j];
                var c = COLORS[piece.kind] || COLORS.FRONT;
                drawPoly(ctx, piece.points, c.fill, c.stroke, 1.1 / s);
                var lx = piece.bbox.x + piece.bbox.w * 0.5;
                var ly = piece.bbox.y + piece.bbox.h * 0.5;
                ctx.fillStyle = '#ffffff';
                ctx.font = (10 / s) + 'px Segoe UI';
                ctx.textAlign = 'center';
                ctx.textBaseline = 'middle';
                ctx.fillText(piece.label || piece.kind, lx, ly);
            }
            ctx.restore();

            yInch += ph + 3.2;
        }
        ctx.restore();

        if (hoverLabel) {
            ctx.fillStyle = '#fff';
            ctx.font = '12px Segoe UI';
            ctx.textAlign = 'left';
            ctx.fillText(hoverLabel, 16, h - 12);
        }
    }

    function pickPiece(canvas, result, pageFilter, mx, my) {
        if (!result || !result.ok) return null;
        var vs = viewSize(canvas);
        var w = vs.w, h = vs.h;
        var view = fitView(result.pages, result.dia, w, h, pageFilter);
        var s = view.s;
        var yInch = 1.6;
        var i, p, j, piece;
        var lx = mx - 28, ly = my - 24;
        for (i = 0; i < view.list.length; i++) {
            p = view.list[i];
            var ph = Math.max(p.height, 4);
            var ox = 0, oy = yInch * s;
            var all = p.bodies.concat(p.sleeves);
            for (j = 0; j < all.length; j++) {
                piece = all[j];
                var px = ox + piece.bbox.x * s;
                var py = oy + (piece.bbox.y - p.contentY0) * s;
                var pw = piece.bbox.w * s;
                var phh = piece.bbox.h * s;
                if (lx >= px && lx <= px + pw && ly >= py && ly <= py + phh) {
                    return piece.label + '  ' + piece.kind + '  ' +
                        piece.bbox.w.toFixed(2) + '×' + piece.bbox.h.toFixed(2) + ' in';
                }
            }
            yInch += ph + 3.2;
        }
        return null;
    }

    function esc(s) {
        return String(s || '').replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;');
    }

    function polyToD(pts) {
        if (!pts || !pts.length) return '';
        var d = 'M ' + pts[0].x.toFixed(5) + ' ' + pts[0].y.toFixed(5);
        var i;
        for (i = 1; i < pts.length; i++) {
            d += ' L ' + pts[i].x.toFixed(5) + ' ' + pts[i].y.toFixed(5);
        }
        return d + ' Z';
    }

    function pieceFill(piece) {
        if (piece.overlap) return '#ff2d2d';
        if (piece.kind === 'BACK') return '#3d8bfd';
        if (piece.kind === 'SLEEVE') return '#f4a261';
        return '#2ec4b6';
    }

    /** Engine X = across DIA, Y = along roll. Preview: rotate so fabric runs left→right like Sparrow. */
    function toStrip(x, y, contentY0, xOff, yOff) {
        return { x: xOff + (y - contentY0), y: yOff + x };
    }

    function rotPoly(pts, contentY0, xOff, yOff) {
        var out = [], i;
        for (i = 0; i < (pts || []).length; i++) {
            out.push(toStrip(pts[i].x, pts[i].y, contentY0, xOff, yOff));
        }
        return out;
    }

    /**
     * One SVG, units = inches. Horizontal strip (DIA = height), same as Sparrow preview.
     */
    function buildSvg(result, pageFilter) {
        if (!result || !result.ok) {
            return '<?xml version="1.0" encoding="UTF-8"?>\n<svg xmlns="http://www.w3.org/2000/svg" width="8in" height="4in" viewBox="0 0 8 4"><text x="0.4" y="2" font-size="0.28" fill="#666">No simulation</text></svg>';
        }
        var pages = result.pages || [];
        if (pageFilter > 0) {
            pages = pages.filter(function (p) { return p.index === pageFilter; });
        }
        var dia = result.dia;
        var gap = 1.25;
        var titleH = 0.45;
        var i;
        var totalW = 0.3;
        for (i = 0; i < pages.length; i++) {
            totalW += Math.max(pages[i].height, 1) + gap;
        }
        if (!pages.length) totalW = 8;
        var W = totalW + 0.2;
        var H = titleH + dia + 0.35;
        var out = [];
        out.push('<?xml version="1.0" encoding="UTF-8"?>');
        out.push('<svg xmlns="http://www.w3.org/2000/svg" version="1.1"');
        out.push(' width="' + W.toFixed(4) + 'in" height="' + H.toFixed(4) + 'in"');
        out.push(' viewBox="0 0 ' + W.toFixed(4) + ' ' + H.toFixed(4) + '"');
        out.push(' xml:space="preserve">');
        out.push('<desc>PatternFlow Custom Nest. Horizontal strip: X = fabric length, Y = DIA. Inches.</desc>');
        out.push('<rect width="' + W.toFixed(4) + '" height="' + H.toFixed(4) + '" fill="#ffffff"/>');

        var x = 0.15;
        var yStrip = titleH;
        var p, j, piece, all, ph, c, name;
        for (i = 0; i < pages.length; i++) {
            p = pages[i];
            ph = Math.max(p.height, 1);
            out.push('<g id="DOC-' + p.index + '">');
            out.push('<text x="' + x.toFixed(4) + '" y="0.30" font-family="Arial, sans-serif" font-size="0.28" fill="#222">DOC '
                + p.index + '  rows ' + p.rowFrom + '–' + p.rowTo + '  H='
                + p.height.toFixed(3) + 'in  (' + (p.height * 0.0254).toFixed(3) + 'm)  DIA='
                + dia.toFixed(2) + 'in  gap=' + result.minGap.toFixed(3) + 'in</text>');
            out.push('<rect x="' + x.toFixed(4) + '" y="' + yStrip.toFixed(4) + '" width="' + ph.toFixed(4)
                + '" height="' + dia.toFixed(4) + '" fill="#f7f7f7" stroke="#444" stroke-width="0.015"/>');
            out.push('<line x1="' + x.toFixed(4) + '" y1="' + (yStrip + dia).toFixed(4) + '" x2="'
                + (x + ph).toFixed(4) + '" y2="' + (yStrip + dia).toFixed(4)
                + '" stroke="#ff4d4d" stroke-width="0.03"/>');
            all = p.bodies.concat(p.sleeves);
            for (j = 0; j < all.length; j++) {
                piece = all[j];
                var fill = pieceFill(piece);
                name = (piece.label || piece.kind) + ' ' + piece.bbox.w.toFixed(2) + 'x' + piece.bbox.h.toFixed(2) + 'in';
                out.push('<path id="' + esc(piece.label || piece.kind) + '-' + j + '"');
                out.push(' d="' + polyToD(rotPoly(piece.points, p.contentY0, x, yStrip)) + '"');
                out.push(' fill="' + fill + '" fill-opacity="0.42" stroke="' + (piece.overlap ? '#b00000' : '#111')
                    + '" stroke-width="0.02"');
                out.push(' data-kind="' + esc(piece.kind) + '" data-size="' + esc(piece.size) + '"');
                out.push(' data-width="' + piece.bbox.w.toFixed(4) + '" data-height="' + piece.bbox.h.toFixed(4) + '">');
                out.push('<title>' + esc(name) + (piece.overlap ? ' OVERLAY' : '') + '</title>');
                out.push('</path>');
                c = toStrip(
                    piece.bbox.x + piece.bbox.w * 0.5,
                    piece.bbox.y + piece.bbox.h * 0.5,
                    p.contentY0, x, yStrip
                );
                out.push('<text x="' + c.x.toFixed(4) + '" y="' + c.y.toFixed(4) + '"');
                out.push(' font-family="Arial, sans-serif" font-size="0.22" text-anchor="middle" fill="#111">'
                    + esc(piece.label || piece.kind)
                    + (piece.rotation != null ? ' ' + piece.rotation + '°' : '')
                    + '</text>');
            }
            out.push('</g>');
            x += ph + gap;
        }
        out.push('</svg>');
        return out.join('\n');
    }

    CN.render = {
        thumbnail: thumbnail,
        paint: paint,
        pickPiece: pickPiece,
        buildSvg: buildSvg,
        COLORS: COLORS
    };
})(typeof window !== 'undefined' ? window : globalThis);
