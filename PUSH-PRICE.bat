@echo off
chcp 65001 >nul
cd /d "%~dp0"

echo.
echo ========================================
echo   price.patternflow.fit - Git + Deploy
echo ========================================
echo.

git add price wrangler.price.jsonc
git status
echo.

set "MSG="
set /p MSG=Commit message (Enter = Update Pricing): 
if "%MSG%"=="" set "MSG=Update Pricing site"

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
call npx --yes wrangler deploy --config wrangler.price.jsonc
if errorlevel 1 (
  echo Deploy fail hoise.
  pause
  exit /b 1
)

echo.
echo Done. Live: https://price.patternflow.fit/
echo.
pause
