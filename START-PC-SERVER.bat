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

echo Starting Custom Nest API  :8765
start "PF Custom Nest API" /D "%~dp0nest\custom-nest\backend" cmd /c "start.bat"

echo Starting Nesting API      :8766
start "PF Nesting API" /D "%~dp0nest\nesting\backend" cmd /c "start.bat"

timeout /t 2 /nobreak >nul

if not exist "%~dp0nest\pc-server\config.yml" (
  echo.
  echo Tunnel setup HOYNAI.
  echo Age SETUP-CLOUDFLARE-TUNNEL.bat ekbar chalan.
  echo Local APIs chalu ache:
  echo   http://127.0.0.1:8765/health
  echo   http://127.0.0.1:8766/health
  echo.
  pause
  exit /b 1
)

where cloudflared >nul 2>&1
if errorlevel 1 (
  echo cloudflared nai. SETUP-CLOUDFLARE-TUNNEL.bat chalan.
  pause
  exit /b 1
)

echo Starting Cloudflare Tunnel...
echo nest-api.patternflow.fit  -^> 8765
echo nesting-api.patternflow.fit -^> 8766
echo.
cloudflared tunnel --config "%~dp0nest\pc-server\config.yml" run
echo.
echo Tunnel bondho. Custom Nest / Nesting window alada close korun.
pause
