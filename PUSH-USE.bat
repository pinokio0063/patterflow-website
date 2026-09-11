@echo off
chcp 65001 >nul
cd /d "%~dp0"

echo.
echo ========================================
echo   use.patternflow.fit - Git + Deploy
echo ========================================
echo.

git add use
git status
echo.

set "MSG="
set /p MSG=Commit message (Enter = Update How to use): 
if "%MSG%"=="" set "MSG=Update How to use site"

git diff --cached --quiet
if errorlevel 1 (
  git commit -m "%MSG%"
  if errorlevel 1 (
    echo Commit fail hoise.
    pause
    exit /b 1
  )
)

git push origin main
if errorlevel 1 (
  echo Push fail hoise.
  pause
  exit /b 1
)

echo.
echo Cloudflare e deploy hochche...
call npx --yes wrangler deploy --config wrangler.use.jsonc
if errorlevel 1 (
  echo Deploy fail hoise.
  pause
  exit /b 1
)

echo.
echo Done. Live: https://use.patternflow.fit/
echo.
pause
