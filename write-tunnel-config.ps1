$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$pc = Join-Path $root "nest\pc-server"
if (-not (Test-Path $pc)) {
    New-Item -ItemType Directory -Path $pc | Out-Null
}

$cfDir = Join-Path $env:USERPROFILE ".cloudflared"
$localJson = Get-ChildItem $pc -Filter "*.json" -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match "^[0-9a-f-]{36}\.json$" } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1

if (-not $localJson) {
    if (-not (Test-Path $cfDir)) {
        throw ".cloudflared folder not found. Run SETUP-CLOUDFLARE-TUNNEL.bat first."
    }
    $cred = Get-ChildItem $cfDir -Filter "*.json" |
        Where-Object { $_.Name -match "^[0-9a-f-]{36}\.json$" } |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1
    if (-not $cred) {
        throw "Tunnel credentials JSON not found in $cfDir"
    }
    Copy-Item $cred.FullName (Join-Path $pc $cred.Name) -Force
    $localJson = Get-Item (Join-Path $pc $cred.Name)
}

$certSrc = Join-Path $cfDir "cert.pem"
if (Test-Path $certSrc) {
    Copy-Item $certSrc (Join-Path $pc "cert.pem") -Force
}

$uuid = [System.IO.Path]::GetFileNameWithoutExtension($localJson.Name)
$yml = @"
tunnel: $uuid
credentials-file: $($localJson.Name)
protocol: http2
originRequest:
  connectTimeout: 30s
  keepAliveTimeout: 90s
  disableChunkedEncoding: true
ingress:
  - hostname: nest-api.patternflow.fit
    service: http://127.0.0.1:9785
  - hostname: nesting-api.patternflow.fit
    service: http://127.0.0.1:9786
  - service: http_status:404
"@
[System.IO.File]::WriteAllText((Join-Path $pc "config.yml"), $yml)
Write-Host "Wrote $($pc)\config.yml"
Write-Host "tunnel $uuid"
Write-Host "credentials $($localJson.Name)"
