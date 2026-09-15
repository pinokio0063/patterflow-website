@echo off
chcp 65001 >nul
cd /d "%~dp0"

echo.
echo ========================================
echo  PatternFlow PC nest server
echo ========================================
echo  Website: https://nest.patternflow.fit/
echo  Ei window BAND korle nest bondho.
echo.

if not exist "%~dp0nest\custom-nest\backend\engine\nest.exe" (
  echo nest.exe nai. Age custom-nest\backend\engine\build.bat chalan.
  pause
  exit /b 1
)
if not exist "%~dp0nest\nesting\backend\temp.exe" (
  echo temp.exe nai. nest\nesting\backend\temp.exe copy korun.
  pause
  exit /b 1
)

where py >nul 2>&1
if errorlevel 1 (
  where python >nul 2>&1
  if errorlevel 1 (
    echo Python nai. python.org theke Python 3 install korun.
    pause
    exit /b 1
  )
)

echo Starting Custom Nest API  :9785
start "PF Custom Nest API" /D "%~dp0nest\custom-nest\backend" cmd /c "start.bat"

echo Starting Nesting API      :9786
start "PF Nesting API" /D "%~dp0nest\nesting\backend" cmd /c "start.bat"

timeout /t 2 /nobreak >nul

set "PC=%~dp0nest\pc-server"
if not exist "%PC%\config.yml" (
  echo.
  echo Tunnel setup HOYNAI.
  echo Age SETUP-CLOUDFLARE-TUNNEL.bat ekbar chalan.
  echo Local APIs chalu ache:
  echo   http://127.0.0.1:9785/health
  echo   http://127.0.0.1:9786/health
  echo.
  pause
  exit /b 1
)

set "CF="
if exist "%PC%\cloudflared.exe" set "CF=%PC%\cloudflared.exe"
if not defined CF where cloudflared >nul 2>&1 && set "CF=cloudflared"
if not defined CF if exist "C:\Program Files (x86)\cloudflared\cloudflared.exe" set "CF=C:\Program Files (x86)\cloudflared\cloudflared.exe"
if not defined CF if exist "C:\Program Files\cloudflared\cloudflared.exe" set "CF=C:\Program Files\cloudflared\cloudflared.exe"
if not defined CF (
  echo cloudflared nai. SETUP-CLOUDFLARE-TUNNEL.bat chalan.
  pause
  exit /b 1
)

echo Starting Cloudflare Tunnel...
echo nest-api.patternflow.fit  -^> 9785
echo nesting-api.patternflow.fit -^> 9786
echo Account: nest\pc-server\CLOUDFLARE-ACCOUNT.txt
echo.
pushd "%PC%"
"%CF%" tunnel --config "%PC%\config.yml" run
popd
echo.
echo Tunnel bondho. Custom Nest / Nesting window alada close korun.
pause
