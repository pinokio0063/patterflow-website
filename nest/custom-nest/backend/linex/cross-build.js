/* One-shot: download Zig into .toolchain and cross-compile nest for Linux. */
const fs = require("fs");
const path = require("path");
const https = require("https");
const { execFileSync, execSync } = require("child_process");

const root = __dirname;
const tc = path.join(root, ".toolchain");
fs.mkdirSync(tc, { recursive: true });
const url = "https://ziglang.org/download/0.14.1/zig-x86_64-windows-0.14.1.zip";
const zip = path.join(tc, "zig.zip");

function get(u) {
    return new Promise((resolve, reject) => {
        https
            .get(u, { headers: { "User-Agent": "pf-nest-linux-build" } }, (r) => {
                if (r.statusCode >= 300 && r.statusCode < 400 && r.headers.location) {
                    get(r.headers.location).then(resolve, reject);
                    return;
                }
                if (r.statusCode !== 200) {
                    reject(new Error("HTTP " + r.statusCode));
                    return;
                }
                const f = fs.createWriteStream(zip);
                r.pipe(f);
                f.on("finish", () => f.close(resolve));
            })
            .on("error", reject);
    });
}

function findZig() {
    const walk = (dir, depth) => {
        if (depth < 0) return null;
        let names;
        try {
            names = fs.readdirSync(dir);
        } catch (e) {
            return null;
        }
        for (const n of names) {
            const p = path.join(dir, n);
            if (n === "zig.exe" && fs.statSync(p).isFile()) return p;
        }
        for (const n of names) {
            const p = path.join(dir, n);
            try {
                if (fs.statSync(p).isDirectory() && n !== "doc" && n !== "lib") {
                    const hit = walk(p, depth - 1);
                    if (hit) return hit;
                }
            } catch (e) { /* skip */ }
        }
        return null;
    };
    return walk(tc, 3);
}

(async () => {
    if (!fs.existsSync(zip) || fs.statSync(zip).size < 1000000) {
        console.log("Downloading zig 0.14.1 ...");
        await get(url);
    }
    console.log("zip bytes", fs.statSync(zip).size);
    if (!findZig()) {
        console.log("Extracting ...");
        execFileSync(
            "powershell.exe",
            [
                "-NoProfile",
                "-Command",
                "Expand-Archive -Force -LiteralPath '" + zip.replace(/'/g, "''") +
                    "' -DestinationPath '" + tc.replace(/'/g, "''") + "'"
            ],
            { stdio: "inherit" }
        );
    }
    const zig = findZig();
    if (!zig) {
        console.error("zig.exe not found after extract");
        process.exit(1);
    }
    console.log("zig", zig);
    const out = path.join(root, "nest");
    const src = path.join(root, "pf_nest.cpp");
    console.log("Cross-compiling x86_64-linux-gnu ...");
    execFileSync(
        zig,
        [
            "c++",
            "-target",
            "x86_64-linux-gnu",
            "-O2",
            "-std=c++17",
            "-DNDEBUG",
            src,
            "-o",
            out
        ],
        { cwd: root, stdio: "inherit" }
    );
    const buf = fs.readFileSync(out);
    const elf = buf[0] === 0x7f && buf[1] === 0x45 && buf[2] === 0x4c && buf[3] === 0x46;
    console.log("bytes", buf.length, elf ? "ELF OK" : "NOT ELF");
    if (!elf) process.exit(2);
    console.log("OK", out);
})().catch((e) => {
    console.error(e);
    process.exit(1);
});
