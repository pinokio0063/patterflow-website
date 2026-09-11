(function () {
    'use strict';

    var CN = window.CustomNest;
    var masters = { font: null, back: null, sleeve: null, sleeveLong: null };
    var jobRows = [];
    var lastResult = null;
    var lastNesting = null;
    var lastSvg = '';
    var lastNestSvg = '';
    var pageFilter = 0;
    var zoom = 1;
    var panX = 20;
    var panY = 20;
    var fitZoom = 1;
    var nestZoom = 1;
    var nestPanX = 20;
    var nestPanY = 20;
    var nestFitZoom = 1;
    var nestDragging = false;
    var nestDragX = 0;
    var nestDragY = 0;
    var dragging = false;
    var dragX = 0;
    var dragY = 0;

    var ROLE_COLOR = { font: 'FRONT', back: 'BACK', sleeve: 'SLEEVE', sleeveLong: 'SLEEVE' };
    var MEAS = { font: 'measFont', back: 'measBack', sleeve: 'measSleeve', sleeveLong: 'measSleeveLong' };

    function $(id) { return document.getElementById(id); }

    function apiUrl(path) {
        var base = String(window.PF_NEST_API || "").replace(/\/$/, "");
        return base + path;
    }

    function nestingApiUrl(path) {
        var base = String(window.PF_NESTING_API || "").replace(/\/$/, "");
        return base + path;
    }

    function fmtMeters(m) {
        if (m == null || !isFinite(m)) return '—';
        return Number(m).toFixed(3) + ' m';
    }

    function fillSleeveSelect() {
        var sk = $('sleeveKey');
        sk.innerHTML = CN.chart.RIB_KEYS.map(function (k) {
            return '<option value="' + k.id + '">' + k.label + '</option>';
        }).join('');
        sk.value = 'with_rib';
    }

    function bindSlot(role, slotId, fileId, thumbId, nameId) {
        var slot = $(slotId);
        var file = $(fileId);
        slot.addEventListener('click', function (e) {
            if (e.target && (e.target.tagName === 'SELECT' || e.target.tagName === 'OPTION')) return;
            file.click();
        });
        slot.addEventListener('dragover', function (e) {
            e.preventDefault();
            slot.style.opacity = '0.85';
        });
        slot.addEventListener('dragleave', function () { slot.style.opacity = '1'; });
        slot.addEventListener('drop', function (e) {
            e.preventDefault();
            slot.style.opacity = '1';
            if (e.dataTransfer.files && e.dataTransfer.files[0]) {
                loadSvgFile(role, e.dataTransfer.files[0], thumbId, nameId, slot);
            }
        });
        file.addEventListener('change', function () {
            if (file.files && file.files[0]) loadSvgFile(role, file.files[0], thumbId, nameId, slot);
        });
    }

    function loadSvgFile(role, file, thumbId, nameId, slot) {
        var reader = new FileReader();
        reader.onload = function () {
            applySvg(role, String(reader.result || ''), file.name, thumbId, nameId, slot);
        };
        reader.readAsText(file);
    }

    function applySvg(role, text, fileName, thumbId, nameId, slot) {
        var outline = CN.path.svgToOutline(text);
        if (!outline) {
            alert('No <path> outline found in ' + fileName);
            return;
        }
        masters[role] = { outline: outline, fileName: fileName, rawName: outline.name, svgText: text };
        slot.classList.add('has');
        $(nameId).textContent = (outline.name || fileName) + ' · ' + outline.pathCount + ' path';
        $(MEAS[role]).textContent = 'File size  ' + outline.inchW.toFixed(3) + ' × ' + outline.inchH.toFixed(3)
            + ' in  (not a chart size — graded from this)';
        showRawThumb($(thumbId), text, ROLE_COLOR[role]);
    }

    var thumbUrls = {};

    function showRawThumb(el, svgText, role) {
            var color = { FRONT: '#2ec4b6', FONT: '#2ec4b6', BACK: '#3d8bfd', SLEEVE: '#f4a261' }[role] || '#2ec4b6';
        var tinted = String(svgText)
            .replace(/fill:\s*#[0-9a-fA-F]{3,8}/gi, 'fill: ' + color)
            .replace(/fill="[^"]*"/gi, 'fill="' + color + '"');
        if (thumbUrls[el.id]) URL.revokeObjectURL(thumbUrls[el.id]);
        var url = URL.createObjectURL(new Blob([tinted], { type: 'image/svg+xml' }));
        thumbUrls[el.id] = url;
        el.innerHTML = '';
        var img = document.createElement('img');
        img.alt = role;
        img.src = url;
        el.appendChild(img);
    }

    function fetchText(url) {
        return fetch(url).then(function (r) {
            if (!r.ok) throw new Error(url);
            return r.text();
        });
    }

    function loadSampleParts() {
        Promise.all([
            fetchText('samples/FONT.svg'),
            fetchText('samples/BACK.svg'),
            fetchText('samples/SLEEVE-SHORT.svg'),
            fetchText('samples/SLEEVE-LONG.svg')
        ]).then(function (arr) {
            applySvg('font', arr[0], 'FONT.svg', 'thumbFont', 'nameFont', $('slotFont'));
            applySvg('back', arr[1], 'BACK.svg', 'thumbBack', 'nameBack', $('slotBack'));
            applySvg('sleeve', arr[2], 'SLEEVE-SHORT.svg', 'thumbSleeve', 'nameSleeve', $('slotSleeve'));
            applySvg('sleeveLong', arr[3], 'SLEEVE-LONG.svg', 'thumbSleeveLong', 'nameSleeveLong', $('slotSleeveLong'));
            $('sleeveKey').value = 'with_rib';
        }).catch(function () {
            alert('Could not fetch samples. Use start.bat (local server), or drop the SVG files yourself.');
        });
    }

    function emptyRow() {
        return { NAME: '', NUMBER: String(jobRows.length + 1), SIZE: 'M', SLV: 'HAF', COMMENTS: '' };
    }

    function renderJob() {
        var tb = $('jobBody');
        tb.innerHTML = '';
        jobRows.forEach(function (row, idx) {
            var tr = document.createElement('tr');
            tr.innerHTML =
                '<td>' + (idx + 1) + '</td>' +
                '<td><input data-k="NAME" value="' + escapeAttr(row.NAME) + '" /></td>' +
                '<td><input data-k="NUMBER" value="' + escapeAttr(row.NUMBER) + '" /></td>' +
                '<td><select data-k="SIZE">' + sizeOptions(row.SIZE) + '</select></td>' +
                '<td><select data-k="SLV">' +
                    '<option value="HAF"' + (row.SLV === 'HAF' ? ' selected' : '') + '>HAF</option>' +
                    '<option value="FULL"' + (row.SLV === 'FULL' ? ' selected' : '') + '>FULL</option>' +
                '</select></td>';
            Array.prototype.forEach.call(tr.querySelectorAll('input,select'), function (el) {
                el.addEventListener('change', function () {
                    row[el.getAttribute('data-k')] = el.value;
                    renderChips();
                });
            });
            tb.appendChild(tr);
        });
        renderChips();
    }

    function sizeOptions(cur) {
        return CN.chart.sizeKeys().map(function (k) {
            return '<option value="' + k + '"' + (k === cur ? ' selected' : '') + '>' + k + '</option>';
        }).join('');
    }

    function escapeAttr(s) {
        return String(s || '').replace(/"/g, '&quot;');
    }

    function renderChips() {
        var parsed = CN.chart.parseJob(jobRows);
        var mix = {}, keys = [];
        parsed.rows.forEach(function (r) {
            var k = r.SIZE + ' ' + r.SLV;
            if (!mix[k]) { mix[k] = 0; keys.push(k); }
            mix[k] += 1;
        });
        $('chips').innerHTML = keys.map(function (k) {
            return '<span class="chip">' + k + ' <b>×' + mix[k] + '</b></span>';
        }).join('') || '<span class="chip">no job rows</span>';
        var msg = [];
        if (parsed.skipped) msg.push(parsed.skipped + ' row(s) ignored');
        $('jobWarn').textContent = msg.join(' · ');
    }

    function loadJobList(list) {
        jobRows = (list || []).map(function (it) {
            var row = {
                NAME: String(it.NAME || ''),
                NUMBER: String(it.NUMBER || ''),
                SIZE: CN.chart.normalizeSize(it.SIZE) || 'M',
                SLV: CN.chart.normalizeSlv(it.SLV) || 'HAF',
                COMMENTS: String(it.COMMENTS != null ? it.COMMENTS : (it.COMMENT || ''))
            };
            Object.keys(it).forEach(function (k) {
                if (/^CUST-/i.test(k)) row[String(k).toUpperCase()] = String(it[k] != null ? it[k] : '');
            });
            return row;
        });
        if (!jobRows.length) jobRows.push(emptyRow());
        renderJob();
    }

    function collectJob() {
        return jobRows.map(function (r) {
            var o = {
                NAME: r.NAME,
                NUMBER: r.NUMBER,
                SIZE: r.SIZE,
                SLV: r.SLV,
                COMMENTS: r.COMMENTS || ''
            };
            Object.keys(r).forEach(function (k) {
                if (/^CUST-/i.test(k)) o[k] = r[k];
            });
            return o;
        });
    }

    function outlinePayload(master) {
        if (!master || !master.outline || !master.outline.points) return null;
        return {
            points: master.outline.points.map(function (p) { return { x: p.x, y: p.y }; })
        };
    }

    function setSimTime(text) {
        var el = $('simTime');
        if (el) el.textContent = text;
    }

    function postJson(url, payload) {
        if (!url || url.charAt(0) === '/') {
            return Promise.resolve({ pending: true });
        }
        return fetch(url, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        }).then(function (res) { return res.text().then(function (text) { return { res: res, text: text }; }); })
        .then(function (pack) {
            var text = pack.text || '';
            if (!text || text.charAt(0) === '<') throw new Error('WRONG_SERVER');
            return JSON.parse(text);
        });
    }

    function simulate() {
        var btn = $('btnSim');
        if (!masters.font || !masters.back) {
            alert('Load FRONT and BACK SVG first.');
            return;
        }
        btn.disabled = true;
        btn.textContent = 'SIMULATING…';
        var t0 = Date.now();
        setSimTime('calculating… 0.0 s');
        var tick = setInterval(function () {
            setSimTime('calculating… ' + ((Date.now() - t0) / 1000).toFixed(1) + ' s');
        }, 100);
        var payload = {
            dia: parseFloat($('inpDia').value),
            minGap: parseFloat($('inpGap').value),
            rowsPerDoc: parseInt($('inpRows').value, 10) || 4,
            sleeveKey: $('sleeveKey').value === 'without_rib' ? 'short_slv_without_rib' : 'short_slv_with_rib',
            timeSec: window.PF_NESTING_TIME_SEC || 30,
            devTrials: false,
            job: collectJob(),
            chart: window.PF_CHART || {},
            masters: {
                font: outlinePayload(masters.font),
                back: outlinePayload(masters.back),
                sleeve: outlinePayload(masters.sleeve),
                sleeveLong: outlinePayload(masters.sleeveLong)
            }
        };
        var customUrl = window.PF_NEST_API ? apiUrl('/simulate') : '';
        var nestingUrl = window.PF_NESTING_API ? nestingApiUrl('/simulate') : '';
        Promise.all([
            postJson(customUrl, payload).catch(function (e) { return { ok: false, error: e.message || String(e) }; }),
            postJson(nestingUrl, payload).catch(function (e) { return { ok: false, error: e.message || String(e) }; })
        ]).then(function (pair) {
            clearInterval(tick);
            var custom = pair[0];
            var nesting = pair[1];
            if (custom && !custom.pending) {
                lastResult = custom;
                if (window.CustomNest) window.CustomNest._lastResult = custom;
                pageFilter = 0;
                renderTabs();
                paint(true);
            }
            lastNesting = nesting;
            paintNesting(true);
            renderStats();
            renderNestingStats();
            renderCompare();
            var sec = (Date.now() - t0) / 1000;
            if ((!custom || custom.pending) && (!nesting || nesting.pending)) {
                setSimTime('preview only · backends not connected');
            } else {
                setSimTime(sec.toFixed(2) + ' s');
            }
        }).then(function () {
            clearInterval(tick);
            btn.disabled = false;
            btn.textContent = 'SIMULATE';
        });
    }

    function renderTabs() {
        var tabs = $('tabs');
        tabs.innerHTML = '';
        function add(label, val) {
            var b = document.createElement('button');
            b.className = 'tab' + (pageFilter === val ? ' on' : '');
            b.type = 'button';
            b.textContent = label;
            b.addEventListener('click', function () {
                pageFilter = val;
                renderTabs();
                paint(true);
            });
            tabs.appendChild(b);
        }
        add('ALL DOCS', 0);
        if (lastResult && lastResult.pages) {
            lastResult.pages.forEach(function (p) {
                add('DOC ' + p.index, p.index);
            });
        }
    }

    function renderStats() {
        if (!lastResult) { $('stats').textContent = 'Waiting for backend'; return; }
        if (lastResult.pending) { $('stats').textContent = 'Backend not connected'; return; }
        if (!lastResult.ok) { $('stats').textContent = lastResult.error || 'Custom failed'; return; }
        var r = lastResult;
        var extra = '';
        if (r.overlapCount) {
            extra = ' · <b style="color:#ff6b6b">' + r.overlapCount + ' overlay</b>';
        }
        var sec = (r.elapsedMs || 0) / 1000;
        var timeLabel = sec < 10 ? sec.toFixed(2) + ' s' : sec.toFixed(1) + ' s';
        $('stats').innerHTML =
            'fabric <b>' + fmtMeters(r.fabricMeters) + '</b> · <b>' + (r.pages ? r.pages.length : 0) + '</b> docs · ' +
            'calc <b class="calc-time">' + timeLabel + '</b>' + extra;
    }

    function renderNestingStats() {
        var el = $('statsNesting');
        if (!el) return;
        if (!lastNesting) { el.textContent = 'Waiting for backend'; return; }
        if (lastNesting.pending) { el.textContent = 'Backend not connected'; return; }
        if (!lastNesting.ok) { el.textContent = lastNesting.error || 'Nesting failed'; return; }
        el.innerHTML = 'fabric <b>' + fmtMeters(lastNesting.fabricMeters) + '</b>';
    }

    function renderCompare() {
        var el = $('compareStrip');
        if (!el) return;
        var n = lastNesting && lastNesting.ok ? lastNesting.fabricMeters : null;
        var c = lastResult && lastResult.ok ? lastResult.fabricMeters : null;
        var html = 'Fabric height · Nesting <b>' + fmtMeters(n) + '</b> · Custom <b>' + fmtMeters(c) + '</b>';
        if (n != null && c != null && isFinite(n) && isFinite(c)) {
            var d = n - c;
            if (Math.abs(d) < 0.0005) html += ' · <span class="better">same</span>';
            else if (d > 0) html += ' · <span class="better">Custom shorter by ' + Math.abs(d).toFixed(3) + ' m</span>';
            else html += ' · <span class="better">Nesting shorter by ' + Math.abs(d).toFixed(3) + ' m</span>';
        }
        el.innerHTML = html;
    }

    function applyTransform() {
        $('stageWorld').style.transform = 'translate(' + panX + 'px,' + panY + 'px) scale(' + zoom + ')';
        $('zoomPct').textContent = Math.round(zoom * 100) + '%';
    }

    function paint(resetFit) {
        lastSvg = CN.render.buildSvg(lastResult, pageFilter);
        $('stageWorld').innerHTML = lastSvg;
        var svg = $('stageWorld').querySelector('svg');
        if (svg) {
            /* Screen display: 12 px per inch so 63in ≈ 756px; zoom from there. */
            var vb = (svg.getAttribute('viewBox') || '0 0 63 20').split(/\s+/);
            var worldW = parseFloat(vb[2]) || 63;
            svg.removeAttribute('width');
            svg.removeAttribute('height');
            svg.style.width = (worldW * 12) + 'px';
            svg.style.height = 'auto';
        }
        if (resetFit) zoomFit();
        else applyTransform();
    }

    function zoomFit() {
        var vp = $('viewport').getBoundingClientRect();
        var svg = $('stageWorld').querySelector('svg');
        if (!svg) {
            zoom = 1; panX = 20; panY = 20; applyTransform();
            return;
        }
        var w = svg.getBoundingClientRect().width / (zoom || 1);
        var h = svg.getBoundingClientRect().height / (zoom || 1);
        if (w < 1) w = 800;
        if (h < 1) h = 400;
        fitZoom = Math.min((vp.width - 48) / w, (vp.height - 48) / h);
        if (!isFinite(fitZoom) || fitZoom <= 0) fitZoom = 0.2;
        zoom = fitZoom;
        panX = 24;
        panY = 24;
        applyTransform();
        updateZoomButtons();
    }

    function minZoom() {
        var floor = fitZoom > 0 ? fitZoom : 0.05;
        return floor;
    }

    function updateZoomButtons() {
        var out = $('btnZoomOut');
        if (out) out.disabled = zoom <= minZoom() + 0.0001;
    }

    function zoomBy(factor, cx, cy) {
        var next = zoom * factor;
        var floor = minZoom();
        if (next < floor) next = floor;
        if (next > 16) next = 16;
        if (next === zoom) {
            updateZoomButtons();
            return;
        }
        if (cx != null) {
            panX = cx - (cx - panX) * (next / zoom);
            panY = cy - (cy - panY) * (next / zoom);
        }
        zoom = next;
        applyTransform();
        updateZoomButtons();
    }

    function applyNestTransform() {
        var world = $('stageNesting');
        if (!world) return;
        world.style.transform = 'translate(' + nestPanX + 'px,' + nestPanY + 'px) scale(' + nestZoom + ')';
        if ($('nestZoomPct')) $('nestZoomPct').textContent = Math.round(nestZoom * 100) + '%';
        if ($('btnNestZoomOut')) $('btnNestZoomOut').disabled = nestZoom <= (nestFitZoom || 0.05) + 0.0001;
    }

    function paintNesting(resetFit) {
        var empty = $('emptyNesting');
        var world = $('stageNesting');
        if (!world) return;
        var svg = '';
        if (lastNesting && lastNesting.ok && lastNesting.svg) svg = lastNesting.svg;
        lastNestSvg = svg;
        world.innerHTML = svg;
        if (empty) empty.style.display = svg ? 'none' : 'flex';
        var node = world.querySelector('svg');
        if (node) {
            var vb = (node.getAttribute('viewBox') || '0 0 63 20').split(/\s+/);
            var worldW = parseFloat(vb[2]) || 63;
            node.removeAttribute('width');
            node.removeAttribute('height');
            node.style.width = (worldW * 12) + 'px';
            node.style.height = 'auto';
        }
        if (resetFit) nestZoomFit();
        else applyNestTransform();
    }

    function nestZoomFit() {
        var vp = $('viewportNesting');
        var svg = $('stageNesting') && $('stageNesting').querySelector('svg');
        if (!vp || !svg) {
            nestZoom = 1; nestPanX = 20; nestPanY = 20; applyNestTransform();
            return;
        }
        var w = svg.getBoundingClientRect().width / (nestZoom || 1);
        var h = svg.getBoundingClientRect().height / (nestZoom || 1);
        if (w < 1) w = 800;
        if (h < 1) h = 400;
        var box = vp.getBoundingClientRect();
        nestFitZoom = Math.min((box.width - 48) / w, (box.height - 48) / h);
        if (!isFinite(nestFitZoom) || nestFitZoom <= 0) nestFitZoom = 0.2;
        nestZoom = nestFitZoom;
        nestPanX = 24;
        nestPanY = 24;
        applyNestTransform();
    }

    function nestZoomBy(factor, cx, cy) {
        var next = nestZoom * factor;
        var floor = nestFitZoom > 0 ? nestFitZoom : 0.05;
        if (next < floor) next = floor;
        if (next > 16) next = 16;
        if (next === nestZoom) {
            applyNestTransform();
            return;
        }
        if (cx != null) {
            nestPanX = cx - (cx - nestPanX) * (next / nestZoom);
            nestPanY = cy - (cy - nestPanY) * (next / nestZoom);
        }
        nestZoom = next;
        applyNestTransform();
    }

    function downloadJson() {
        if (!lastResult) {
            alert('Simulate first.');
            return;
        }
        var payload = {
            ok: lastResult.ok,
            dia: lastResult.dia,
            minGap: lastResult.minGap,
            fabricInches: lastResult.fabricInches,
            fabricMeters: lastResult.fabricMeters,
            sleeveKey: lastResult.sleeveKey,
            docs: lastResult.docs || []
        };
        var blob = new Blob([JSON.stringify(payload, null, 2)], { type: 'application/json' });
        var a = document.createElement('a');
        a.href = URL.createObjectURL(blob);
        a.download = 'patternflow-nest.json';
        a.click();
        setTimeout(function () { URL.revokeObjectURL(a.href); }, 2000);
    }

    function downloadSvg() {
        if (!lastResult) {
            alert('Simulate first.');
            return;
        }
        var svg = CN.render.buildSvg(lastResult, pageFilter);
        var name = 'patternflow-nest';
        if (pageFilter > 0) name += '-doc' + pageFilter;
        name += '.svg';
        var blob = new Blob([svg], { type: 'image/svg+xml' });
        var a = document.createElement('a');
        a.href = URL.createObjectURL(blob);
        a.download = name;
        a.click();
        setTimeout(function () { URL.revokeObjectURL(a.href); }, 2000);
    }

    function init() {
        fillSleeveSelect();
        bindSlot('font', 'slotFont', 'fileFont', 'thumbFont', 'nameFont');
        bindSlot('back', 'slotBack', 'fileBack', 'thumbBack', 'nameBack');
        bindSlot('sleeve', 'slotSleeve', 'fileSleeve', 'thumbSleeve', 'nameSleeve');
        bindSlot('sleeveLong', 'slotSleeveLong', 'fileSleeveLong', 'thumbSleeveLong', 'nameSleeveLong');

        $('btnDemoParts').addEventListener('click', loadSampleParts);
        $('btnAddRow').addEventListener('click', function () {
            jobRows.push(emptyRow());
            renderJob();
        });
        $('btnLoadJob').addEventListener('click', function () { $('fileJob').click(); });
        $('fileJob').addEventListener('change', function () {
            var f = $('fileJob').files && $('fileJob').files[0];
            if (!f) return;
            var reader = new FileReader();
            reader.onload = function () {
                try { loadJobList(JSON.parse(reader.result)); }
                catch (e) { alert('Invalid JSON'); }
            };
            reader.readAsText(f);
        });
        $('btnDemoJob').addEventListener('click', function () {
            fetch('samples/demo-job.json').then(function (r) { return r.json(); }).then(loadJobList)
                .catch(function () { alert('Open via start.bat to load the sample job, or use Load JSON.'); });
        });
        $('btnSim').addEventListener('click', simulate);
        $('btnZoomIn').addEventListener('click', function () { zoomBy(1.25); });
        $('btnZoomOut').addEventListener('click', function () { zoomBy(1 / 1.25); });
        $('btnZoomFit').addEventListener('click', zoomFit);
        $('btnDownloadSvg').addEventListener('click', downloadSvg);
        $('btnDownloadJson').addEventListener('click', downloadJson);
        if ($('btnNestZoomIn')) $('btnNestZoomIn').addEventListener('click', function () { nestZoomBy(1.25); });
        if ($('btnNestZoomOut')) $('btnNestZoomOut').addEventListener('click', function () { nestZoomBy(1 / 1.25); });
        if ($('btnNestZoomFit')) $('btnNestZoomFit').addEventListener('click', nestZoomFit);

        var vp = $('viewport');
        vp.addEventListener('wheel', function (e) {
            e.preventDefault();
            var rect = vp.getBoundingClientRect();
            var cx = e.clientX - rect.left;
            var cy = e.clientY - rect.top;
            zoomBy(e.deltaY < 0 ? 1.12 : 1 / 1.12, cx, cy);
        }, { passive: false });
        vp.addEventListener('mousedown', function (e) {
            if (e.button !== 0) return;
            dragging = true;
            vp.classList.add('drag');
            dragX = e.clientX - panX;
            dragY = e.clientY - panY;
        });
        window.addEventListener('mousemove', function (e) {
            if (!dragging) return;
            panX = e.clientX - dragX;
            panY = e.clientY - dragY;
            applyTransform();
        });
        window.addEventListener('mouseup', function () {
            dragging = false;
            vp.classList.remove('drag');
        });
        var nvp = $('viewportNesting');
        if (nvp) {
            nvp.addEventListener('wheel', function (e) {
                e.preventDefault();
                var rect = nvp.getBoundingClientRect();
                nestZoomBy(e.deltaY < 0 ? 1.12 : 1 / 1.12, e.clientX - rect.left, e.clientY - rect.top);
            }, { passive: false });
            nvp.addEventListener('mousedown', function (e) {
                if (e.button !== 0) return;
                nestDragging = true;
                nvp.classList.add('drag');
                nestDragX = e.clientX - nestPanX;
                nestDragY = e.clientY - nestPanY;
            });
        }
        window.addEventListener('mousemove', function (e) {
            if (!nestDragging) return;
            nestPanX = e.clientX - nestDragX;
            nestPanY = e.clientY - nestDragY;
            applyNestTransform();
        });
        window.addEventListener('mouseup', function () {
            nestDragging = false;
            if (nvp) nvp.classList.remove('drag');
        });
        window.addEventListener('resize', function () {
            if (lastSvg) zoomFit();
            if (lastNestSvg) nestZoomFit();
        });

        jobRows = [
            { NAME: 'T1', NUMBER: '1', SIZE: 'L', SLV: 'HAF' },
            { NAME: 'T2', NUMBER: '2', SIZE: 'L', SLV: 'HAF' },
            { NAME: 'T3', NUMBER: '1', SIZE: 'M', SLV: 'HAF' }
        ];
        if (window.CustomNest) {
            window.CustomNest._repaint = function (resetFit) {
                renderTabs();
                paint(!!resetFit);
            };
        }
        renderJob();
        renderTabs();
        renderCompare();
        paintNesting(true);
        fetch(apiUrl('/engine')).then(function (r) { return r.text(); }).then(function (text) {
            if (!text || text.charAt(0) === '<') throw new Error('html');
            var info = JSON.parse(text);
            if (info && info.engine === 'cpp') setSimTime(info.exe ? 'C++ engine · ready' : 'C++ exe missing — run engine\\build.bat');
        }).catch(function () {
            setSimTime(window.PF_NEST_API ? 'backend offline' : 'preview only · backend not connected');
        });
        [['thumbFont', 'FRONT — drop SVG'],
         ['thumbBack', 'BACK — drop SVG'],
         ['thumbSleeve', 'SHORT · HAF — drop SVG'],
         ['thumbSleeveLong', 'LONG · FULL — drop SVG']].forEach(function (row) {
            $(row[0]).innerHTML = '<div class="ph">' + row[1] + '</div>';
        });
        paint(true);
    }

    init();
})();
