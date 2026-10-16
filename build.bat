@echo off
rem Configures and builds with the Visual Studio 2026 C++ toolchain + Ninja.
rem Usage:  build.bat [target ...]        (default: all)
rem         build.bat ottobit_tests       (DSP tests only, no JUCE compile)
setlocal

set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo Could not find vcvars64.bat at "%VCVARS%". Edit build.bat to match your Visual Studio install.
    exit /b 1
)
call "%VCVARS%" >nul || exit /b 1

set "TARGETS=%*"
if "%TARGETS%"=="" set "TARGETS=all"

if not exist "%~dp0build\build.ninja" (
    cmake -S "%~dp0." -B "%~dp0build" -G Ninja -DCMAKE_BUILD_TYPE=Release ^
        -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl || exit /b 1
)
cmake --build "%~dp0build" --target %TARGETS% || exit /b 1
endlocal
