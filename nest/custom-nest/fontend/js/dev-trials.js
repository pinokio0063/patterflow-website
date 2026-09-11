/**
 * DEV ONLY — visual trial recorder UI.
 * Public cut: remove this file, its <script>, and the #devTrials panel in index.html.
 *
 * Default canvas = final nest only.
 * Recorded: each snapshot is its own card. DOCs inside a card sit side-by-side.
 * Colored try ghosts: green = sat, orange = fail, red = overlay.
 */
(function (global) {
    'use strict';

    var list = [];
    var showing = false;

    function $(id) { return document.getElementById(id); }

    function esc(s) {
        return String(s || '').replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;');
    }

    function polyToD(pts, dx, dy) {
        if (!pts || !pts.length) return '';
        dx = dx || 0;
        dy = dy || 0;
        var d = 'M ' + (pts[0].x + dx).toFixed(5) + ' ' + (pts[0].y + dy).toFixed(5);
        var i;
        for (i = 1; i < pts.length; i++) {
            d += ' L ' + (pts[i].x + dx).toFixed(5) + ' ' + (pts[i].y + dy).toFixed(5);
        }
        return d + ' Z';
    }

    function bind() {
        var n = $('devTrialCount');
        if (n) n.addEventListener('click', function () {
            if (!list.length) return;
            showing = !showing;
            n.classList.toggle('on', showing);
            n.textContent = showing
                ? (list.length + ' recorded · showing')
                : (list.length + ' recorded');
            var el = $('devTrialLabel');
            if (el) {
                el.textContent = showing
                    ? 'Green sit · orange fail · red overlay · each DOC in its own card'
                    : 'ফাইনাল নেস্ট শুধু। recorded = আলাদা DOC কার্ড + রঙিন ট্রাই।';
            }
            if (global.CustomNest && global.CustomNest._repaint) global.CustomNest._repaint(true);
        });
    }

    function attach(result) {
        list = (result && result.devTrials) ? result.devTrials : [];
        showing = false;
        var n = $('devTrialCount');
        if (n) {
            n.classList.remove('on');
            n.textContent = list.length ? (list.length + ' recorded') : 'none';
            n.title = list.length ? 'Click: separate DOC cards + colored tries' : '';
        }
        var el = $('devTrialLabel');
        if (el) el.textContent = list.length
            ? 'ফাইনাল নেস্ট শুধু। recorded চাপলে আলাদা DOC কার্ড — সবুজ/কমলা/লাল ট্রাই।'
            : 'কোনো ট্রায়াল নেই';
    }

    function showingRecorded() { return showing && list.length > 0; }

    function pieceFill(pc) {
        if (pc.overlap) return '#ff2d2d';
        if (pc.kind === 'BACK') return '#3d8bfd';
        if (pc.kind === 'SLEEVE') return '#f4a261';
        return '#2ec4b6';
    }

    function tryColor(tr) {
        if (tr.overlay) return '#ff2d2d';
        if (tr.ok) return '#00c853';
        return '#ff9800';
    }

    function pagesOf(rec, result) {
        if (rec && rec.final) return result.pages || [];
        if (rec && rec.pages && rec.pages.length) return rec.pages;
        return [{ index: 1, rowFrom: 0, rowTo: 0, height: 8, contentY0: 0, bodies: [], sleeves: [] }];
    }

    function triesOf(rec) {
        return (rec && rec.tries) ? rec.tries : [];
    }

    function pageIndexForTry(pages, tr) {
        var cy = (tr.y || 0) + (tr.h || 0) * 0.5;
        var best = 0, bestD = 1e9, i, mid, d, top, bot;
        for (i = 0; i < pages.length; i++) {
            top = pages[i].contentY0 || 0;
            bot = top + (pages[i].height || 0);
            if (cy >= top - 0.2 && cy <= bot + 0.2) return i;
            mid = (top + bot) * 0.5;
            d = Math.abs(cy - mid);
            if (d < bestD) { bestD = d; best = i; }
        }
        return best;
    }

    function drawTry(out, tr, ox, dy) {
        var col = tryColor(tr);
        var dash = tr.ok ? '0.12 0.08' : '0.18 0.10';
        if (tr.points && tr.points.length > 1) {
            out.push('<path d="' + polyToD(tr.points, ox, dy) + '" fill="' + col
                + '" fill-opacity="' + (tr.ok ? '0.22' : '0.30') + '" stroke="' + col
                + '" stroke-width="0.07" stroke-dasharray="' + dash + '"/>');
        } else if (tr.w > 0 && tr.h > 0) {
            out.push('<rect x="' + (ox + tr.x).toFixed(4) + '" y="' + (tr.y + dy).toFixed(4)
                + '" width="' + tr.w.toFixed(4) + '" height="' + tr.h.toFixed(4)
                + '" fill="' + col + '" fill-opacity="0.28" stroke="' + col
                + '" stroke-width="0.07" stroke-dasharray="' + dash + '"/>');
        }
        if (tr.w > 0) {
            out.push('<text x="' + (ox + tr.x + tr.w * 0.5).toFixed(4) + '" y="'
                + (tr.y + dy + tr.h * 0.5).toFixed(4)
                + '" font-family="Arial, sans-serif" font-size="0.20" font-weight="700" text-anchor="middle" fill="'
                + col + '">' + esc((tr.label || '') + ' ' + (tr.reason || (tr.ok ? 'sit' : 'fail')))
                + '</text>');
        }
    }

    function drawSheet(out, page, tries, ox, oy, dia) {
        var titleH = 0.48;
        var ph = Math.max(page.height || 0, 6);
        var y = oy;
        out.push('<rect x="' + (ox - 0.12).toFixed(4) + '" y="' + (y - 0.08).toFixed(4)
            + '" width="' + (dia + 0.24).toFixed(4) + '" height="' + (titleH + ph + 0.28).toFixed(4)
            + '" fill="#ffffff" stroke="#111" stroke-width="0.07"/>');
        out.push('<rect x="' + (ox - 0.12).toFixed(4) + '" y="' + (y - 0.08).toFixed(4)
            + '" width="' + (dia + 0.24).toFixed(4) + '" height="' + titleH.toFixed(4)
            + '" fill="#2b3a4a"/>');
        out.push('<text x="' + (ox + dia * 0.5).toFixed(4) + '" y="' + (y + 0.28).toFixed(4)
            + '" font-family="Arial, sans-serif" font-size="0.26" font-weight="700" text-anchor="middle" fill="#fff">DOC '
            + (page.index || 1) + '   rows ' + (page.rowFrom || 0) + '–' + (page.rowTo || 0)
            + '   H=' + (page.height || 0).toFixed(2) + 'in</text>');
        y += titleH;
        out.push('<rect x="' + ox.toFixed(4) + '" y="' + y.toFixed(4) + '" width="' + dia.toFixed(4)
            + '" height="' + ph.toFixed(4) + '" fill="#f4f6f8" stroke="#888" stroke-width="0.02"/>');
        out.push('<line x1="' + (ox + dia).toFixed(4) + '" y1="' + y.toFixed(4) + '" x2="'
            + (ox + dia).toFixed(4) + '" y2="' + (y + ph).toFixed(4)
            + '" stroke="#ff4d4d" stroke-width="0.04"/>');
        var dy = y - (page.contentY0 || 0);
        var all = (page.bodies || []).concat(page.sleeves || []);
        var j, piece, fill, name;
        for (j = 0; j < all.length; j++) {
            piece = all[j];
            fill = pieceFill(piece);
            name = (piece.label || piece.kind) || '';
            if (piece.points && piece.points.length > 1) {
                out.push('<path d="' + polyToD(piece.points, ox, dy) + '" fill="' + fill
                    + '" fill-opacity="0.38" stroke="#111" stroke-width="0.02"/>');
            } else if (piece.bbox) {
                out.push('<rect x="' + (ox + piece.bbox.x).toFixed(4) + '" y="'
                    + (piece.bbox.y + dy).toFixed(4) + '" width="' + piece.bbox.w.toFixed(4)
                    + '" height="' + piece.bbox.h.toFixed(4) + '" fill="' + fill
                    + '" fill-opacity="0.36" stroke="#111" stroke-width="0.02"/>');
            }
            if (piece.bbox) {
                out.push('<text x="' + (ox + piece.bbox.x + piece.bbox.w * 0.5).toFixed(4) + '" y="'
                    + (piece.bbox.y + dy + piece.bbox.h * 0.5).toFixed(4)
                    + '" font-family="Arial, sans-serif" font-size="0.20" text-anchor="middle" fill="#111">'
                    + esc(name) + (piece.rotation != null ? ' ' + piece.rotation + '°' : '') + '</text>');
            }
        }
        for (j = 0; j < tries.length; j++) drawTry(out, tries[j], ox, dy);
        return titleH + ph + 0.28;
    }

    function serialHead(rec, n, total) {
        if (rec.final) return 'FINAL  ·  record ' + n + ' / ' + total;
        return 'RECORD ' + n + ' / ' + total;
    }

    function serialFoot(rec, n, total) {
        if (rec.final) return 'FINAL  ·  record ' + n + ' / ' + total + '  ·  last output';
        var nTry = (rec.tries && rec.tries.length) ? rec.tries.length : 0;
        return 'RECORD ' + n + ' / ' + total
            + '  ·  ' + (rec.pass || '')
            + '  ·  ' + (rec.kind || '')
            + '  ·  ' + (rec.reason || '')
            + (rec.size ? '  ·  ' + rec.size : '')
            + (nTry ? '  ·  ' + nTry + ' tries' : '');
    }

    function buildSerialSvg(result) {
        if (!result || !result.ok) {
            return '<?xml version="1.0"?><svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 8 4"><text x="0.4" y="2" font-size="0.28" fill="#666">No simulation</text></svg>';
        }
        var dia = result.dia || 63;
        var recs = list.slice();
        recs.push({ final: true, pages: result.pages || [], tries: [] });
        var total = recs.length;
        var pad = 0.9;
        var headH = 1.15;
        var footH = 1.05;
        var sheetGap = 2.2;
        var cardGap = 3.2;
        var leftBadge = 1.6;
        var i, rec, pages, tries, groups, g, sheetH, cardInnerW, cardH, maxW = 0;
        var cards = [];
        for (i = 0; i < recs.length; i++) {
            rec = recs[i];
            pages = pagesOf(rec, result);
            tries = triesOf(rec);
            groups = [];
            for (g = 0; g < pages.length; g++) groups.push([]);
            for (g = 0; g < tries.length; g++) groups[pageIndexForTry(pages, tries[g])].push(tries[g]);
            sheetH = 0;
            for (g = 0; g < pages.length; g++) {
                sheetH = Math.max(sheetH, 0.48 + Math.max(pages[g].height || 0, 6) + 0.28);
            }
            cardInnerW = pages.length * dia + Math.max(0, pages.length - 1) * sheetGap;
            cardH = pad + headH + sheetH + footH + pad;
            maxW = Math.max(maxW, leftBadge + pad + cardInnerW + pad + leftBadge);
            cards.push({
                rec: rec, pages: pages, groups: groups, n: i + 1,
                sheetH: sheetH, innerW: cardInnerW, h: cardH
            });
        }

        var W = maxW + 1.2;
        var H = 1.4;
        for (i = 0; i < cards.length; i++) H += cards[i].h + (i ? cardGap : 0);
        H += 0.8;

        var out = [];
        out.push('<?xml version="1.0" encoding="UTF-8"?>');
        out.push('<svg xmlns="http://www.w3.org/2000/svg" version="1.1"');
        out.push(' width="' + W.toFixed(4) + 'in" height="' + H.toFixed(4) + 'in"');
        out.push(' viewBox="0 0 ' + W.toFixed(4) + ' ' + H.toFixed(4) + '">');
        out.push('<rect width="' + W.toFixed(4) + '" height="' + H.toFixed(4) + '" fill="#c5cdd6"/>');
        out.push('<text x="0.5" y="0.55" font-family="Arial, sans-serif" font-size="0.28" fill="#111">');
        out.push('<tspan fill="#00c853" font-weight="700">■ sit</tspan>');
        out.push('<tspan dx="0.5" fill="#ff9800" font-weight="700">■ fail</tspan>');
        out.push('<tspan dx="0.5" fill="#ff2d2d" font-weight="700">■ overlay</tspan>');
        out.push('<tspan dx="0.7" fill="#333">each card = one record · DOC sheets stay separate</tspan></text>');

        var y = 1.1;
        var x0, card, n, cx, sy, px;
        for (i = 0; i < cards.length; i++) {
            card = cards[i];
            rec = card.rec;
            n = card.n;
            x0 = 0.5;
            out.push('<g id="RECORD-' + n + '">');
            out.push('<rect x="' + x0.toFixed(4) + '" y="' + y.toFixed(4)
                + '" width="' + (leftBadge + pad + card.innerW + pad + leftBadge).toFixed(4)
                + '" height="' + card.h.toFixed(4)
                + '" fill="' + (rec.final ? '#e8f6ea' : '#eef2f6')
                + '" stroke="#111" stroke-width="0.09"/>');
            cx = x0 + (leftBadge + pad + card.innerW + pad + leftBadge) * 0.5;
            out.push('<text x="' + cx.toFixed(4) + '" y="' + (y + 0.55).toFixed(4)
                + '" font-family="Arial, sans-serif" font-size="0.40" font-weight="700" text-anchor="middle" fill="#111">'
                + esc(serialHead(rec, n, total)) + '</text>');
            out.push('<text x="' + (x0 + 0.35).toFixed(4) + '" y="' + (y + card.h * 0.5).toFixed(4)
                + '" font-family="Arial, sans-serif" font-size="0.62" font-weight="700" fill="#1a6bb5">#'
                + n + '</text>');
            out.push('<text x="' + (x0 + leftBadge + pad + card.innerW + pad + 0.2).toFixed(4)
                + '" y="' + (y + card.h * 0.5).toFixed(4)
                + '" font-family="Arial, sans-serif" font-size="0.62" font-weight="700" fill="#1a6bb5">#'
                + n + '</text>');
            sy = y + pad + headH;
            px = x0 + leftBadge + pad;
            for (g = 0; g < card.pages.length; g++) {
                drawSheet(out, card.pages[g], card.groups[g], px, sy, dia);
                px += dia + sheetGap;
            }
            out.push('<text x="' + cx.toFixed(4) + '" y="' + (y + card.h - 0.4).toFixed(4)
                + '" font-family="Arial, sans-serif" font-size="0.26" font-weight="700" text-anchor="middle" fill="#111">'
                + esc(serialFoot(rec, n, total)) + '</text>');
            out.push('</g>');
            y += card.h + cardGap;
        }
        out.push('</svg>');
        return out.join('\n');
    }

    global.DevTrials = {
        bind: bind,
        attach: attach,
        showingRecorded: showingRecorded,
        buildSerialSvg: buildSerialSvg
    };
})(typeof window !== 'undefined' ? window : globalThis);
