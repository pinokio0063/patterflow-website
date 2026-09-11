/**
 * FULL 4-sleeve gold-layout lab.
 * Does not change the C++ nest engine. Measures the user's arrangement.
 */
(function () {
    'use strict';

    var path = window.CustomNest && window.CustomNest.path;
    var geom = window.CustomNest && window.CustomNest.geom;
    var COLORS = ['#2ec4b6', '#f4a261', '#e76f51', '#3d8bfd', '#c9a227'];

    var master = null;
    var sourceName = '';
    var pieces = [];
    var sel = -1;
    var drag = null;
    var lastGold = null;
    var PX = 8;

    var $ = function (id) { return document.getElementById(id); };

    function round(n, d) {
        var m = Math.pow(10, d == null ? 2 : d);
        return Math.round(n * m) / m;
    }

    function rotSnap(deg) {
        var a = ((deg % 360) + 360) % 360;
        return a > 90 && a < 270 ? 180 : 0;
    }

    function scalePts(pts, s) {
        var out = [], i;
        for (i = 0; i < pts.length; i++) out.push({ x: pts[i].x * s, y: pts[i].y * s });
        return out;
    }

    function rebuild(p) {
        var placed = geom.placePoly(p.local, p.x, p.y, p.rot, false);
        p.points = placed.points;
        p.bbox = placed.bbox;
        p.x = placed.bbox.x;
        p.y = placed.bbox.y;
    }

    function setStatus(msg) {
        $('status').textContent = msg || '';
    }

    function dia() { return parseFloat($('inpDia').value) || 63; }
    function minGap() { return parseFloat($('inpGap').value) || 0.05; }
    function count() {
        var n = parseInt($('inpCount').value, 10);
        if (n < 3) n = 3;
        if (n > 5) n = 5;
        return n;
    }

    function boardSize() {
        var el = $('board');
        return { w: el.clientWidth || 800, h: el.clientHeight || 600 };
    }

    function layoutScale() {
        var sz = boardSize();
        var worldW = dia() + 4;
        var worldH = 36;
        var px = Math.min((sz.w - 24) / worldW, (sz.h - 24) / worldH);
        if (px < 4) px = 4;
        PX = px;
    }

    function toScreen(x, y) {
        return { x: 16 + x * PX, y: 16 + y * PX };
    }

    function ptsToD(pts) {
        var d = '', i;
        for (i = 0; i < pts.length; i++) {
            var s = toScreen(pts[i].x, pts[i].y);
            d += (i ? ' L ' : 'M ') + s.x.toFixed(1) + ' ' + s.y.toFixed(1);
        }
        return d + ' Z';
    }

    function draw() {
        layoutScale();
        var svg = $('stage');
        var sz = boardSize();
        svg.setAttribute('viewBox', '0 0 ' + sz.w + ' ' + sz.h);
        svg.setAttribute('width', sz.w);
        svg.setAttribute('height', sz.h);
        var g = $('shapes');
        g.innerHTML = '';
        var d0 = toScreen(0, 0);
        var d1 = toScreen(dia(), 0);
        var d2 = toScreen(dia(), 40);
        $('diaRect').setAttribute('x', d0.x);
        $('diaRect').setAttribute('y', d0.y);
        $('diaRect').setAttribute('width', d1.x - d0.x);
        $('diaRect').setAttribute('height', d2.y - d0.y);
        $('diaLine').setAttribute('x1', d1.x);
        $('diaLine').setAttribute('y1', d0.y);
        $('diaLine').setAttribute('x2', d2.x);
        $('diaLine').setAttribute('y2', d2.y);
        var i;
        for (i = 0; i < pieces.length; i++) {
            var p = pieces[i];
            rebuild(p);
            var el = document.createElementNS('http://www.w3.org/2000/svg', 'g');
            el.setAttribute('class', 'piece' + (i === sel ? ' sel' : ''));
            el.setAttribute('data-i', String(i));
            var pathEl = document.createElementNS('http://www.w3.org/2000/svg', 'path');
            pathEl.setAttribute('d', ptsToD(p.points));
            pathEl.setAttribute('fill', COLORS[i % COLORS.length]);
            pathEl.setAttribute('fill-opacity', '0.72');
            pathEl.setAttribute('stroke', i === sel ? '#fff' : '#111');
            pathEl.setAttribute('stroke-width', i === sel ? 2 : 1);
            el.appendChild(pathEl);
            var bb = p.bbox;
            var c = toScreen(bb.x + bb.w * 0.5, bb.y + 0.55);
            var lab = document.createElementNS('http://www.w3.org/2000/svg', 'text');
            lab.setAttribute('x', c.x);
            lab.setAttribute('y', c.y);
            lab.setAttribute('text-anchor', 'middle');
            lab.setAttribute('fill', '#111');
            lab.setAttribute('font-size', '12');
            lab.setAttribute('font-weight', '700');
            lab.textContent = (i + 1) + ' · ' + rotSnap(p.rot) + '°';
            el.appendChild(lab);
            g.appendChild(el);
        }
        $('selInfo').textContent = sel < 0
            ? 'কোনো স্লিভ সিলেক্ট নেই'
            : ('সিলেক্ট #' + (sel + 1) + '  সাইজ ' + round(pieces[sel].bbox.w, 2) + '×' + round(pieces[sel].bbox.h, 2) + ' in  ·  ' + rotSnap(pieces[sel].rot) + '°');
    }

    function deal() {
        if (!master) {
            setStatus('আগে FULL SVG দিন বা স্যাম্পল লোড করুন।');
            return;
        }
        var n = count();
        var scales = [];
        var k;
        for (k = 0; k < n; k++) {
            scales.push(round(0.68 + k * (0.32 / Math.max(1, n - 1)) + (Math.random() * 0.04 - 0.02), 3));
        }
        scales.sort(function (a, b) { return a - b; });
        pieces = [];
        var x = 0.4;
        var y = 1.2;
        for (k = 0; k < n; k++) {
            var local = scalePts(master.points, scales[k]);
            var p = { local: local, scale: scales[k], rot: 0, x: x, y: y };
            rebuild(p);
            pieces.push(p);
            x += p.bbox.w + 0.6;
            if (x + 8 > dia()) {
                x = 0.4;
                y += p.bbox.h + 0.8;
            }
        }
        sel = 0;
        lastGold = null;
        $('facts').innerHTML = '<p class="warn">ছোট→বড় সাজানো আছে। এখন আপনি ৪টা (বা ৩–৫) হাতা টেনে সঠিক রোতে বসান, তারপর <b>Analyze</b>।</p>';
        setStatus(n + 'টা কপি · ছোট থেকে বড় · ড্র্যাগ করে সাজান');
        draw();
    }

    function ingestSvg(text, name) {
        var outline = path.svgToOutline(text);
        if (!outline) {
            setStatus('SVG থেকে হাতার শেপ পাইনি।');
            return;
        }
        master = path.gradeOutline(outline, outline.inchW, outline.inchH);
        sourceName = name || outline.name || 'FULL.svg';
        $('fname').textContent = sourceName + ' · ' + round(master.bbox.w, 2) + '×' + round(master.bbox.h, 2) + ' in';
        deal();
    }

    function loadSample() {
        fetch('../samples/SLEEVE-LONG.svg')
            .then(function (r) {
                if (!r.ok) throw new Error('missing');
                return r.text();
            })
            .then(function (t) { ingestSvg(t, 'SLEEVE-LONG.svg'); })
            .catch(function () { setStatus('স্যাম্পল পাইনি। SVG ড্রপ করুন।'); });
    }

    function pieceAtEvent(ev) {
        var t = ev.target;
        while (t && t !== $('stage')) {
            if (t.getAttribute && t.getAttribute('data-i') != null) {
                return parseInt(t.getAttribute('data-i'), 10);
            }
            t = t.parentNode;
        }
        return -1;
    }

    function onDown(ev) {
        var i = pieceAtEvent(ev);
        if (i < 0) {
            sel = -1;
            draw();
            return;
        }
        sel = i;
        var svg = $('stage');
        var pt = svg.createSVGPoint();
        pt.x = ev.clientX;
        pt.y = ev.clientY;
        var ctm = svg.getScreenCTM();
        if (!ctm) return;
        var loc = pt.matrixTransform(ctm.inverse());
        drag = {
            i: i,
            ox: loc.x - (16 + pieces[i].x * PX),
            oy: loc.y - (16 + pieces[i].y * PX)
        };
        draw();
        ev.preventDefault();
    }

    function onMove(ev) {
        if (!drag) return;
        var svg = $('stage');
        var pt = svg.createSVGPoint();
        pt.x = ev.clientX;
        pt.y = ev.clientY;
        var ctm = svg.getScreenCTM();
        if (!ctm) return;
        var loc = pt.matrixTransform(ctm.inverse());
        var p = pieces[drag.i];
        p.x = (loc.x - 16 - drag.ox) / PX;
        p.y = (loc.y - 16 - drag.oy) / PX;
        if (p.x < -1) p.x = -1;
        if (p.y < 0) p.y = 0;
        rebuild(p);
        draw();
    }

    function onUp() { drag = null; }

    function toggleRot() {
        if (sel < 0) return;
        var p = pieces[sel];
        var cx = p.bbox.x + p.bbox.w * 0.5;
        var cy = p.bbox.y + p.bbox.h * 0.5;
        p.rot = rotSnap(p.rot) === 0 ? 180 : 0;
        rebuild(p);
        p.x += cx - (p.bbox.x + p.bbox.w * 0.5);
        p.y += cy - (p.bbox.y + p.bbox.h * 0.5);
        rebuild(p);
        draw();
    }

    function nudge(dx, dy) {
        if (sel < 0) return;
        pieces[sel].x += dx;
        pieces[sel].y += dy;
        if (pieces[sel].y < 0) pieces[sel].y = 0;
        rebuild(pieces[sel]);
        draw();
    }

    function ordered() {
        var idx = pieces.map(function (_, i) { return i; });
        idx.sort(function (a, b) { return pieces[a].bbox.x - pieces[b].bbox.x; });
        return idx;
    }

    function analyze() {
        if (pieces.length < 3) {
            setStatus('আগে কপি তৈরি করুন।');
            return;
        }
        var gap = minGap();
        var order = ordered();
        var row = order.map(function (i) { return pieces[i]; });
        var serial = row.map(function (p) { return rotSnap(p.rot); });
        var odd = [], even = [], i;
        for (i = 0; i < row.length; i++) {
            if (i % 2 === 0) odd.push(row[i]);
            else even.push(row[i]);
        }
        var oddTop = odd.map(function (p) { return p.bbox.y; });
        var evenHem = even.map(function (p) { return p.bbox.b; });
        var oddSpread = oddTop.length ? Math.max.apply(null, oddTop) - Math.min.apply(null, oddTop) : 0;
        var evenSpread = evenHem.length ? Math.max.apply(null, evenHem) - Math.min.apply(null, evenHem) : 0;
        var oddMinY = odd.length ? Math.min.apply(null, odd.map(function (p) { return p.bbox.y; })) : 0;
        var evenMinY = even.length ? Math.min.apply(null, even.map(function (p) { return p.bbox.y; })) : 0;
        var dropIn = evenMinY - oddMinY;
        var tall = 0;
        for (i = 0; i < row.length; i++) if (row[i].bbox.h > tall) tall = row[i].bbox.h;
        var dropPct = tall > 0.01 ? (dropIn / tall) * 100 : 0;
        var gaps = [];
        var overlay = false;
        for (i = 0; i < row.length; i++) {
            var j;
            for (j = i + 1; j < row.length; j++) {
                var d = geom.minDistance(row[i], row[j]);
                gaps.push({ a: i + 1, b: j + 1, d: d });
                if (d < 1e-4) overlay = true;
            }
        }
        var neighbor = [];
        for (i = 0; i < row.length - 1; i++) {
            neighbor.push(geom.minDistance(row[i], row[i + 1]));
        }
        var tight = neighbor.filter(function (d) { return d + 1e-4 < gap; });
        var loose = neighbor.filter(function (d) { return d > gap + 0.15; });
        var recipe0 = serial.every(function (r, k) { return r === ((k % 2) === 0 ? 0 : 180); });
        var recipe180 = serial.every(function (r, k) { return r === ((k % 2) === 0 ? 180 : 0); });
        var midDx = null;
        if (row.length >= 4) {
            midDx = {
                secondFromFirst: row[1].bbox.x - (row[0].bbox.r),
                thirdFromSecond: row[2].bbox.x - (row[1].bbox.r)
            };
        }

        lastGold = {
            kind: 'full-sleeve-gold-row',
            created: new Date().toISOString(),
            source: sourceName,
            dia: dia(),
            minGap: gap,
            count: row.length,
            serial: serial,
            recipe: recipe0 ? 'row1-0/180' : (recipe180 ? 'row2-180/0' : 'custom'),
            oddTopAlignIn: round(oddSpread, 3),
            evenHemAlignIn: round(evenSpread, 3),
            dropIn: round(dropIn, 3),
            dropPctOfTallest: round(dropPct, 1),
            neighborGapsIn: neighbor.map(function (d) { return round(d, 3); }),
            overlay: overlay,
            underMinGap: tight.length > 0,
            looseGaps: loose.length > 0,
            mid: midDx ? {
                secondFromFirst: round(midDx.secondFromFirst, 3),
                thirdFromSecond: round(midDx.thirdFromSecond, 3)
            } : null,
            pieces: row.map(function (p, n) {
                return {
                    pos: n + 1,
                    rot: rotSnap(p.rot),
                    scale: p.scale,
                    x: round(p.bbox.x, 3),
                    y: round(p.bbox.y, 3),
                    w: round(p.bbox.w, 3),
                    h: round(p.bbox.h, 3),
                    hem: round(p.bbox.b, 3)
                };
            })
        };

        var lines = [];
        lines.push('<p><b>বাম→ডান রোটেশন:</b> ' + serial.join(' / ') + '°</p>');
        lines.push('<p class="' + (recipe0 || recipe180 ? 'ok' : 'warn') + '">' +
            (recipe0 ? 'ইঞ্জিন রো ১ সিরিয়ালের মতো (০/১৮০/০/১৮০)।' :
                (recipe180 ? 'ইঞ্জিন রো ২ সিরিয়ালের মতো (১৮০/০/১৮০/০)।' :
                    'সিরিয়াল ইঞ্জিন রেসিপি থেকে আলাদা — এটাই গোল্ড হিসেবে সেভ করা যাবে।')) +
            '</p>');
        lines.push('<p class="' + (oddSpread <= 0.08 ? 'ok' : 'warn') + '"><b>বিজোড় টপ-অ্যালাইন:</b> ফারাক ' +
            round(oddSpread, 3) + ' in ' + (oddSpread <= 0.08 ? '(একই টপ)' : '(টপ মিলেনি)') + '</p>');
        lines.push('<p class="' + (evenSpread <= 0.08 ? 'ok' : 'warn') + '"><b>জোড় ডাউন-অ্যালাইন:</b> হেম ফারাক ' +
            round(evenSpread, 3) + ' in ' + (evenSpread <= 0.08 ? '(একই হেম)' : '(হেম মিলেনি)') + '</p>');
        lines.push('<p><b>জোড় কত নিচে:</b> ' + round(dropIn, 3) + ' in  ·  লম্বা হাতার ' +
            round(dropPct, 1) + '% ' +
            (Math.abs(dropIn) < 0.08 ? '(ফ্ল্যাট রো)' :
                (dropPct >= 12 && dropPct <= 60 ? '(ইঞ্জিন জিপার রেঞ্জ ১২–৬০%)' : '(ইঞ্জিন জিপার রেঞ্জের বাইরে)')) +
            '</p>');
        lines.push('<p class="' + (overlay ? 'bad' : (tight.length ? 'bad' : (loose.length ? 'warn' : 'ok'))) + '"><b>True গ্যাপ:</b> পাশাপাশি ' +
            neighbor.map(function (d) { return round(d, 3); }).join(' / ') + ' in' +
            (overlay ? ' · ওভারল্যাপ আছে' : '') +
            (tight.length ? ' · min gap-এর কম' : '') +
            (loose.length && !overlay && !tight.length ? ' · কিছু জায়গায় গ্যাপ বেশি (মাইনাস করা যেত)' : '') +
            '</p>');
        if (midDx) {
            lines.push('<p><b>মাঝের ২টা (২য়+৩য়) ফাঁক:</b> ১ম→২য় AABB ' + round(midDx.secondFromFirst, 3) +
                ' in · ২য়→৩য় ' + round(midDx.thirdFromSecond, 3) + ' in</p>');
        }
        lines.push('<p class="hint">এই মাপগুলোই আপনার ভাষা। সেভ করে চ্যাটে দিন — ইঞ্জিন তখনই বদলাবে যখন আপনি বলবেন এই গোল্ড লক করো।</p>');
        $('facts').innerHTML = lines.join('');
        setStatus('Analyze OK · চাইলে গোল্ড JSON সেভ করুন');
    }

    function copyReport() {
        if (!lastGold) {
            analyze();
            if (!lastGold) return;
        }
        var g = lastGold;
        var txt = [
            'FULL-SLVE গোল্ড রো',
            'রোটেশন: ' + g.serial.join(' / '),
            'রেসিপি: ' + g.recipe,
            'বিজোড় টপ ফারাক: ' + g.oddTopAlignIn + ' in',
            'জোড় হেম ফারাক: ' + g.evenHemAlignIn + ' in',
            'ড্রপ: ' + g.dropIn + ' in (' + g.dropPctOfTallest + '%)',
            'পাশের গ্যাপ: ' + g.neighborGapsIn.join(' / '),
            'ওভারল্যাপ: ' + (g.overlay ? 'হ্যাঁ' : 'না'),
            g.mid ? ('মাঝ ফাঁক: ' + g.mid.secondFromFirst + ' / ' + g.mid.thirdFromSecond) : ''
        ].filter(Boolean).join('\n');
        if (navigator.clipboard && navigator.clipboard.writeText) {
            navigator.clipboard.writeText(txt).then(function () { setStatus('রিপোর্ট কপি হয়েছে — চ্যাটে পেস্ট করুন'); });
        } else {
            setStatus(txt);
        }
    }

    function saveGold() {
        if (!lastGold) analyze();
        if (!lastGold) return;
        var blob = new Blob([JSON.stringify(lastGold, null, 2)], { type: 'application/json' });
        var a = document.createElement('a');
        var stamp = lastGold.created.replace(/[:.]/g, '-').slice(0, 19);
        a.href = URL.createObjectURL(blob);
        a.download = 'full-row-gold-' + stamp + '.json';
        a.click();
        URL.revokeObjectURL(a.href);
        setStatus('গোল্ড JSON ডাউনলোড · চাইলে FULL-SLVE/gold/ এ রাখুন');
    }

    function bindDrop() {
        var box = $('drop');
        var file = $('fileSvg');
        box.addEventListener('click', function () { file.click(); });
        file.addEventListener('change', function () {
            var f = file.files && file.files[0];
            if (!f) return;
            var r = new FileReader();
            r.onload = function () { ingestSvg(String(r.result), f.name); };
            r.readAsText(f);
        });
        ['dragover', 'dragenter'].forEach(function (ev) {
            box.addEventListener(ev, function (e) { e.preventDefault(); box.classList.add('over'); });
        });
        box.addEventListener('dragleave', function () { box.classList.remove('over'); });
        box.addEventListener('drop', function (e) {
            e.preventDefault();
            box.classList.remove('over');
            var f = e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0];
            if (!f) return;
            var r = new FileReader();
            r.onload = function () { ingestSvg(String(r.result), f.name); };
            r.readAsText(f);
        });
    }

    function init() {
        if (!path || !geom) {
            setStatus('path.js / geom.js লোড হয়নি। start.bat দিয়ে খুলুন।');
            return;
        }
        bindDrop();
        $('btnSample').addEventListener('click', loadSample);
        $('btnDeal').addEventListener('click', deal);
        $('btnRot').addEventListener('click', toggleRot);
        $('btnAnalyze').addEventListener('click', analyze);
        $('btnCopy').addEventListener('click', copyReport);
        $('btnSave').addEventListener('click', saveGold);
        var stage = $('stage');
        stage.addEventListener('mousedown', onDown);
        window.addEventListener('mousemove', onMove);
        window.addEventListener('mouseup', onUp);
        window.addEventListener('keydown', function (e) {
            if (e.key === 'r' || e.key === 'R') { toggleRot(); e.preventDefault(); }
            if (e.key === 'ArrowLeft') { nudge(-0.05, 0); e.preventDefault(); }
            if (e.key === 'ArrowRight') { nudge(0.05, 0); e.preventDefault(); }
            if (e.key === 'ArrowUp') { nudge(0, -0.05); e.preventDefault(); }
            if (e.key === 'ArrowDown') { nudge(0, 0.05); e.preventDefault(); }
        });
        window.addEventListener('resize', draw);
        loadSample();
    }

    document.addEventListener('DOMContentLoaded', init);
})();
