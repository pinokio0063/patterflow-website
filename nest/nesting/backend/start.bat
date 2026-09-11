@echo off
cd /d "%~dp0"

if not exist "temp.exe" (
  echo temp.exe missing in this folder.
  echo Copy it from the Illustrator Pattern extension:
  echo   ...\Pattern\client\js\temp\bin\temp.exe
  pause
  exit /b 1
)

echo Freeing port 8766 if an old server is still running...
for /f "tokens=5" %%P in ('netstat -ano ^| findstr ":8766" ^| findstr "LISTENING"') do (
  echo Killing PID %%P
  taskkill /F /PID %%P >nul 2>&1
)

echo PatternFlow Nesting API  (Sparrow temp.exe)
echo Listen: http://127.0.0.1:8766/
echo Close THIS window to stop the server.

where py >nul 2>&1
if %errorlevel%==0 (
  py -3 server.py
) else (
  python server.py
)
if errorlevel 1 (
  echo.
  echo Server failed. Close any other window using port 8766, then run start.bat again.
)
pause
