@echo off
chcp 65001 >nul
cd /d "%~dp0"

echo.
echo ========================================
echo   PatternFlow - push and deploy ALL
echo ========================================
echo.

git --version >nul 2>&1
if errorlevel 1 (
  echo Git installed nei.
  pause
  exit /b 1
)

echo Current changes:
git status --short
echo.

set "MSG="
set /p MSG=Commit message (Enter = Update all PatternFlow sites): 
if "%MSG%"=="" set "MSG=Update all PatternFlow sites"

git add -A
git diff --cached --quiet
if errorlevel 1 (
  git commit -m "%MSG%"
  if errorlevel 1 (
    echo Commit fail hoise.
    pause
    exit /b 1
  )
) else (
  echo Notun change nai. Ager commit-i push hobe.
)

echo.
echo [1/7] GitHub e push...
git push origin main
if errorlevel 1 (
  echo Push fail hoise.
  pause
  exit /b 1
)

echo.
echo [2/7] Deploy use.patternflow.fit ...
call npx --yes wrangler deploy --config wrangler.use.jsonc
if errorlevel 1 goto :deployfail

echo.
echo [3/7] Deploy price.patternflow.fit ...
call npx --yes wrangler deploy --config wrangler.price.jsonc
if errorlevel 1 goto :deployfail

echo.
echo [4/7] Deploy layouts.patternflow.fit ...
call npx --yes wrangler deploy --config wrangler.layouts.jsonc
if errorlevel 1 goto :deployfail

echo.
echo [5/7] Deploy sheet.patternflow.fit ...
call npx --yes wrangler deploy --config wrangler.sheet.jsonc
if errorlevel 1 goto :deployfail

echo.
echo [6/7] Deploy library.patternflow.fit ...
call npx --yes wrangler deploy --config wrangler.library.jsonc
if errorlevel 1 goto :deployfail

echo.
echo Syncing nest patterns...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0sync-nest-patterns.ps1"

echo.
echo [7/7] Deploy nest.patternflow.fit ...
call npx --yes wrangler deploy --config wrangler.nest.jsonc
if errorlevel 1 goto :deployfail

echo.
echo ========================================
echo   Done
echo ========================================
echo Home:    https://patternflow.fit/
echo How-to:  https://use.patternflow.fit/
echo Price:   https://price.patternflow.fit/
echo Layouts: https://layouts.patternflow.fit/
echo Sheet:   https://sheet.patternflow.fit/
echo Library: https://library.patternflow.fit/
echo Nest:    https://nest.patternflow.fit/
echo.
pause
exit /b 0

:deployfail
echo.
echo Cloudflare deploy fail hoise.
pause
exit /b 1
