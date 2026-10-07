# Chroma Comp

A single, transparent digital compressor plugin (VST3 / AU / Standalone) built with JUCE 8.

* Threshold, Ratio, Knee, Attack, Release, Makeup, Mix, optional 1.5 ms Lookahead, Bypass
* Log-domain, stereo-linked detector with smooth decoupled peak envelope (no overshoot, no zipper noise)
* **Adaptive analyze**: listens to >= 5 s of your audio (play your track, press the button) and sets Threshold, Ratio, Knee, Attack, Release and Makeup from what it hears, steered by *Feel* (Tight / Subtle / Balance / Glue), *Intensity* (Low / Mid / High) and *Transients* (Preserve / Unpreserved). It measures level distribution, crest factor, transient rise time, tempo and low-end weight.
* Preset system: 8 factory presets + save / load / delete user presets (`.ccpreset` XML files)
* 600 x 392 compact, resizable GUI - white surface, neon-blue accents, 3D knobs
* v-sync-driven waveform display (4 s scrolling min/max envelope of input and/or output) with a gain-reduction meter, a BS.1770 momentary loudness (LUFS) meter and IN / OUT buttons; the display and meters cost 0 CPU while the editor is closed

## Get the plugin without installing anything (GitHub Actions)

1. Create a new GitHub repository and push this folder to it (or drag-and-drop the files on github.com).
2. Open the **Actions** tab - the *Build Chroma Comp* workflow starts automatically (~6-10 min).
3. Open the finished run, scroll to **Artifacts**, download `ChromaComp-Windows`, `-macOS` or `-Linux`.
4. Copy the `.vst3` to
   * Windows: `C:\Program Files\Common Files\VST3`
   * macOS: `~/Library/Audio/Plug-Ins/VST3` (and `.component` to `~/Library/Audio/Plug-Ins/Components`)
   * Linux: `~/.vst3`

Publish a downloadable release: `git tag v1.0.0 && git push --tags` - the workflow attaches zips to a GitHub Release.

macOS note: CI builds are not code-signed. If Gatekeeper blocks the plugin run
`xattr -dr com.apple.quarantine "Chroma Comp.vst3"` or sign/notarize it with your Apple Developer ID.

## Build locally

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```
Needs CMake >= 3.22 and a C++17 compiler. JUCE is downloaded automatically (tag set by `JUCE_GIT_TAG`).

## Licence

JUCE is dual-licensed (AGPLv3 / commercial). If you distribute this plugin you must either publish its
source under a compatible open-source licence or hold a JUCE commercial licence. VST is a trademark of Steinberg.
