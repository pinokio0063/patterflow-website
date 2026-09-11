/**
 * Nest math lives in engine/nest.exe (C++).
 * The preview UI POSTs job + outlines to /simulate.
 */
(function (global) {
    'use strict';
    var CN = global.CustomNest = global.CustomNest || {};
    CN.packer = {
        simulate: function () {
            throw new Error('Nest calculation runs in C++ (POST /simulate). Use start.bat.');
        }
    };
})(typeof window !== 'undefined' ? window : globalThis);
