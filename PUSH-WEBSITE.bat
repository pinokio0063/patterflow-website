@echo off
chcp 65001 >nul
cd /d "%~dp0"

echo.
echo ========================================
echo   PatternFlow website - Git update
echo ========================================
echo.

git --version >nul 2>&1
if errorlevel 1 (
  echo Git installed nei. Age Git for Windows install korun.
  pause
  exit /b 1
)

git rev-parse --is-inside-work-tree >nul 2>&1
if errorlevel 1 (
  echo Ei folder e Git repo nei.
  pause
  exit /b 1
)

echo Current changes:
echo.
git status
echo.

set "MSG="
set /p MSG=Commit message likhun (Enter = Update website): 
if "%MSG%"=="" set "MSG=Update website"

git add -A
git diff --cached --quiet
if errorlevel 1 (
  git commit -m "%MSG%"
  if errorlevel 1 (
    echo.
    echo Commit fail hoise.
    pause
    exit /b 1
  )
) else (
  echo.
  echo Notun change nai. Ager commit-i push hobe.
)

echo.
echo GitHub e push hochche...
git push origin main
if errorlevel 1 (
  echo.
  echo Push fail hoise. Internet ba GitHub login check korun.
  pause
  exit /b 1
)

echo.
echo Done. Cloudflare kichu minute er moddhe site update korbe.
echo Live: https://patternflow.fit/
echo.
pause
