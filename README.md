# VoiceStem

A VST3 plugin that splits a mic signal into **Voice / Ambience / Reverb** and controls each with its own knob.
Built with an open-source model (DeepFilterNet3) and our own DSP, inspired by the knob layout of Supertone Clear.
(No code or models from the Clear binary are used.)

## How it works

```
input ─┬─ DeepFilterNet3 ── voice ─┬─ ReverbSplitter ── direct
       │                           │                    reverb = voice − direct
       └─ (latency-aligned) ────── ambience = input − voice
out = Voice·direct + Reverb·reverb + Ambience·ambience
```

- **Voice**: DeepFilterNet3 (48 kHz, 10 ms hop) output. Wrapped as a static library from the Rust `libDF` + tract runtime (`rust/`), with both models embedded, so the plugin is a single file.
- **Ambience**: input − voice. Because it's a subtraction, setting all three knobs to 0 dB reproduces the original input exactly (null test −100 dB).
- **Reverb**: late-reverb estimation from the voice signal (Lebart-style power spectrum + decision-directed Wiener gain). The room T60 is estimated blindly from the decay slopes of the frame energy, so dry voices are barely touched and reverberant voices are suppressed more.
- Other sample rates (44.1 kHz etc.) are converted to and from 48 kHz with a windowed-sinc resampler.

| Mode | Latency | CPU (1 channel, real-time factor) |
|---|---|---|
| Default (DF3) | 50 ms | ~0.05 |
| Low Latency (DF3-LL) | 30 ms | ~0.18 |

The top of the editor shows a scrolling waveform (about 5 s, one pixel per 10 ms). Layers are stacked outward from the centre — Voice (blue), Reverb (purple), Ambience (green) — at their post-knob levels, so turning a knob down shrinks that colour right away. The grey outline is the original input; the gap between it and the coloured area is what was removed.

Knob range is −60 (Off) to +12 dB. Defaults are Voice 0 dB, Ambience Off and Reverb 0 dB. Double-click resets a knob.

## Measured results (`tools/`, DeepFilterNet sample audio)

- Denoising: SI-SDR for noisy speech at 0 dB SNR went from 6.0 to **20.0 dB** (LL: 18.9 dB).
- Reverb separation (RT60 0.79 s): the reverb stem takes 16 dB out of reverberant speech but only 22 dB out of dry speech. Its effect is weaker than a purpose-trained model; a dedicated 3-stem training run would be needed to match Clear.

## Install

Download `VoiceStem-Setup-<version>.exe` from [Releases](https://github.com/soodal2u/voicestem/releases) and run it (administrator approval required).
- The plugin goes into the standard VST3 folder `C:\Program Files\Common Files\VST3\VoiceStem.vst3`, where every DAW and host finds it.
- Optionally installs the standalone app too (Start menu entry).
- Uninstall from Windows "Apps". The VC++ runtime is linked statically, so nothing else needs installing.
- Installing an update while a host has VoiceStem loaded is fine; the new version takes effect when the host reloads it.

VST2 is not provided: Steinberg no longer licenses the VST2 SDK to new developers, so a VST2 build can't legally be distributed. Every current host, including Reaper, Cubase, Ableton and FL Studio, supports VST3.

## Fault protection and diagnostics

- NaN/Inf in the input (from a driver or another effect) is replaced with silence before it reaches the model. If the model itself produces NaN/Inf, it is bypassed and reloaded automatically.
- Output beyond +12 dBFS is clamped (non-finite values become 0).
- **Black box:** when the output is much louder than any recent input (≥ −12 dBFS and ≥ +18 dB above input), or contains NaN/Inf, the last 4 seconds are saved automatically to `%APPDATA%\VoiceStem\incidents\incident-<time>.wav` (left = input, right = output), and the cause is logged in `%APPDATA%\VoiceStem\voicestem.log`. At most one capture every 10 seconds.

## Build (Windows, VS 2022 Build Tools)

Requirements: CMake ≥ 3.22, Rust (stable-msvc, `rustup`), JUCE (`C:/dev/JUCE`, override with `-DVOICESTEM_JUCE_DIR`).

```bash
cmake -S . -B C:/dev/voicestem-build -G "Visual Studio 17 2022" -A x64
cmake --build C:/dev/voicestem-build --config Release
```

At configure time the DF3 models (SHA-256 verified) are downloaded into the build folder, and cargo builds `dfwrap.lib`.
Output: `C:/dev/voicestem-build/VoiceStem_artefacts/Release/VST3/VoiceStem.vst3` (plus Standalone).

### Verification tools

- `VoiceStemCli <in.wav> --check [--ll] [--block N]`: null test, voice/input alignment and real-time factor.
- `VoiceStemCli <in.wav> <out.wav> --voice dB --ambience dB --reverb dB [--ll]`: offline render.
- `VoiceStemHostCheck <VoiceStem.vst3> <in.wav> <out.wav> <png>`: loads the plugin like a DAW and processes audio.
- `VoiceStemSnapshot <png>`: saves an image of the editor.

## Licenses

- DeepFilterNet (libDF and models): MIT / Apache-2.0, © Hendrik Schröter
- JUCE: AGPLv3 / commercial license
