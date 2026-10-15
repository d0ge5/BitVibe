# Ottobit Jr. Replica (VST3)

An unofficial software recreation of the Meris Ottobit Jr. bitcrusher / filter / stutter / sequencer
pedal, built from the behaviour documented in the official manual (v6) and published reviews. It is a
behavioural re-implementation written from scratch: no Meris code, firmware or artwork is used.
"Meris" and "Ottobit" are trademarks of their owner; if you ever distribute this, give it its own
name and face.

## Controls

Front panel (same layout as the pedal):

| Knob | Range | Alt (second layer) |
| --- | --- | --- |
| **Sample** | 48 Hz – 48 kHz sample-rate reduction (fully clockwise = off) | Step 1 |
| **Filter** | 96 Hz – 24 kHz ladder low-pass (fully clockwise = bypassed) | Step 2 |
| **Bits** | 24 bit down to 1 bit (fully clockwise = off); noise gate engages below 8 bit | Step 3 |
| **Stutter** | 23 positions: off, then full / double / half speed x (1, 2, 3, 4, 6, 8, 16 repeats), then random | Step 4 |
| **Sequencer** | Off, once, twice, 4x, 8x, continuous (number of passes through the sequence) | Step 5 |
| **Seq Mult** | x1 … x128 (eight steps), multiplies the tap tempo | Step 6 |

* **ALT** (click to toggle) swaps the six knobs to the six sequencer steps. Knob at minimum = **skip**
  the step, at maximum = **mute** it.
* **SEQ TYPE** cycles Pitch → Filter → Sample Rate (the two small LEDs show it; both off = Sample Rate).
  * *Pitch*: every step is -12 … +12 semitones, centre = dry (27 choices incl. skip and mute).
  * *Filter / Sample Rate*: every step is 0–100 % of the front-panel Filter / Sample value
    (noon = 50 %), so the sequence always tracks the front-panel knob.
* **TAP** sets the tempo (23.4 – 6000 BPM, so the sequencer can run in the audio band for ring-mod
  tones). **Hold** TAP (or use the HOLD button / automate *Stutter Hold*) to freeze the stutter buffer —
  you can press it before playing so the next note gets frozen.
* **BYPASS** switches the effect in and out (also wired to the host's bypass).
* The stutter and the sequencer are triggered by **pick-attack detection** on the input. If it misses
  or false-triggers, adjust **TRIGGER** (sensitivity) in the global panel, and try feeding it the dry
  signal first, as the manual recommends.

Global panel (extras that a plugin needs; the hardware keeps equivalents in its global-settings mode):
tempo, input / output gain, trigger sensitivity, *Sync* to the host tempo, *Hold*, headroom
(instrument vs line level) and the stutter snippet length (1/4, 1/8 or 1/16 note).

The factory sound is Preset 1: pitch sequencer, continuous, steps -7, -3, -12, +7, +4, 0 semitones.

## What follows the manual, and what is a guess

Taken directly from the manual / specs: all ranges above, the 23-position stutter table, hold
behaviour, the 6-step sequencer with skip/mute, the pitch step set, the percentage-of-front-panel
behaviour of the filter and sample-rate sequencers, tempo range, the x128 multiplier, the ladder-style
filter, the <8-bit noise gate, and the line-level headroom difference.

**Assumed** (the manual does not say; all easy to change in `Source/dsp`):

* Order of the blocks: Stutter → Pitch → Sample rate → Bits → Filter → Mute.
* Filter resonance is fixed at a moderate value; knob tapers are exponential for Sample and Filter and
  linear for Bits.
* A sequencer knob position of "off" exists below "once".
* A step lasts one beat divided by the multiplier.
* Stutter snippet length is a note division of the tempo (default 1/8 note); the snippet starts ~8 ms
  before the detected pick so the transient is included.
* The pick detector is a generic fast/slow envelope comparator.
* Pitch shifting is a crossfading two-tap delay-line shifter (grainy, which suits the sound).

**Not implemented** (yet): the expression-pedal morph between two complete sets of settings, the 16
hardware preset slots / MIDI CC / SysEx (use your DAW's presets and automation instead), the
mono-sum of a stereo input when stutter/sequencer are active (this version stays in stereo), and the
true/buffered bypass options.

## Building

Needs Visual Studio 2026 (C++ workload), CMake and Ninja. JUCE 8 is a git submodule in
`third_party/JUCE`, pinned to 8.0.12 (JUCE is AGPLv3 / commercial: fine for personal use, but
distributing a closed-source build needs a JUCE licence).

```bat
git clone --recursive https://github.com/<you>/ottoclone.git
cd ottoclone
:: or, in an existing clone:  git submodule update --init
```

```bat
build.bat                  :: plugin (VST3 + Standalone) and the DSP tests
build.bat ottobit_tests    :: just the DSP tests
build\ottobit_tests.exe    :: run them
```

The VST3 ends up in `build\OttobitJrReplica_artefacts\Release\VST3\`. Copy the whole
`Ottobit Jr Replica.vst3` folder to `C:\Program Files\Common Files\VST3\` (needs admin) and rescan
plugins in your DAW. A Standalone app is built in `build\OttobitJrReplica_artefacts\Release\Standalone\`
for quick testing without a DAW.

### Verification tools

```bat
cmake -S . -B build -DOTTOBIT_BUILD_SMOKETEST=ON
build.bat OttobitSmokeTest OttobitEditorSnapshot
```

* `OttobitSmokeTest <.vst3> <outdir>` loads the built VST3 through JUCE's plugin host, checks the
  parameters, runs audio through several settings, tests bypass / state save-restore / odd block
  sizes, and writes demo WAVs (a plucked string through the factory sound, bit-crush + filter,
  stutter, filter sequence).
* `OttobitEditorSnapshot <outdir>` renders the editor (both layers) to PNG without opening a window.

The DSP tests also pass under MSVC AddressSanitizer (`/fsanitize=address`).

## Layout

```
Source/dsp/Mapping.h      knob → value tables from the manual (shared by engine, GUI and tests)
Source/dsp/Components.h   attack detector, pitch shifter, ladder filter, stutter buffer
Source/dsp/Engine.h       the pedal: signal chain, sequencer, tempo (no JUCE dependency)
Source/Plugin*.{h,cpp}    JUCE processor (parameters, state) and the pedal-shaped editor
Tests/EngineTests.cpp     offline checks of everything above
Tools/                    VST3 smoke-test host and editor-to-PNG renderer
artifacts/                editor screenshots (the demo WAVs are generated by the smoke test, not committed)
```
