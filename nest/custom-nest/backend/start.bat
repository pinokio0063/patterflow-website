@echo off
cd /d "%~dp0"

if not exist "engine\nest.exe" (
  echo Building C++ nest engine...
  call engine\build.bat
  if errorlevel 1 (
    echo.
    echo Build failed. Install Visual Studio C++ and try again.
    pause
    exit /b 1
  )
)

echo Freeing port 9785 if an old server is still running...
for /f "tokens=5" %%P in ('netstat -ano ^| findstr ":9785" ^| findstr "LISTENING"') do (
  echo Killing PID %%P
  taskkill /F /PID %%P >nul 2>&1
)

echo PatternFlow Custom Nest API  (C++ engine)
echo Listen: http://127.0.0.1:9785/
echo Close THIS window to stop the server.

where py >nul 2>&1
if %errorlevel%==0 (
  py -3 server.py
) else (
  python server.py
)
if errorlevel 1 (
  echo.
  echo Server failed. Close any other window using port 9785, then run start.bat again.
)
pause
