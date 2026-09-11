@echo off
setlocal EnableExtensions
cd /d "%~dp0"
set "CL="

set "HAVECL="
for /f "delims=" %%C in ('where cl 2^>nul') do (
  set "HAVECL=1"
  goto :have_cl
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%V in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    if exist "%%V\VC\Auxiliary\Build\vcvars64.bat" (
      call "%%V\VC\Auxiliary\Build\vcvars64.bat" >nul
      goto :try_cl
    )
  )
)

:try_cl
for /f "delims=" %%C in ('where cl 2^>nul') do (
  set "HAVECL=1"
  goto :have_cl
)

echo ERROR: Visual Studio C++ compiler not found.
echo Install "Desktop development with C++" and run this again.
exit /b 1

:have_cl
echo Building PatternFlow nest engine (C++)...
cl /nologo /O2 /EHsc /std:c++17 /W3 /DNDEBUG pf_nest.cpp /Fe:nest.exe
if errorlevel 1 exit /b 1
if exist pf_nest.obj del /q pf_nest.obj
echo OK: %~dp0nest.exe
exit /b 0
