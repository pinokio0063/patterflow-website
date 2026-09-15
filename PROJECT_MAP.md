# PatternFlow website — Project Map

Marketing site and tools for PatternFlow (Illustrator jersey nesting). Static pages live on Cloudflare Workers + assets. Nesting engines run on the owner’s Windows PC.

## Tech stack

- Static HTML/CSS/JS on Cloudflare Workers (`wrangler *.jsonc`)
- Custom nest: C++ `nest.exe` + Python HTTP (`nest/custom-nest/backend`)
- Sparrow nest: `temp.exe` + Python HTTP (`nest/nesting/backend`)
- Public reach: Cloudflare Tunnel (`cloudflared`) from the PC to `*.patternflow.fit`

## Live sites

| URL | Source | Wrangler |
|---|---|---|
| https://patternflow.fit/ | `index.html` | git-connected `patterflow-website` |
| https://use.patternflow.fit/ | `use/` | `wrangler.use.jsonc` |
| https://price.patternflow.fit/ | `price/` | `wrangler.price.jsonc` |
| https://layouts.patternflow.fit/ | `layout/` | `wrangler.layouts.jsonc` |
| https://sheet.patternflow.fit/ | `DATA-SHEET2/` | `wrangler.sheet.jsonc` |
| https://library.patternflow.fit/ | `laybary/` | `wrangler.library.jsonc` |
| https://nest.patternflow.fit/ | `nest/custom-nest/fontend/` | `wrangler.nest.jsonc` |

One-click site deploy: `PUSH-ALL.bat` (commit, push, deploy workers). Nest patterns: `sync-nest-patterns.ps1` before nest deploy.

## Nest compare

Public UI compares **Nesting (Sparrow)** vs **Custom (`nest.exe`)**. SIMULATE POSTs the same job JSON to both APIs. API bases are in `nest/custom-nest/fontend/js/config.js`. The settings gear holds gap/rows/rib plus an editable per-size W×H chart (body, short, long). Polo defaults to without rib. The live `PF_CHART` object is what `/simulate` sends.

Patterns: drop a style folder in `nest/itam/pattern/{name}/`. `PUSH-ALL.bat` copies into `nest/custom-nest/fontend/patterns/`.

## PC nest server (2–3 users)

The website cannot run `.exe` on Cloudflare. The owner PC stays on 24h and serves the engines.

1. Once: `SETUP-CLOUDFLARE-TUNNEL.bat` (Cloudflare login, tunnel + DNS)
2. Daily: `START-PC-SERVER.bat`
3. Sleep = Never while plugged in

| Public URL | Local |
|---|---|
| https://nest-api.patternflow.fit | `127.0.0.1:9785` Custom `nest.exe` |
| https://nesting-api.patternflow.fit | `127.0.0.1:9786` Sparrow `temp.exe` |

APIs are JSON-only (`POST /simulate`, `GET /health`, `GET /queue`). They do not serve the exe files. Up to **4 jobs run at the same time**. A 5th user waits for a free slot; nest time starts only after the job begins. `GET /queue?ticket=` reports that user’s waiting/running status.

Tunnel files live in `nest/pc-server/` (copy the whole Home page folder to another Windows PC). Daily start: `START-PC-SERVER.bat`. Account notes: `nest/pc-server/CLOUDFLARE-ACCOUNT.txt`. Credentials JSON is local (not git).

Later move to a Windows VPS: copy the two backend folders, run the same Python servers, change only the two URLs in `config.js`.
