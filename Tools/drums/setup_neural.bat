@echo off
rem Installs BitVibe's optional neural drum helper for the DRUMS tab (Windows):
rem a Python environment in %LOCALAPPDATA%\BitVibe\neural with PyTorch (CPU), librosa and ADTOF-pytorch,
rem plus the helper script. Needs Python 3.9 or newer ("py" launcher or "python" on PATH).
rem Downloads about 350 MB. Run it again to repair or update; delete the folder to remove it.
rem
rem ADTOF and its weights are CC BY-NC-SA 4.0 (non-commercial) and are not part of BitVibe.
setlocal
set "TARGET=%LOCALAPPDATA%\BitVibe\neural"
set "ADTOF=https://github.com/xavriley/ADTOF-pytorch/archive/85c192e78f716ea0b111cc8a5ee4a8f6a3a4f8a9.zip"

set "PY=py -3"
where py >nul 2>nul || set "PY=python"
%PY% --version >nul 2>nul || (
    echo Python 3.9 or newer is needed: https://www.python.org/downloads/
    exit /b 1
)

echo Creating the Python environment in "%TARGET%" ...
if not exist "%TARGET%\Scripts\python.exe" (
    %PY% -m venv "%TARGET%" || goto :fail
)
set "VPY=%TARGET%\Scripts\python.exe"

echo Installing PyTorch (CPU build) ...
"%VPY%" -m pip install --upgrade pip || goto :fail
"%VPY%" -m pip install torch --index-url https://download.pytorch.org/whl/cpu || goto :fail
echo Installing librosa, soundfile, pretty_midi and numpy ...
"%VPY%" -m pip install numpy librosa soundfile pretty_midi || goto :fail
echo Installing ADTOF-pytorch ...
"%VPY%" -m pip install "%ADTOF%" || goto :fail

copy /y "%~dp0bitvibe_adtof.py" "%TARGET%\bitvibe_adtof.py" >nul || goto :fail
"%VPY%" "%TARGET%\bitvibe_adtof.py" --check || goto :fail

echo.
echo Done. BitVibe now analyses new beats with ADTOF (DRUMS tab, NEURAL HELPER on).
echo To remove it, delete "%TARGET%".
exit /b 0

:fail
echo.
echo Setup failed (see the messages above). BitVibe keeps using its built-in analysis.
exit /b 1
