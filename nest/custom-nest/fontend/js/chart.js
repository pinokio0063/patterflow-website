/**
 * Size chart + job list (testing JSON shape). Pants are ignored.
 */
(function (global) {
    'use strict';

    var CN = global.CustomNest = global.CustomNest || {};

    /* Physical rank (small → large). Do not reorder — nest logic uses sizeIndex. */
    var SIZE_ORDER = ['2Y', '4Y', '6Y', '8Y', '10Y', '12Y', 'XS', 'S', 'M', 'L', 'XL', '2XL', '3XL', '4XL', '5XL'];
    /* Job list display: adult first, then youth. */
    var LIST_ORDER = ['XS', 'S', 'M', 'L', 'XL', '2XL', '3XL', '4XL', '5XL', '2Y', '4Y', '6Y', '8Y', '10Y', '12Y'];

    var SLV_KEYS = [
        { id: 'short_slv_without_rib', label: 'Short sleeve — without rib' },
        { id: 'short_slv_with_rib', label: 'Short sleeve — with rib' },
        { id: 'long_slv_without_rib', label: 'Long sleeve — without rib' },
        { id: 'long_slv_with_rib', label: 'Long sleeve — with rib' }
    ];

    var RIB_KEYS = [
        { id: 'with_rib', label: 'With rib' },
        { id: 'without_rib', label: 'Without rib' }
    ];

    function chart() {
        return global.PF_CHART || {};
    }

    function sizeKeys() {
        return LIST_ORDER.filter(function (k) {
            return chart()[k] && chart()[k].FONT_BACK;
        });
    }

    function sizeIndex(size) {
        var i = SIZE_ORDER.indexOf(String(size || '').toUpperCase());
        return i === -1 ? 999 : i;
    }

    function normalizeSize(raw) {
        if (!raw) return '';
        return String(raw).toUpperCase().trim()
            .replace(/XXXXXL/g, '5XL')
            .replace(/XXXXL/g, '4XL')
            .replace(/XXXL/g, '3XL')
            .replace(/XXL/g, '2XL');
    }

    function normalizeSlv(raw) {
        if (!raw) return '';
        var slv = String(raw).toUpperCase().trim();
        if (slv.indexOf('H') === 0) return 'HAF';
        if (slv.indexOf('F') === 0) return 'FULL';
        return slv;
    }

    function partDim(size, key) {
        var row = chart()[normalizeSize(size)];
        if (!row || !row[key]) return { w: 0, h: 0 };
        var w = Number(row[key].width), h = Number(row[key].height);
        return {
            w: isFinite(w) && w > 0 ? w : 0,
            h: isFinite(h) && h > 0 ? h : 0
        };
    }

    function bodyDim(size) { return partDim(size, 'FONT_BACK'); }

    function sleeveDim(size, sleeveKey) { return partDim(size, sleeveKey); }

    /**
     * Testing JSON: one row = one garment copy. NUMBER is a label, not a multiplier.
     */
    function parseJob(list) {
        var rows = [], skippedFull = 0, skipped = 0, i;
        for (i = 0; i < (list || []).length; i++) {
            var it = list[i] || {};
            var size = normalizeSize(it.SIZE);
            var slv = normalizeSlv(it.SLV);
            if (!size || !slv) { skipped += 1; continue; }
            if (slv !== 'HAF' && slv !== 'FULL') { skipped += 1; continue; }
            if (bodyDim(size).w <= 0) { skipped += 1; continue; }
            rows.push({
                NAME: String(it.NAME || ''),
                NUMBER: String(it.NUMBER || ''),
                SIZE: size,
                SLV: slv,
                COMMENTS: String(it.COMMENTS || '')
            });
        }
        return { rows: rows, skippedFull: skippedFull, skipped: skipped };
    }

    function groupCopies(rows) {
        var map = {}, order = [], i;
        for (i = 0; i < rows.length; i++) {
            var s = rows[i].SIZE;
            if (!map[s]) {
                map[s] = { SIZE: s, copies: [], width: bodyDim(s).w, height: bodyDim(s).h };
                order.push(s);
            }
            map[s].copies.push(rows[i]);
        }
        order.sort(function (a, b) {
            var dw = map[b].width - map[a].width;
            if (Math.abs(dw) > 1e-6) return dw;
            return sizeIndex(a) - sizeIndex(b);
        });
        return order.map(function (s) { return map[s]; });
    }

    CN.chart = {
        SIZE_ORDER: SIZE_ORDER,
        LIST_ORDER: LIST_ORDER,
        SLV_KEYS: SLV_KEYS,
        RIB_KEYS: RIB_KEYS,
        sizeKeys: sizeKeys,
        sizeIndex: sizeIndex,
        bodyDim: bodyDim,
        sleeveDim: sleeveDim,
        parseJob: parseJob,
        groupCopies: groupCopies,
        normalizeSize: normalizeSize,
        normalizeSlv: normalizeSlv
    };
})(typeof window !== 'undefined' ? window : globalThis);
