@echo off
rem Builds the Windows installer into dist\.  Run build.bat first so the plugin exists in build\.
rem Needs Inno Setup 6:  winget install JRSoftware.InnoSetup
setlocal

set "ROOT=%~dp0..\.."
set /p VERSION=<"%ROOT%\VERSION"

set "ISCC="
if exist "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not defined ISCC (
    echo Inno Setup 6 not found. Install it with:  winget install JRSoftware.InnoSetup
    exit /b 1
)

if not exist "%ROOT%\build\BitVibe_artefacts\Release\VST3\BitVibe.vst3" (
    echo The plugin has not been built yet. Run build.bat first.
    exit /b 1
)

"%ISCC%" /Qp /DMyAppVersion=%VERSION% "%~dp0BitVibe.iss" || exit /b 1
echo.
echo Installer written to %ROOT%\dist
endlocal
