#!/bin/sh
# Installs BitVibe's optional neural drum helper for the DRUMS tab (macOS):
# a Python environment in ~/Library/Application Support/BitVibe/neural with PyTorch, librosa and
# ADTOF-pytorch, plus the helper script. Needs Python 3.9 or newer (python3).
# Downloads about 350 MB. Run it again to repair or update; delete the folder to remove it.
#
# ADTOF and its weights are CC BY-NC-SA 4.0 (non-commercial) and are not part of BitVibe.
set -e

TARGET="$HOME/Library/Application Support/BitVibe/neural"
ADTOF="https://github.com/xavriley/ADTOF-pytorch/archive/85c192e78f716ea0b111cc8a5ee4a8f6a3a4f8a9.zip"
HERE="$(cd "$(dirname "$0")" && pwd)"

if ! command -v python3 >/dev/null 2>&1; then
    echo "Python 3.9 or newer is needed: https://www.python.org/downloads/"
    exit 1
fi

echo "Creating the Python environment in $TARGET ..."
[ -x "$TARGET/bin/python3" ] || python3 -m venv "$TARGET"
VPY="$TARGET/bin/python3"

echo "Installing PyTorch ..."
"$VPY" -m pip install --upgrade pip
"$VPY" -m pip install torch
echo "Installing librosa, soundfile, pretty_midi and numpy ..."
"$VPY" -m pip install numpy librosa soundfile pretty_midi
echo "Installing ADTOF-pytorch ..."
"$VPY" -m pip install "$ADTOF"

cp "$HERE/bitvibe_adtof.py" "$TARGET/bitvibe_adtof.py"
"$VPY" "$TARGET/bitvibe_adtof.py" --check

echo
echo "Done. BitVibe now analyses new beats with ADTOF (DRUMS tab, NEURAL HELPER on)."
echo "To remove it, delete \"$TARGET\"."
