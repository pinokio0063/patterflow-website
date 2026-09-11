$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$dest = Join-Path $root "nest\pc-server\config.yml"
$cfDir = Join-Path $env:USERPROFILE ".cloudflared"
if (-not (Test-Path $cfDir)) {
    throw ".cloudflared folder not found. Run cloudflared tunnel login first."
}

$cred = Get-ChildItem $cfDir -Filter "*.json" |
    Where-Object { $_.Name -match "^[0-9a-f-]{36}\.json$" } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if (-not $cred) {
    throw "Tunnel credentials JSON not found in $cfDir"
}

$uuid = [System.IO.Path]::GetFileNameWithoutExtension($cred.Name)
$credPath = $cred.FullName -replace "\\", "/"
$yml = @"
tunnel: $uuid
credentials-file: $credPath
protocol: http2
originRequest:
  connectTimeout: 30s
  keepAliveTimeout: 90s
  disableChunkedEncoding: true
ingress:
  - hostname: nest-api.patternflow.fit
    service: http://127.0.0.1:8765
  - hostname: nesting-api.patternflow.fit
    service: http://127.0.0.1:8766
  - service: http_status:404
"@
$outDir = Split-Path $dest -Parent
if (-not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir | Out-Null
}
[System.IO.File]::WriteAllText($dest, $yml)
Write-Host "Wrote $dest"
Write-Host "tunnel $uuid"
