# Ottoclone

<table>
  <tr>
    <td align="center"><img src="artifacts/editor_front.png" width="340" alt="Ottoclone front panel"><br><sub>Front panel</sub></td>
    <td align="center"><img src="artifacts/editor_alt_pitch.png" width="340" alt="Ottoclone Alt layer showing the six sequencer steps"><br><sub>Alt layer: the six sequencer steps</sub></td>
  </tr>
</table>

**Ottoclone** is a free, open-source bitcrusher / ladder-filter / stutter / 6-step-sequencer effect for
guitar and synths: a **VST3** plug-in (plus **Audio Unit** on macOS and a **Standalone** app). It is written
from scratch in C++ with [JUCE](https://juce.com) and is inspired by a hardware pedal (see
[Credits](#credits-and-licence)). The aim is to put those ideas, plus some extras only a plug-in can offer,
in the hands of more people for free.

> **Status: early version (0.1.0), work in progress.** The sound engine is covered by automated tests
> and the plug-in loads and runs in a test host, but it has had little real-world use in DAWs, and the
> macOS build has not been run on a real Mac yet. Bug reports are welcome.
>
> Ottoclone is an independent, unofficial project. It is not affiliated with or endorsed by the makers of
> the hardware that inspired it, and it contains none of their code, firmware or artwork.

[Features](#features) · [Install](#install) · [Using it](#using-it) · [Design notes](#design-notes) · [Building](#building-from-source) · [Credits & licence](#credits-and-licence)

## Features

- **Sample-rate reduction** from 48 kHz down to 48 Hz and **bit-depth reduction** from 24 bit to 1 bit
  (with a noise gate below 8 bit).
- **Ladder-style low-pass filter**, 96 Hz to 24 kHz.
- **Stutter** with 23 positions (3 speeds × 7 repeat counts, off and random) and a hold to freeze it.
- **6-step sequencer** for pitch (±12 semitones), filter cutoff or sample rate, with per-step skip/mute,
  1× to 128× tempo multiplier and tap tempo from 23.4 to 6000 BPM (fast enough for ring-mod tones).
- Stutter and sequencer are triggered by **pick-attack detection**.
- **Plug-in extras:** tempo sync to the host, all 22 parameters automatable, input and output gain,
  adjustable trigger sensitivity and stutter length, and processing that stays in stereo. No reported
  latency, and light on CPU (under 1 % of one core in my measurements).

## Install

Prebuilt installers will be attached to the [Releases](https://github.com/d0ge5/ottoclone/releases) page.
**None have been published yet**; until then, build them yourself (see
[Building from source](#building-from-source)).

| System | Installer | What it installs |
| --- | --- | --- |
| Windows 10/11, 64-bit | `Ottoclone-<version>-Windows-x64-Setup.exe` | VST3 plug-in, optional Standalone app |
| macOS 11+, Apple Silicon or Intel | `Ottoclone-<version>-macOS.pkg` | VST3, Audio Unit, optional Standalone app |

Neither installer is code-signed, so your system will warn you the first time.

**Windows**

1. Run the setup. If Windows shows "Windows protected your PC", choose **More info → Run anyway**.
2. Pick **Install for all users** (asks for an administrator password) or **Install for me only**.
3. Accept the licence, choose the full install or the plug-in only, and finish.
4. Open your DAW and rescan plug-ins. Ottoclone is listed under **Fx / Distortion**.

No extra runtime is needed. "Install for me only" uses `%LOCALAPPDATA%\Programs\Common\VST3`, which some
DAWs do not scan by default; if the plug-in is missing, add that folder to the DAW's plug-in paths.
Uninstall from *Settings → Apps*.

**macOS**

1. Open the `.pkg` (right-click → **Open** if macOS blocks it).
2. On the *Customize* page tick the formats you want (VST3, Audio Unit, Standalone) and install; it asks
   for your password because it installs for all users.
3. Restart your DAW. Logic Pro uses the Audio Unit and may take a minute to validate it the first time.
4. If macOS blocks the plug-in, open **System Settings → Privacy & Security** and choose **Open Anyway**.

To uninstall, delete `/Library/Audio/Plug-Ins/VST3/Ottoclone.vst3`,
`/Library/Audio/Plug-Ins/Components/Ottoclone.component` and `/Applications/Ottoclone.app`.

## Using it

The window is laid out like a guitar pedal: six knobs, a bypass footswitch, a tap footswitch and a
sequencer type switch.

| Knob | What it does | Alt layer |
| --- | --- | --- |
| **Sample** | Sample-rate reduction, 48 Hz – 48 kHz (fully clockwise = off) | Step 1 |
| **Filter** | Ladder low-pass, 96 Hz – 24 kHz (fully clockwise = off) | Step 2 |
| **Bits** | Bit depth, 24 → 1 bit (fully clockwise = off) | Step 3 |
| **Stutter** | Off, then full / double / half speed × 1, 2, 3, 4, 6, 8 or 16 repeats, then random | Step 4 |
| **Sequencer** | How many passes: off, once, twice, 4×, 8×, continuous | Step 5 |
| **Seq Mult** | Sequencer speed as a multiple of the tempo, ×1 … ×128 | Step 6 |

- **ALT** swaps the knobs to the six sequencer steps (they turn orange). A step at minimum is
  **skipped**, at maximum it is **muted**.
- **SEQ TYPE** cycles what the sequencer controls: **Pitch** (each step −12 … +12 semitones, centre =
  unshifted), **Filter** or **Sample Rate** (each step is 0–100 % of the front-panel value). The two small
  LEDs show the type; both off means Sample Rate.
- **TAP** sets the tempo. **Hold it** (or use the HOLD button) to freeze the stutter; you can press it
  before playing so the next note is the one that gets frozen.
- **BYPASS** switches the effect in and out. It is also wired to your host's bypass.
- The **Global** panel adds what a plug-in needs: tempo, input/output gain, **Trigger** sensitivity for the
  attack detector, host tempo **Sync**, instrument/line **Headroom**, and the stutter snippet length.

It starts with a factory sound: the pitch sequencer running (steps −7, −3, −12, +7, +4, 0 semitones).
Turn **Sequencer** to *Off* for a clean signal. If the stutter or sequencer misses notes or triggers on
noise, adjust **Trigger**, and try feeding the plug-in the dry guitar signal first.

## Design notes

Ottoclone's behaviour is defined from the manufacturer's published documentation and reviews of the
hardware, not from any of its code.

**Based on that documentation:** the ranges above, the 23-position stutter table, stutter hold, the 6-step
sequencer with skip/mute, the pitch step set, filter and sample-rate steps as a percentage of the
front-panel setting, the tempo range, the ×128 multiplier, the ladder-style filter, the sub-8-bit noise
gate, and the instrument/line headroom difference.

**Guesses** (the documentation does not say; easy to change in [`Source/dsp`](Source/dsp)):

- The order of the effects: Stutter → Pitch → Sample rate → Bits → Filter → Mute.
- A fixed, moderate filter resonance; exponential knob tapers for Sample and Filter, linear for Bits.
- An "off" position below "once" on the Sequencer knob.
- One step lasts one beat divided by the multiplier.
- The stutter snippet is a note division of the tempo (default 1/8) that starts about 8 ms before the
  detected pick.
- A generic fast/slow envelope pick detector and a grainy two-tap delay-line pitch shifter.

**Not implemented yet:** an expression-pedal morph between two sets of settings, 16 preset slots with
MIDI CC and SysEx (use your DAW's presets and automation instead), and a true/buffered bypass option.
Ottoclone also stays in stereo where the hardware reportedly sums to mono while the stutter or sequencer is
active.

## Building from source

JUCE 8 is a git submodule pinned to 8.0.12, so clone with `--recursive`:

```sh
git clone --recursive https://github.com/d0ge5/ottoclone.git
cd ottoclone
# already cloned without it?  git submodule update --init
```

**Windows** needs the Visual Studio C++ workload, CMake and Ninja. `build.bat` expects Visual Studio at
its default 2026 path; edit the `VCVARS` line at the top if yours is elsewhere.

```bat
build.bat                 :: plug-in (VST3 + Standalone) and the DSP tests
build.bat ottobit_tests   :: just the DSP tests (skips compiling the plug-in)
build\ottobit_tests.exe   :: run them
```

The VST3 is built to `build\Ottoclone_artefacts\Release\VST3\Ottoclone.vst3`.

**macOS** needs the Xcode command line tools and CMake, and builds a universal binary with VST3,
Audio Unit and Standalone:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### Making the installers

- **Windows:** install [Inno Setup 6](https://jrsoftware.org/isinfo.php) (`winget install JRSoftware.InnoSetup`),
  run `build.bat`, then `installer\windows\build_installer.bat`. The result is
  `dist\Ottoclone-<version>-Windows-x64-Setup.exe`.
- **macOS:** needs a Mac. Either run `installer/macos/build_pkg.sh build dist` after building, or run the
  **Build installers** workflow in the repository's *Actions* tab. It builds on a macOS runner, runs the tests,
  validates the Audio Unit with `auval` and uploads `Ottoclone-<version>-macOS.pkg` as a workflow artifact.
- The version number lives in the [`VERSION`](VERSION) file. Signing and notarization hooks exist in
  `build_pkg.sh` (configured through environment variables) but have not been tried yet.

<details>
<summary>Tests and verification tools</summary>

The DSP engine is plain C++ with no JUCE dependency and has its own test suite
(`Tests/EngineTests.cpp`, 130 checks: the documented mappings, bit-depth levels, sample-rate hold, filter
response, attack detection, stutter timing, sequencer behaviour and a randomized robustness test). It also
passes under MSVC AddressSanitizer and runs on macOS in CI.

Two extra tools check the built plug-in itself:

```bat
cmake -S . -B build -DOTTOBIT_BUILD_SMOKETEST=ON
build.bat OttocloneSmokeTest OttocloneEditorSnapshot
```

- `OttocloneSmokeTest <.vst3> <outdir>` loads the VST3 through JUCE's plug-in host, checks the parameters,
  runs audio through several settings, tests bypass, state save/restore and odd block sizes, and writes demo
  WAV files.
- `OttocloneEditorSnapshot <outdir>` renders the editor, both layers, to PNG without opening a window (this
  is how the screenshots above were made).

</details>

<details>
<summary>Project layout</summary>

```
Source/dsp/Mapping.h      knob → value tables (shared by engine, GUI and tests)
Source/dsp/Components.h   attack detector, pitch shifter, ladder filter, stutter buffer
Source/dsp/Engine.h       the effect: signal chain, sequencer, tempo (no JUCE dependency)
Source/Plugin*.{h,cpp}    JUCE processor (parameters, state) and the pedal-style editor
Tests/EngineTests.cpp     offline checks of the engine
Tools/                    VST3 smoke-test host and editor-to-PNG renderer
installer/windows         Inno Setup script and build wrapper
installer/macos           .pkg build script, installer text, signing entitlements
.github/workflows         macOS installer build
artifacts/                editor screenshots
```

</details>

## Credits and licence

Ottoclone is inspired by the **Meris Ottobit Jr.** pedal. It is an independent project and is **not
affiliated with, endorsed by or sponsored by Meris**. "Meris" and "Ottobit" are trademarks of their owner and
are mentioned here only to credit that inspiration.

Ottoclone is free software under the [GNU Affero General Public License v3.0](LICENSE). It uses
[JUCE 8](https://juce.com) under JUCE's AGPLv3 option and the MIT-licensed VST 3 SDK; see
[`NOTICE.md`](NOTICE.md). If you distribute a build, the AGPL requires you to make the corresponding source
available to the people you give it to. VST is a trademark of Steinberg Media Technologies GmbH.
