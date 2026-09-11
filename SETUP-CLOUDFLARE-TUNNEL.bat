@echo off
chcp 65001 >nul
cd /d "%~dp0"

echo.
echo ========================================
echo  PatternFlow - Cloudflare Tunnel setup
echo ========================================
echo.
echo Browser e Cloudflare login hobe.
echo Zone select: patternflow.fit
echo.

where cloudflared >nul 2>&1
if errorlevel 1 (
  echo cloudflared nai. winget diye install hocche...
  winget install --id Cloudflare.cloudflared -e --accept-package-agreements --accept-source-agreements
  if errorlevel 1 (
    echo Install fail. Download: https://github.com/cloudflare/cloudflared/releases
    pause
    exit /b 1
  )
  echo Install shesh. Ei window BAND kore SETUP-CLOUDFLARE-TUNNEL.bat abar chalan.
  pause
  exit /b 0
)

echo [1/4] Cloudflare login...
cloudflared tunnel login
if errorlevel 1 (
  echo Login fail. patternflow.fit select koresen kina check korun.
  pause
  exit /b 1
)

echo.
echo [2/4] Tunnel create (patternflow-pc)...
cloudflared tunnel create patternflow-pc
if errorlevel 1 (
  echo Tunnel already thakte pare. List:
  cloudflared tunnel list
)

echo.
echo [3/4] DNS: nest-api + nesting-api ...
cloudflared tunnel route dns patternflow-pc nest-api.patternflow.fit
cloudflared tunnel route dns patternflow-pc nesting-api.patternflow.fit

echo.
echo [4/4] config.yml likhchi...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0write-tunnel-config.ps1"
if errorlevel 1 (
  echo config.yml fail.
  pause
  exit /b 1
)

echo.
echo Done. Ekhon START-PC-SERVER.bat chalan.
echo Site: https://nest.patternflow.fit/
echo APIs: https://nest-api.patternflow.fit/health
echo       https://nesting-api.patternflow.fit/health
echo.
pause
