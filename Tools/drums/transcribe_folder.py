#!/usr/bin/env python3
"""Runs the neural helper over every WAV file in a folder (development tool, for scoring ADTOF with
bitvibe_drum_eval --from-json):

    python transcribe_folder.py <audio folder> <json folder> [--contains TEXT] [--device cpu|cuda]

Run it with the helper's own Python (for example %LOCALAPPDATA%\\BitVibe\\neural\\Scripts\\python.exe).
"""

import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bitvibe_adtof  # noqa: E402


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("audio_dir")
    parser.add_argument("json_dir")
    parser.add_argument("--contains", default="", help="only files whose name contains this text")
    parser.add_argument("--device", default="cpu", choices=["cpu", "cuda"])
    args = parser.parse_args()
    os.makedirs(args.json_dir, exist_ok=True)
    files = sorted(f for f in os.listdir(args.audio_dir) if f.lower().endswith(".wav") and args.contains in f)
    audio_seconds = 0.0
    started = time.time()
    for name in files:
        result = bitvibe_adtof.transcribe(os.path.join(args.audio_dir, name), args.device)
        audio_seconds += result["duration"]
        with open(os.path.join(args.json_dir, os.path.splitext(name)[0] + ".json"), "w", encoding="utf-8") as f:
            json.dump(result, f)
        print(f"{name}: {len(result['hits'])} hits")
    elapsed = time.time() - started
    print(f"{len(files)} files, {audio_seconds / 60:.1f} min of audio in {elapsed:.0f} s "
          f"({elapsed / max(audio_seconds / 60, 1e-9):.1f} s per minute of audio)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
