/* Smoke test: parse sample SVGs in JS, run C++ nest.exe. */
var fs = require('fs');
var path = require('path');
var { spawnSync } = require('child_process');
var root = path.join(__dirname, '..');
function load(rel) { return fs.readFileSync(path.join(root, rel), 'utf8'); }
global.window = global;
eval(load('js/chart-data.js'));
eval(load('js/path.js'));

var CN = global.CustomNest;
var font = CN.path.svgToOutline(load('samples/FONT.svg'));
var back = CN.path.svgToOutline(load('samples/BACK.svg'));
var slv = CN.path.svgToOutline(load('samples/SLEEVE-SHORT.svg'));
var slvLong = CN.path.svgToOutline(load('samples/SLEEVE-LONG.svg'));
if (!font || !back || !slv || !slvLong) {
    console.error('parse fail', !!font, !!back, !!slv, !!slvLong);
    process.exit(1);
}

var exe = path.join(root, 'engine', 'nest.exe');
if (!fs.existsSync(exe)) {
    console.error('missing engine/nest.exe — run engine/build.bat');
    process.exit(1);
}

var payload = {
    dia: 63,
    minGap: 0.05,
    rowsPerDoc: 4,
    sleeveKey: 'short_slv_with_rib',
    job: JSON.parse(load('samples/demo-job.json')),
    chart: global.PF_CHART,
    masters: {
        font: { points: font.points },
        back: { points: back.points },
        sleeve: { points: slv.points },
        sleeveLong: { points: slvLong.points }
    }
};

var t0 = Date.now();
var proc = spawnSync(exe, [], { input: JSON.stringify(payload), encoding: 'utf8', maxBuffer: 32 * 1024 * 1024 });
if (proc.error) {
    console.error(proc.error);
    process.exit(1);
}
var result;
try { result = JSON.parse(proc.stdout); }
catch (e) {
    console.error('bad JSON from nest.exe');
    console.error(proc.stdout.slice(0, 400));
    console.error(proc.stderr);
    process.exit(1);
}
console.log('wall_ms', Date.now() - t0, 'engine_s', (result.elapsedMs / 1000).toFixed(3), 'engine', result.engine);
if (!result.ok) {
    console.error(result.error);
    process.exit(1);
}
console.log('pages', result.pages.length, 'bodyRows', result.bodyRowCount);
console.log('fabric_m', result.fabricMeters.toFixed(3), 'copies', result.job.rows.length);
result.pages.forEach(function (p) {
    console.log(' DOC', p.index, 'bodies', p.bodies.length, 'sleeves', p.sleeves.length,
        'h_in', p.height.toFixed(2));
});
if (result.warnings.length) console.log('warnings', result.warnings);
if (result.pages.length < 1 || result.bodyRowCount < 1) process.exit(1);
console.log('OK');
