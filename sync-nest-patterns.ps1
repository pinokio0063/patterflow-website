$src = Join-Path $PSScriptRoot "nest\itam\pattern"
$dst = Join-Path $PSScriptRoot "nest\custom-nest\fontend\patterns"
if (-not (Test-Path $src)) { exit 0 }
New-Item -ItemType Directory -Force -Path $dst | Out-Null
Copy-Item "$src\*" $dst -Recurse -Force

function Classify([string]$name) {
  $n = $name.ToLowerInvariant()
  if ($n -match 'front|font') { return 'font' }
  if ($n -match 'back') { return 'back' }
  if ($n -match 'short|shrot|haf|half') { return 'sleeve' }
  if ($n -match 'long|full') { return 'sleeveLong' }
  return $null
}

$catalog = @()
Get-ChildItem $src -Directory | ForEach-Object {
  $item = @{ id = $_.Name; name = $_.Name; font = $null; back = $null; sleeve = $null; sleeveLong = $null }
  Get-ChildItem $_.FullName -File -Filter *.svg | ForEach-Object {
    $role = Classify $_.Name
    if ($role) { $item[$role] = ('patterns/' + $_.Directory.Name + '/' + $_.Name) }
  }
  $catalog += $item
}
$json = $catalog | ConvertTo-Json -Depth 5
if ($catalog.Count -eq 1) { $json = "[$json]" }
Set-Content -Encoding utf8 (Join-Path $dst "index.json") $json
