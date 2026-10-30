# Notices

**BitVibe** is free software, licensed under the **GNU Affero General Public License v3.0**
(see `LICENSE`). Copyright (C) 2026 the BitVibe contributors. The complete corresponding source
code is at <https://github.com/d0ge5/BitVibe>.

BitVibe is an independent, unofficial software recreation of the behaviour of a guitar pedal,
written from the publicly available manual and published reviews. It is **not affiliated with,
endorsed by or sponsored by Meris**. "Meris" and "Ottobit" are trademarks of their respective owner
and are used here only to say what this software is modelled on.

## Third-party software

* **JUCE 8** — <https://juce.com>. Used under the AGPLv3 option of its dual licence
  (AGPLv3 / commercial JUCE licence). Its full licence text is in `third_party/JUCE/LICENSE.md`.
* **VST 3 SDK** (bundled inside JUCE) — Copyright (c) Steinberg Media Technologies GmbH,
  MIT License (`third_party/JUCE/modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt`).
  VST is a trademark of Steinberg Media Technologies GmbH, registered in Europe and other countries.
* **Audio Unit** support (macOS) uses Apple's Audio Unit framework through JUCE.

## Algorithms the code is based on (no third-party code is included)

* **Reverbs.** The Room and Hall reverbs use the classic Schroeder/Freeverb comb and allpass layout (Freeverb is
  public domain; the code here is our own). The **Plate** reverb is written from the block diagram and delay lengths
  published in Jon Dattorro, "Effect Design Part 1: Reverberator and Other Filters", *Journal of the Audio
  Engineering Society* 45(9), 1997; none of the existing implementations of it was used. The **Spring**, **Shimmer**,
  **Gated** and reversed reverbs are our own designs.
* **Overdrive and fuzz.** Modelled from the published circuit values of the two classic pedals they are named after
  in the README; no firmware, schematics' artwork or third-party code is included.

## Optional components that are not part of BitVibe

* **ADTOF** drum-transcription model (M. Zehren, M. Alunno, P. Bientinesi —
  <https://github.com/MZehren/ADTOF>, licensed CC BY-NC-SA 4.0, non-commercial) through the
  **ADTOF-pytorch** port (<https://github.com/xavriley/ADTOF-pytorch>), with PyTorch, librosa and their
  dependencies. None of these are included in BitVibe or its installers. `Tools/drums/setup_neural.bat`
  / `setup_neural.sh` download them into a separate folder on your own computer if you choose to, and
  BitVibe then runs `Tools/drums/bitvibe_adtof.py` as a separate program to analyse drum beats.
* The built-in drum analysis was tuned and measured on the **MDB Drums** (CC BY-NC-SA 4.0) and
  **IDMT-SMT-Drums** (CC BY-NC-ND 4.0) research datasets. They were used only for evaluation: no audio,
  annotations or spectral templates derived from them are included in BitVibe; a few scalar settings of the
  analysis (thresholds and weights) were chosen by measuring results against them.
