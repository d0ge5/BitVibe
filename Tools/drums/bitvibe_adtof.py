#!/usr/bin/env python3
"""BitVibe's optional neural drum helper.

Transcribes a drum recording with ADTOF and writes the hits as JSON for the DRUMS tab:

    python bitvibe_adtof.py <audio file> <output.json> [--device cpu|cuda]
    python bitvibe_adtof.py --check          # verifies that everything is installed

Output (hits sorted by time):

    {"engine": "adtof", "version": 1, "fps": 100, "duration": 9.6,
     "hits": [{"t": 0.05, "drum": "kick", "p": 0.81, "raw": 0.74}, ...]}

"t" is in seconds, "drum" one of kick / snare / hihat / cymbal / tom, "raw" the network's activation at
the peak and "p" the same value rescaled so that 0.3 sits where ADTOF's own published per-class
thresholds sit. BitVibe's default Detect setting of 0.3 therefore behaves like ADTOF's tuned defaults.
Peaks are picked with a low threshold, so the plug-in can trade misses against false hits live.

ADTOF (M. Zehren, M. Alunno, P. Bientinesi; https://github.com/MZehren/ADTOF) is CC BY-NC-SA 4.0 and
is used here through the ADTOF-pytorch port (https://github.com/xavriley/ADTOF-pytorch). Neither is part
of BitVibe: setup_neural.bat / setup_neural.sh install them on your own machine, and BitVibe only runs
this script as a separate program.
"""

import argparse
import json
import sys

FPS = 100
# ADTOF's class order (General MIDI 35, 38, 47, 42, 49) and its published peak-picking thresholds.
CLASSES = ["kick", "snare", "tom", "hihat", "cymbal"]
ADTOF_THRESHOLDS = [0.22, 0.24, 0.32, 0.22, 0.30]
REPORT_FLOOR = 0.05      # raw activations below this are not reported
BITVIBE_DEFAULT = 0.3    # BitVibe's default Detect setting


def rescale(raw, threshold):
    """Monotonic map that sends ADTOF's threshold to BitVibe's default (0 -> 0, 1 -> 1)."""
    if raw <= threshold:
        return BITVIBE_DEFAULT * raw / threshold
    return BITVIBE_DEFAULT + (1.0 - BITVIBE_DEFAULT) * (raw - threshold) / (1.0 - threshold)


def pick_peaks(act, fps, floor):
    """madmom-style note peak picking (as used by ADTOF): subtract a moving average, keep local maxima
    above the floor, merge peaks closer than 20 ms. Returns (time, activation) pairs."""
    import numpy as np

    act = np.asarray(act, dtype=np.float32).reshape(-1)
    n = act.size
    if n == 0:
        return []
    pre_avg, post_avg = int(round(0.10 * fps)), int(round(0.01 * fps))
    pre_max, post_max = int(round(0.02 * fps)), int(round(0.01 * fps))
    combine = max(1, int(round(0.02 * fps)))

    padded = np.pad(act, (pre_avg, post_avg), mode="edge")
    kernel = np.ones(pre_avg + post_avg + 1, dtype=np.float32) / (pre_avg + post_avg + 1)
    detrended = np.maximum(0.0, act - np.convolve(padded, kernel, mode="valid"))

    padded = np.pad(detrended, (pre_max, post_max), mode="edge")
    windows = np.stack([padded[k:k + n] for k in range(pre_max + post_max + 1)])
    is_peak = (detrended >= windows.max(axis=0)) & (detrended > 0.0) & (act >= floor)

    peaks = []
    for i in np.flatnonzero(is_peak):
        if peaks and i - peaks[-1] <= combine:
            if act[i] > act[peaks[-1]]:
                peaks[-1] = int(i)
            continue
        peaks.append(int(i))
    return [(i / fps, float(act[i])) for i in peaks]


def transcribe(audio_path, device):
    import numpy as np
    from adtof_pytorch import transcribe_to_midi

    # With return_activations the MIDI path is not written.
    pred = transcribe_to_midi(audio_path, "unused.mid", return_activations=True, device=device, fps=FPS)
    act = np.asarray(pred)[0]  # [frames, 5]
    hits = []
    for c, name in enumerate(CLASSES):
        for t, raw in pick_peaks(act[:, c], FPS, REPORT_FLOOR):
            hits.append({"t": round(t, 4), "drum": name, "p": round(rescale(raw, ADTOF_THRESHOLDS[c]), 4), "raw": round(raw, 4)})
    hits.sort(key=lambda h: (h["t"], CLASSES.index(h["drum"])))
    return {"engine": "adtof", "version": 1, "fps": FPS, "duration": round(act.shape[0] / FPS, 3), "hits": hits}


def check():
    import numpy
    import torch
    import adtof_pytorch  # noqa: F401
    import librosa
    print(f"ok: torch {torch.__version__}, numpy {numpy.__version__}, librosa {librosa.__version__}, "
          f"weights {adtof_pytorch.get_default_weights_path()}")


def main():
    parser = argparse.ArgumentParser(description="BitVibe neural drum helper (ADTOF)")
    parser.add_argument("audio", nargs="?")
    parser.add_argument("output", nargs="?")
    parser.add_argument("--device", default="cpu", choices=["cpu", "cuda"])
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        if args.check:
            check()
            return 0
        if not args.audio or not args.output:
            parser.error("give an audio file and an output file")
        result = transcribe(args.audio, args.device)
        with open(args.output, "w", encoding="utf-8") as f:
            json.dump(result, f)
        print(f"{len(result['hits'])} hits in {result['duration']} s")
        return 0
    except Exception as e:  # one line for BitVibe's status display
        print(f"{type(e).__name__}: {e}".replace("\n", " ")[:300], file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
