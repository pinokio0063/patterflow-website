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
        sk.value = 'without_rib';
    }

    function sleevePartKeys() {
        var without = $('sleeveKey') && $('sleeveKey').value === 'without_rib';
        return {
            short: without ? 'short_slv_without_rib' : 'short_slv_with_rib',
            long: without ? 'long_slv_without_rib' : 'long_slv_with_rib'
        };
    }

    function chartPart(size, key) {
        var row = window.PF_CHART && window.PF_CHART[size];
        if (!row) return { width: 0, height: 0 };
        if (!row[key] || typeof row[key] !== 'object') row[key] = { width: 0, height: 0 };
        return row[key];
    }

    function numVal(n) {
        var x = Number(n);
        if (!isFinite(x)) return '';
        return String(Math.round(x * 10000) / 10000);
    }

    function dimInput(size, part, field, val) {
        return '<input type="number" step="0.0001" min="0" data-size="' + size
            + '" data-part="' + part + '" data-field="' + field
            + '" value="' + numVal(val) + '" />';
    }

    function renderChartEditor() {
        var host = $('chartEdit');
        if (!host) return;
        var keys = sleevePartKeys();
        var sizes = CN.chart.sizeKeys();
        var html = '<table><thead><tr>'
            + '<th rowspan="2">SIZE</th>'
            + '<th colspan="2">Body</th>'
            + '<th colspan="2">Short</th>'
            + '<th colspan="2">Long</th>'
            + '</tr><tr>'
            + '<th>W</th><th>H</th><th>W</th><th>H</th><th>W</th><th>H</th>'
            + '</tr></thead><tbody>';
        sizes.forEach(function (size) {
            var body = chartPart(size, 'FONT_BACK');
            var sh = chartPart(size, keys.short);
            var lg = chartPart(size, keys.long);
            html += '<tr>'
                + '<td class="sz">' + size + '</td>'
                + '<td>' + dimInput(size, 'FONT_BACK', 'width', body.width) + '</td>'
                + '<td>' + dimInput(size, 'FONT_BACK', 'height', body.height) + '</td>'
                + '<td>' + dimInput(size, keys.short, 'width', sh.width) + '</td>'
                + '<td>' + dimInput(size, keys.short, 'height', sh.height) + '</td>'
                + '<td>' + dimInput(size, keys.long, 'width', lg.width) + '</td>'
                + '<td>' + dimInput(size, keys.long, 'height', lg.height) + '</td>'
                + '</tr>';
        });
        html += '</tbody></table>';
        host.innerHTML = html;
        Array.prototype.forEach.call(host.querySelectorAll('input'), function (inp) {
            inp.addEventListener('change', onChartCell);
            inp.addEventListener('input', onChartCell);
        });
    }

    function onChartCell(e) {
        var inp = e.target;
        var size = inp.getAttribute('data-size');
        var part = inp.getAttribute('data-part');
        var field = inp.getAttribute('data-field');
        var n = parseFloat(inp.value);
        if (isNaN(n) || n < 0) n = 0;
        chartPart(size, part)[field] = n;
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
            console.warn('No <path> outline found in ' + fileName);
            return false;
        }
        masters[role] = { outline: outline, fileName: fileName, rawName: outline.name, svgText: text };
        if (slot) slot.classList.add('has');
        if (nameId && $(nameId)) {
            $(nameId).textContent = (outline.name || fileName) + ' · ' + outline.pathCount + ' path';
        }
        if (MEAS[role] && $(MEAS[role])) {
            $(MEAS[role]).textContent = 'File size  ' + outline.inchW.toFixed(3) + ' × ' + outline.inchH.toFixed(3)
                + ' in  (not a chart size — graded from this)';
        }
        if (thumbId && $(thumbId)) showRawThumb($(thumbId), text, ROLE_COLOR[role]);
        return true;
    }

    function classifySvgName(name) {
        var n = String(name || '').toLowerCase();
        if (/front|font/.test(n)) return 'font';
        if (/back/.test(n)) return 'back';
        if (/short|shrot|haf|half/.test(n)) return 'sleeve';
        if (/long|full/.test(n)) return 'sleeveLong';
        return null;
    }

    function renderPatternList(list) {
        var host = $('patternList');
        if (!host) return;
        host.innerHTML = '';
        (list || []).forEach(function (item) {
            var btn = document.createElement('button');
            btn.type = 'button';
            btn.className = 'pattern-card';
            btn.setAttribute('data-id', item.id);
            var thumbs = '';
            ['font', 'back', 'sleeve', 'sleeveLong'].forEach(function (k) {
                if (item[k]) thumbs += '<img src="' + item[k] + '" alt="">';
            });
            btn.innerHTML = '<h3>' + item.name + '</h3><div class="pattern-thumbs">' + thumbs + '</div>';
            btn.addEventListener('click', function () { selectPattern(item); });
            host.appendChild(btn);
        });
    }

    function selectPattern(item) {
        var jobs = [
            ['font', item.font],
            ['back', item.back],
            ['sleeve', item.sleeve],
            ['sleeveLong', item.sleeveLong]
        ].filter(function (row) { return row[1]; });
        Promise.all(jobs.map(function (row) {
            return fetchText(row[1]).then(function (text) {
                return { role: row[0], text: text, name: row[1].split('/').pop() };
            });
        })).then(function (parts) {
            masters = { font: null, back: null, sleeve: null, sleeveLong: null };
            parts.forEach(function (p) { applySvg(p.role, p.text, p.name); });
            Array.prototype.forEach.call(document.querySelectorAll('.pattern-card'), function (el) {
                el.classList.toggle('on', el.getAttribute('data-id') === item.id);
            });
            if ($('sleeveKey') && item.id === 'polo') {
                $('sleeveKey').value = 'without_rib';
                renderChartEditor();
            }
            if ($('patternStatus')) {
                $('patternStatus').textContent = item.name + ' loaded'
                    + (masters.font ? ' · FRONT' : '')
                    + (masters.back ? ' · BACK' : '')
                    + (masters.sleeve ? ' · HAF' : '')
                    + (masters.sleeveLong ? ' · FULL' : '');
            }
        }).catch(function () {
            if ($('patternStatus')) $('patternStatus').textContent = 'Could not load ' + item.name;
        });
    }

    function loadPatternCatalog() {
        return fetch('patterns/index.json').then(function (r) {
            if (!r.ok) throw new Error('no catalog');
            return r.json();
        }).then(function (data) {
            var list = Array.isArray(data) ? data : (data ? [data] : []);
            renderPatternList(list);
            if (list[0]) selectPattern(list[0]);
        }).catch(function () {
            if ($('patternStatus')) $('patternStatus').textContent = 'No patterns found';
        });
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
            $('sleeveKey').value = 'without_rib';
        }).catch(function () {
            alert('Could not fetch samples. Use start.bat (local server), or drop the SVG files yourself.');
        });
    }

    function emptyRow() {
        return { NAME: '', NUMBER: '', SIZE: 'M', QTY: 0, SLV: 'HAF', COMMENTS: '' };
    }

    function defaultJobRows() {
        var starter = { M: 3, L: 3, XL: 3 };
        return CN.chart.sizeKeys().map(function (size) {
            return { NAME: '', NUMBER: '', SIZE: size, QTY: starter[size] || 0, SLV: 'HAF', COMMENTS: '' };
        });
    }

    function rowQty(row) {
        var n = parseInt(row.QTY, 10);
        return isNaN(n) || n < 0 ? 0 : n;
    }

    var MAX_PCS_SIZE = 10;
    var MAX_PCS_TOTAL = 40;

    var limitTimer = null;

    function flashLimitNotice() {
        var el = $('limitToast');
        if (!el) return;
        el.classList.add('on');
        clearTimeout(limitTimer);
        limitTimer = setTimeout(function () {
            el.classList.remove('on');
        }, 3600);
    }

    function clampQty(raw, row, announce) {
        var wanted = parseInt(raw, 10);
        if (isNaN(wanted) || wanted < 0) wanted = 0;
        var n = wanted;
        if (n > MAX_PCS_SIZE) n = MAX_PCS_SIZE;
        var others = 0;
        jobRows.forEach(function (r) {
            if (r !== row) others += rowQty(r);
        });
        var room = MAX_PCS_TOTAL - others;
        if (room < 0) room = 0;
        if (n > room) n = room;
        if (announce && wanted > n) flashLimitNotice();
        return n;
    }

    function renderJob() {
        var tb = $('jobBody');
        tb.innerHTML = '';
        jobRows.forEach(function (row) {
            var tr = document.createElement('div');
            tr.className = 'job-row';
            tr.innerHTML =
                '<span class="size-cell">' + escapeAttr(row.SIZE) + '</span>' +
                '<input data-k="QTY" type="number" min="0" max="' + MAX_PCS_SIZE + '" step="1" value="' + rowQty(row) + '" />' +
                '<select data-k="SLV">' +
                    '<option value="HAF"' + (row.SLV === 'HAF' ? ' selected' : '') + '>HAF</option>' +
                    '<option value="FULL"' + (row.SLV === 'FULL' ? ' selected' : '') + '>FULL</option>' +
                '</select>';
            Array.prototype.forEach.call(tr.querySelectorAll('input,select'), function (el) {
                el.addEventListener('change', function () {
                    if (el.getAttribute('data-k') === 'QTY') {
                        row.QTY = clampQty(el.value, row, true);
                        el.value = row.QTY;
                    } else {
                        row[el.getAttribute('data-k')] = el.value;
                    }
                    renderChips();
                });
                el.addEventListener('input', function () {
                    if (el.getAttribute('data-k') !== 'QTY') return;
                    row.QTY = clampQty(el.value, row, true);
                    if (String(el.value) !== '' && Number(el.value) !== row.QTY) el.value = row.QTY;
                    renderChips();
                });
            });
            tb.appendChild(tr);
        });
        renderChips();
    }

    function escapeAttr(s) {
        return String(s || '').replace(/"/g, '&quot;');
    }

    function renderChips() {
        var keys = [];
        jobRows.forEach(function (r) {
            var q = rowQty(r);
            if (q <= 0) return;
            keys.push({ k: r.SIZE + ' ' + r.SLV, q: q });
        });
        $('chips').innerHTML = keys.map(function (it) {
            return '<span class="chip">' + it.k + ' <b>×' + it.q + '</b></span>';
        }).join('');
        $('jobWarn').textContent = '';
    }

    function loadJobList(list) {
        var bySize = {};
        (list || []).forEach(function (it) {
            var size = CN.chart.normalizeSize(it.SIZE);
            var slv = CN.chart.normalizeSlv(it.SLV) || 'HAF';
            if (!size) return;
            if (!bySize[size]) bySize[size] = { SIZE: size, QTY: 0, SLV: slv };
            bySize[size].QTY += 1;
            bySize[size].SLV = slv;
        });
        jobRows = defaultJobRows().map(function (row) {
            if (bySize[row.SIZE]) {
                row.QTY = bySize[row.SIZE].QTY;
                row.SLV = bySize[row.SIZE].SLV;
            }
            return row;
        });
        jobRows.forEach(function (row) {
            row.QTY = clampQty(row.QTY, row);
        });
        if (!jobRows.length) jobRows.push(emptyRow());
        renderJob();
    }

    function collectJob() {
        var out = [];
        jobRows.forEach(function (r) {
            var q = clampQty(rowQty(r), r);
            r.QTY = q;
            var i;
            for (i = 0; i < q; i++) {
                out.push({
                    NAME: r.SIZE,
                    NUMBER: String(i + 1),
                    SIZE: r.SIZE,
                    SLV: r.SLV,
                    COMMENTS: r.COMMENTS || ''
                });
            }
        });
        return out;
    }

    function outlinePayload(master) {
        if (!master || !master.outline || !master.outline.points) return null;
        return {
            points: master.outline.points.map(function (p) { return { x: p.x, y: p.y }; })
        };
    }

    function nestTimeLimit() {
        var el = $('inpTime');
        var n = el ? parseInt(el.value, 10) : NaN;
        if (isNaN(n)) n = Number(window.PF_NESTING_TIME_SEC) || 20;
        if (n < 5) n = 5;
        if (n > 90) n = 90;
        if (el) el.value = n;
        return n;
    }

    function syncNestTimeLabel() {
        var el = $('nestTimeLabel');
        if (el) el.textContent = String(nestTimeLimit());
    }

    function newQueueTicket() {
        var bytes = new Uint8Array(16);
        if (window.crypto && crypto.getRandomValues) crypto.getRandomValues(bytes);
        else for (var i = 0; i < 16; i++) bytes[i] = Math.floor(Math.random() * 256);
        bytes[6] = (bytes[6] & 0x0f) | 0x40;
        bytes[8] = (bytes[8] & 0x3f) | 0x80;
        var hex = [];
        for (var j = 0; j < 16; j++) hex.push(('0' + bytes[j].toString(16)).slice(-2));
        return hex.slice(0, 4).join('') + '-' + hex.slice(4, 6).join('') + '-' + hex.slice(6, 8).join('') + '-' + hex.slice(8, 10).join('') + '-' + hex.slice(10, 16).join('');
    }

    var simTicket = '';
    var nestStartedAt = 0;

    function setCountdown(sec) {
        var text = (Math.max(0, Number(sec) || 0)).toFixed(1);
        var nodes = document.querySelectorAll('.pane-empty .cd');
        Array.prototype.forEach.call(nodes, function (el) { el.textContent = text; });
    }

    function setQueueMsg(text) {
        var a = $('qmNesting');
        var b = $('qmCustom');
        if (a) a.textContent = text || '';
        if (b) b.textContent = text || '';
    }

    function readQueue(url) {
        if (!url) return Promise.resolve(null);
        return fetch(url, { cache: 'no-store' }).then(function (res) { return res.json(); }).catch(function () { return null; });
    }

    function pollQueues() {
        var q = simTicket ? ('?ticket=' + encodeURIComponent(simTicket)) : '';
        return Promise.all([
            readQueue(window.PF_NEST_API ? apiUrl('/queue' + q) : ''),
            readQueue(window.PF_NESTING_API ? nestingApiUrl('/queue' + q) : '')
        ]).then(function (pair) {
            var custom = pair[0] || {};
            var sparrow = pair[1] || {};
            var waiting = custom.status === 'waiting' || sparrow.status === 'waiting';
            var running = custom.status === 'running' || sparrow.status === 'running';
            var msg = sparrow.message || custom.message || '';
            if (waiting) {
                nestStartedAt = 0;
                var ahead = Math.max(Number(custom.ahead) || 0, Number(sparrow.ahead) || 0);
                msg = 'Server: waiting · ' + ahead + ' ahead · 4 slots busy';
                setCountdown(nestTimeLimit());
            } else if (running) {
                if (!nestStartedAt) nestStartedAt = Date.now();
                msg = sparrow.message || custom.message || 'Server: your job is running';
            }
            setQueueMsg(msg);
            return msg;
        });
    }

    function showEmptyFrames(on) {
        var a = $('emptyNesting');
        var b = $('emptyCustom');
        if (a) a.style.display = on ? 'flex' : 'none';
        if (b) b.style.display = on ? 'flex' : 'none';
    }

    function resetZoom100() {
        zoom = 1;
        panX = 20;
        panY = 20;
        fitZoom = 1;
        nestZoom = 1;
        nestPanX = 20;
        nestPanY = 20;
        nestFitZoom = 1;
        applyTransform();
        applyNestTransform();
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
        if (!collectJob().length) {
            alert('Set piece quantity above 0.');
            return;
        }
        btn.disabled = true;
        btn.textContent = 'START';
        simTicket = newQueueTicket();
        nestStartedAt = 0;
        var limit = nestTimeLimit();
        lastResult = null;
        lastNesting = null;
        lastSvg = '';
        lastNestSvg = '';
        $('stageWorld').innerHTML = '';
        if ($('stageNesting')) $('stageNesting').innerHTML = '';
        resetZoom100();
        showEmptyFrames(true);
        setCountdown(limit);
        setQueueMsg('Server: joining queue…');
        setSimTime('waiting for a slot…');
        pollQueues();
        var tick = setInterval(function () {
            if (!nestStartedAt) {
                setCountdown(limit);
                setSimTime('waiting for a slot…');
                return;
            }
            var left = Math.max(0, limit - (Date.now() - nestStartedAt) / 1000);
            setCountdown(left);
            setSimTime('calculating… ' + left.toFixed(1) + ' s');
        }, 100);
        var qTick = setInterval(pollQueues, 700);
        var payload = {
            dia: parseFloat($('inpDia').value),
            minGap: parseFloat($('inpGap').value),
            rowsPerDoc: parseInt($('inpRows').value, 10) || 4,
            sleeveKey: $('sleeveKey').value === 'without_rib' ? 'short_slv_without_rib' : 'short_slv_with_rib',
            timeSec: limit,
            queueTicket: simTicket,
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
            clearInterval(qTick);
            setQueueMsg('');
            var custom = pair[0];
            var nesting = pair[1];
            if (custom && !custom.pending) {
                lastResult = custom;
                if (window.CustomNest) window.CustomNest._lastResult = custom;
                pageFilter = 0;
                paint(true);
            }
            lastNesting = nesting;
            paintNesting(true);
            renderStats();
            renderNestingStats();
            var started = nestStartedAt || Date.now();
            var sec = (Date.now() - started) / 1000;
            if ((!custom || custom.pending) && (!nesting || nesting.pending)) {
                setSimTime('preview only · backends not connected');
            } else {
                setSimTime(sec.toFixed(2) + ' s');
            }
        }).then(function () {
            clearInterval(tick);
            clearInterval(qTick);
            btn.disabled = false;
            btn.textContent = 'START';
        });
    }

    function renderStats() {
        if (!lastResult) { $('stats').innerHTML = 'fabric <b>—</b>'; return; }
        if (lastResult.pending) { $('stats').textContent = 'Backend not connected'; return; }
        if (!lastResult.ok) { $('stats').textContent = lastResult.error || 'Cutting Friendly failed'; return; }
        var r = lastResult;
        var extra = '';
        if (r.overlapCount) {
            extra = ' · <b style="color:#ff6b6b">' + r.overlapCount + ' overlay</b>';
        }
        $('stats').innerHTML = 'fabric <b>' + fmtMeters(r.fabricMeters) + '</b>' + extra;
    }

    function renderNestingStats() {
        var el = $('statsNesting');
        if (!el) return;
        if (!lastNesting) { el.innerHTML = 'fabric <b>—</b>'; return; }
        if (lastNesting.pending) { el.textContent = 'Backend not connected'; return; }
        if (!lastNesting.ok) { el.textContent = lastNesting.error || 'Nesting failed'; return; }
        el.innerHTML = 'fabric <b>' + fmtMeters(lastNesting.fabricMeters) + '</b>';
    }

    function applyTransform() {
        $('stageWorld').style.transform = 'translate(' + panX + 'px,' + panY + 'px) scale(' + zoom + ')';
        $('zoomPct').textContent = Math.round(zoom * 100) + '%';
    }

    function paint(resetFit) {
        var empty = $('emptyCustom');
        if (!lastResult || !lastResult.ok) {
            lastSvg = '';
            $('stageWorld').innerHTML = '';
            if (empty) empty.style.display = 'flex';
            applyTransform();
            return;
        }
        if (empty) empty.style.display = 'none';
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
        if (!svg) {
            applyNestTransform();
            return;
        }
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

    function init() {
        fillSleeveSelect();
        renderChartEditor();
        if ($('sleeveKey')) $('sleeveKey').addEventListener('change', renderChartEditor);
        loadPatternCatalog();
        var gear = $('btnSettings');
        var pop = $('settingsPop');
        if (gear && pop) {
            gear.addEventListener('click', function (e) {
                e.stopPropagation();
                var open = pop.hasAttribute('hidden');
                if (open) {
                    pop.removeAttribute('hidden');
                    renderChartEditor();
                } else {
                    pop.setAttribute('hidden', '');
                }
                gear.classList.toggle('on', open);
            });
            document.addEventListener('click', function (e) {
                if (pop.hasAttribute('hidden')) return;
                if (pop.contains(e.target) || gear.contains(e.target)) return;
                pop.setAttribute('hidden', '');
                gear.classList.remove('on');
            });
        }
        $('btnSim').addEventListener('click', simulate);
        if ($('inpTime')) {
            $('inpTime').addEventListener('change', syncNestTimeLabel);
            $('inpTime').addEventListener('input', syncNestTimeLabel);
            syncNestTimeLabel();
        }
        $('btnZoomIn').addEventListener('click', function () { zoomBy(1.25); });
        $('btnZoomOut').addEventListener('click', function () { zoomBy(1 / 1.25); });
        $('btnZoomFit').addEventListener('click', zoomFit);
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

        jobRows = defaultJobRows();
        if (window.CustomNest) {
            window.CustomNest._repaint = function (resetFit) {
                paint(!!resetFit);
            };
        }
        renderJob();
        setCountdown(nestTimeLimit());
        showEmptyFrames(true);
        resetZoom100();
        paint(false);
        paintNesting(false);
        fetch(apiUrl('/engine')).then(function (r) { return r.text(); }).then(function (text) {
            if (!text || text.charAt(0) === '<') throw new Error('html');
            var info = JSON.parse(text);
            if (info && info.engine === 'cpp') setSimTime(info.exe ? 'C++ engine · ready' : 'C++ exe missing — run engine\\build.bat');
        }).catch(function () {
            setSimTime(window.PF_NEST_API ? 'backend offline' : 'preview only · backend not connected');
        });
    }

    init();
})();
