# VEIL — Dark Fantasy Audio Effect Plugin (Claude Code brief)

You are a senior C++ audio DSP engineer and JUCE plugin developer with strong UI/UX taste. Build **VEIL**, a real, shippable Audio Unit effect plugin for **Logic Pro and GarageBand on macOS**, working on **both Apple Silicon and Intel Macs**.

VEIL turns ordinary synths, pads, pianos and guitars into dark, cinematic, haunted textures: gothic cathedrals, forgotten kingdoms, ritual magic, dungeon synth, dark ambient. All artwork must be original: no game names, logos or assets in the plugin itself.

---

## 0. HOW TO WORK (read first)

**Repository:** https://github.com/AdrianAbbas/Dark-Fantasy-Plugin

A previous version exists in this repo. It builds, passes CI and passes `auval`. When it is inserted on a GarageBand software-instrument track on an Apple Silicon Mac, three things go wrong:

1. The instrument goes completely silent.
2. GarageBand shows only the generic AU parameter sliders, not the custom VEIL interface.
3. Moving the controls changes nothing audible.

The existing offline tests already show the processor producing sound from a sine input. So the likely cause is **how the plugin is built, signed, registered, installed or loaded by the host**, rather than the DSP math. Treat this as the top priority.

**Rules for this project:**

- **Audit first, then fix.** Read the whole repo before changing anything. Keep what is sound and rewrite what is weak or thin. You may restructure files freely. Keep the AU identity (`aufx` / `Vel1` / `Veil`) unless you find a concrete reason to change it, and document the reason if you do.
- **Work in milestones with hard gates** (Section 2). Do not start a milestone until the previous gate passes. Commit at the end of each milestone with a clear message.
- **Never claim something works that you have not verified.** "auval passed" does not mean "works in GarageBand". "Standalone shows the GUI" does not mean "the AU editor works". Say exactly what was tested, how, and what was not.
- **Never "fix" a bug by hiding it.** Do not disable DSP, remove the editor, silence assertions or loosen tests to make them pass.
- **Prefer a stable, great-sounding simpler algorithm over a broken ambitious one.**
- If you are running on the user's Mac, build, install and validate locally. If you are running somewhere without macOS, use GitHub Actions (`gh run watch`, `gh run view --log-failed`) as the macOS build machine, and say clearly what could not be tested.
- Keep a running `docs/DEVLOG.md` with decisions, bugs found, root causes and fixes.

---

## 1. PLATFORM AND COMPATIBILITY REQUIREMENTS

These are non-negotiable and come before any feature work.

### Build

- JUCE 8.x (pin an exact tag or commit), CMake ≥ 3.22, C++20 (fall back to C++17 only if needed).
- Formats: **AUv2** (primary), VST3 (secondary), Standalone (for development only).
- **Universal binary:** `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"`. Verify both slices with `lipo -archs` in CI and fail the build if either is missing.
- **Deployment target:** macOS 10.15 or lower if JUCE allows, so older Intel MacBooks can load it. Document the minimum.
- Plugin is an **effect** (`kAudioUnitType_Effect`, `IS_SYNTH FALSE`), inserted after an instrument.

### GarageBand / sandboxed-host compatibility

GarageBand is a sandboxed host. It handles Audio Units differently from Logic and from `auval`. Investigate and handle all of the following:

- **Sandbox-safe declaration.** Set `AU_SANDBOX_SAFE TRUE` in `juce_add_plugin` (adds `sandboxSafe` to the AudioComponents entry in Info.plist). Without it, sandboxed hosts may load the plugin out-of-process. Verify the key is present in the built Info.plist.
- **Out-of-process loading.** The plugin must produce audio and show its editor **even when loaded out-of-process** (Logic can also do this, and Apple Silicon hosts do it for some plugins). Nothing may depend on being in the host's process.
- **Channel layouts.** Support mono→mono, stereo→stereo **and mono→stereo**. Check what a GarageBand software-instrument track actually requests. Never leave output channels uncleared or unwritten.
- **Editor.** `hasEditor()` returns true and `createEditor()` always succeeds. Editor construction must be cheap and must not throw, assert in release, block on file I/O, or depend on audio having been processed yet. Check that the AU exposes its Cocoa view (`kAudioUnitProperty_CocoaUI`) correctly in the built bundle.
- **Bypass.** Host bypass (`kAudioUnitProperty_BypassEffect`, via `getBypassParameter()`) and VEIL's own bypass must both pass the dry signal.
- **Factory presets** exposed as AU factory presets (`getNumPrograms` etc.), so GarageBand's and Logic's preset menus list them.
- **State.** Saving and reopening a project restores every parameter.

### Signing, quarantine and installation

An unsigned plugin downloaded as a GitHub Actions artifact gets the `com.apple.quarantine` flag, and macOS may refuse to load its code even though the host still lists it. This is a strong suspect for the GarageBand failure.

- CI must **ad-hoc sign the whole bundle** (`codesign --force --deep --sign - VEIL.component`) and verify with `codesign --verify --strict --verbose=4`.
- Provide **`scripts/install-mac.sh`** that the user runs after downloading. It must:
  1. copy `VEIL.component` to `~/Library/Audio/Plug-Ins/Components/` (and `VEIL.vst3` to `~/Library/Audio/Plug-Ins/VST3/`),
  2. remove quarantine: `xattr -dr com.apple.quarantine <bundle>`,
  3. re-sign ad-hoc and verify,
  4. reset the AU cache: `killall -9 AudioComponentRegistrar`,
  5. run `auval -v aufx Vel1 Veil` and print a clear pass/fail,
  6. print the next steps for GarageBand and Logic (Section 8).
- Provide `scripts/uninstall-mac.sh`.
- Document that proper distribution to other people needs a paid Apple Developer ID, signing with hardened runtime and notarization. Add an optional CI path for this that is skipped when signing secrets are absent.

### Real-time safety

- No heap allocation, locks, file I/O, logging or `std::function` creation on the audio thread.
- All buffers allocated in `prepareToPlay`. Correct re-initialisation on sample-rate or block-size change. Handle `numSamples == 0`, blocks larger than the prepared size, and any sample rate from 44.1 to 192 kHz.
- Denormal protection, NaN/Inf guards per module (a failing module outputs silence for its wet path and resets itself; it never poisons the chain or the dry signal).
- Thread-safe parameter access via `AudioProcessorValueTreeState` and atomics; GUI reads meters through atomics only.
- Report latency accurately with `setLatencySamples`.

---

## 2. MILESTONES AND GATES

### Milestone 0 — Diagnose the GarageBand failure

1. Read the full repo. Write a short audit in `docs/DEVLOG.md`: architecture, what is solid, what is thin or wrong.
2. List every plausible cause of "silent + generic UI in GarageBand", rank them, and test each. At minimum consider: sandbox-safe/out-of-process loading, quarantine and signature, AU registration cache, bus layout negotiation, editor construction failure, architecture mismatch, Info.plist contents, and the GarageBand plug-in window being set to its "Controls" view instead of "Editor".
3. Build the **AU host test harness** (Section 7.2) before fixing anything, so the bug can be reproduced or ruled out by a test rather than guessed at.

**Gate:** the root cause (or the top candidates, if it cannot be reproduced on CI) is documented with evidence.

### Milestone 1 — Transparent, loadable, visible

Ship the smallest possible VEIL that is provably correct in a host:

- All modules default to a state where the plugin is clearly audible but nothing breaks.
- Bypass, wet/dry 0%, all modules disabled: output equals input (within float tolerance, after latency compensation).
- Custom editor appears.
- Universal binary, ad-hoc signed, install script, `auval` pass, harness pass both in-process and out-of-process.

**Gate:** CI green, harness green, and the user confirms on their Mac in GarageBand: sound passes, custom UI appears, bypass works. **Stop and ask the user to test before Milestone 2.** Give them exact, numbered steps.

### Milestone 2 — DSP modules

Implement or upgrade the six modules (Section 3), one at a time, each with its own tests. Fixed signal order:

**Abyss → Ancient Blood → Choir → Wraith → [Haunting ghosts injected] → Echoes → Crypt**

(Tone shaping and saturation first, then pitch and spectral layers, then the ghosts, then time effects, with reverb last so tails are never re-distorted and ghosts sit inside the same space.)

**Gate:** every module test passes, every control measurably changes the output, no module can silence the dry path.

### Milestone 3 — The Haunting (Section 4)

**Gate:** Haunting tests pass (Section 7.1). On a recorded test phrase, ghosts appear, are audibly drawn from earlier material, stay in key with Harmony Lock at 100%, are repeatable with the same seed, and never raise output more than +1.5 dB above the level with Haunting off.

### Milestone 4 — Custom interface (Section 5)

**Gate:** editor opens in the AU harness and standalone, resizes cleanly, stays under ~5% CPU when idle on an M1/Intel i5 class machine, screenshots saved to `docs/`.

### Milestone 5 — Presets, polish, docs, release

**Gate:** all tests green, `auval` and `pluginval` (strictness 10) green, release artifact uploaded, README complete, user confirms the release in GarageBand and Logic.

---

## 3. SOUND DESIGN MODULES

Each module has: an on/off switch (switching crossfades over ~20 ms, no clicks), its own Mix where it makes sense, and smoothed parameters. Every control must have an audible, measurable effect.

**Defaults matter.** With VEIL freshly inserted on a bright stock synth, it should already sound dark and atmospheric, but not mushy, distorted or quieter than the input. Tune defaults by ear-proxy tests (spectral centroid lower, RMS within ±2 dB of input, tail present).

### I — THE ABYSS (dark filtering)

- Cutoff (low-pass), Resonance, High-Pass, Slope 12/24 dB, Darkness, Movement, Movement Rate (free Hz or tempo-synced divisions).
- Use TPT/state-variable filters (e.g. `juce::dsp::StateVariableTPTFilter`, cascaded for 24 dB). Resonance must stay stable at any setting and never self-oscillate into loud whistling.
- **Darkness** is a macro: lowers the effective cutoff, adds a gentle high-shelf cut and a slight low-mid warmth.
- **Movement:** slow LFO on cutoff in octaves, smoothed.

### II — THE CRYPT (cathedral reverb)

- Space, Decay, Pre-Delay, Damping, Low Cut, Shimmer, Width, Mix, Character.
- 8- or 16-line **feedback delay network** with a Hadamard/Householder matrix, gentle delay-line modulation to avoid metallic ringing, input diffusion (all-pass chain), damping filters in the loop.
- **Characters must differ in DSP, not just values:**
  - *Forgotten Chapel:* short lines, bright early reflections, quick build.
  - *Ancient Cathedral:* long lines, slow dense build, dark tail.
  - *Underground Crypt:* strong low-mid resonance, heavy damping, close early reflections.
  - *Endless Void:* very long decay, high modulation, extra diffusion, near-infinite tail capped safely.
- **Shimmer:** octave-up pitch shift inside the feedback path with its own low gain and a limiter so it can never run away.
- Feedback gain is always strictly below 1 after all processing.

### III — THE WRAITH (spectral haunting)

- Spectral Blur, Detune, Drift, Ghost, Freeze, Mix.
- STFT (e.g. 2048 FFT, 4× overlap, Hann analysis and synthesis windows, correct overlap-add normalisation).
- **Blur:** smooth magnitudes across time and neighbouring bins. **Detune/Drift:** small, slowly modulated bin shifts or phase-vocoder pitch offsets. **Ghost:** a delayed, pitch-shifted copy of the spectral layer.
- **Freeze:** captures the current magnitude spectrum and keeps resynthesising it with randomised, evolving phases, producing a sustained processed drone. It must keep sounding after input stops, fade in/out over ~100 ms when toggled, and never be a short looped buffer.
- **Latency policy:** the Wraith is a wet-only layer. Its STFT delay is part of the effect, so the dry path stays at zero latency. Document this. Do not mix a near-identical delayed copy with the dry signal at equal levels (comb filtering).

### IV — ANCIENT BLOOD (saturation and decay)

- Drive, Texture, Age, Dust, Instability, Mix.
- 2× or 4× oversampling around the nonlinearity (`juce::dsp::Oversampling`, allocated in `prepareToPlay`), with latency reported if it adds any.
- **Texture** morphs between soft tape-like saturation and harder asymmetric clipping. Apply output compensation so Drive mostly changes tone, not loudness.
- **Age:** progressive bandwidth reduction, gentle bit/sample-rate softening, slight wow.
- **Dust:** sparse, filtered crackle and hiss. Off at default.
- **Instability:** slow random pitch (wow/flutter) and level drift, smoothed.

### V — THE ECHOES (mystical delay)

- Time (ms or synced: 1/16, 1/8, 1/8 dotted, 1/4, 1/4 dotted, 1/2, 1 bar), Feedback, Damping, Diffusion, Reverse, Stereo Spread, Mix.
- Smooth time changes (crossfade or slew, no zipper noise or pitch jumps on sync changes).
- **Reverse:** buffered reverse delay using two overlapping, windowed read heads that play each captured segment backwards. Document the timing.
- Feedback loop includes damping and a soft limiter; feedback can never grow without bound even at maximum.

### VI — THE CHOIR (phantom harmonics)

- Voice Count (1–4), Interval (octave below, fifth below, fourth above, fifth above, octave above, octave+fifth), Detune, Formant Character, Movement, Blend.
- **Real pitch shifting:** a granular (overlapping Hann grains) or phase-vocoder pitch shifter per voice. Each voice gets its own small detune and slow movement. A plain chorus is not acceptable.
- **Formant Character:** a bank of 3–4 band-pass resonators morphing between vowel shapes (roughly "oo" → "oh" → "ah"), applied to the shifted voices to give a choir-like colour.
- Voices are spread in stereo, lightly filtered and blended under the dry signal.

---

## 4. THE HAUNTING (signature feature)

**VEIL remembers what you play, and haunts you with it.**

Every other effect only processes the sound happening right now. VEIL keeps a memory of the last minute of your playing. While you play, fragments of earlier phrases come back as ghosts: reversed, slowed, pitched down, smeared and far away, retuned to fit what you're playing now. The longer you play, the more haunted the track becomes. No two performances sound the same, yet every ghost comes from your own music, so it always belongs to the song.

This replaces any previous "Ritual Engine" or XY-morph feature in the repo. Remove those parameters, classes and UI completely.

### 4.1 How it works

**Memory (the Reliquary).**
- Continuously record the input (after input gain, before any processing) into a circular stereo buffer, allocated in `prepareToPlay`.
- Store at a fixed internal rate of at most 48 kHz (resample when the host runs higher) so memory stays reasonable: target ≤ ~25 MB per instance at the maximum length. 16-bit storage is acceptable.
- Alongside the audio, store an analysis frame every ~20 ms: RMS level, onset flag, spectral centroid and a 12-bin **chroma** vector (pitch-class energy), computed with a preallocated FFT. Analysis must be real-time safe; amortise it across blocks or move it to a lock-free background worker.
- Silence is never stored as a candidate fragment.

**Fragment selection.**
- When a ghost is due, choose a past fragment (0.5–4 s, starting near an onset) from the memory, excluding the most recent few seconds so ghosts are never just a delay.
- **Harmony Lock:** compare the chroma of the current input with each candidate. Pick the best match and, if needed, transpose the fragment by the semitone rotation that best fits the current harmony (limited to ±7 semitones, preferring octaves, fifths and fourths). With Harmony Lock off, selection is random among non-silent fragments. Ghosts must never sound out of key against sustained input when Harmony Lock is high.
- Bias selection by **Age**: from "recent memories" (last ~10 s) to "ancient memories" (the oldest part of the buffer).

**Ghost voices.**
- Up to 4 simultaneous ghosts. Each one is a granular player reading its fragment with:
  - Direction: forward or reversed (probability set by **Transformation**).
  - Pitch: Harmony Lock transposition plus optional octave down.
  - Time stretch: 1× to 4× slower using overlapping Hann grains, so ghosts can become slow, smeared apparitions.
  - Smear: random grain position jitter for a blurred, spectral quality.
- Each ghost has a long, soft envelope (fade in and out over hundreds of milliseconds, never clicks), its own random stereo position and its own darkening filter set by **Distance**.
- Ghosts are injected after the Wraith and before the Echoes and Crypt, so they sit in the same space as the rest of the sound and fade into the reverb.

**Possession.**
- When ghosts are present, they gently possess the main sound. The live signal is subtly ducked, darkened (Abyss cutoff dips), and the Crypt grows larger and wetter. When the ghosts leave, everything returns.
- This is applied as smoothed offsets on top of the user's settings, never replacing them. Setting Possession to 0 returns exactly to the manual settings.
- Every knob being moved by Possession shows a thin secondary arc in the UI displaying its effective value.

### 4.2 Controls

| Control | Range / options | What it does |
|---|---|---|
| Haunting On | on/off | Enables the whole feature (memory keeps recording when off, so turning it on mid-song works immediately) |
| Presence | 0–100% | Level of the ghosts in the mix |
| Frequency | rare → constant | How often ghosts appear; host-synced option spawns them on bar/beat divisions |
| Memory | 4–60 s | How far back VEIL remembers |
| Age | recent → ancient | Bias towards newer or older memories |
| Harmony Lock | 0–100% | How strictly ghosts are matched and retuned to what you are playing now |
| Transformation | 0–100% | Amount of reversing, slowing, octave-down and smearing |
| Distance | near → far | How dark, quiet and reverberant the ghosts are |
| Possession | 0–100% | How much ghost activity reshapes the main sound |
| Seal | on/off | Stops recording new memories; ghosts keep coming from what is already stored |
| Banish | momentary button | Clears the memory and fades out all ghosts |
| Seed | integer | Random seed for repeatable results |

All continuous controls are automatable and smoothed.

### 4.3 Bound relics

The user can **bind** up to 4 moments from memory as relics (by clicking on the memory ring in the UI, Section 5). Bound relics are copied to their own fixed buffers (up to 4 s each), are always eligible as ghosts, survive Banish, and are **saved with the project** in the plugin state (16-bit, capped size; document the size). A relic can be unbound by clicking it again.

### 4.4 Determinism and bouncing

- All randomness comes from a seeded generator. On transport start, re-seed from Seed plus the transport position, so playing the same section from the same point gives the same ghosts, and an offline bounce matches playback when the memory contents match.
- Document honestly: because ghosts come from what has been played, starting playback mid-song gives different ghosts than playing from the start (bound relics are the exception). Explain this in the README and recommend binding relics for parts that must stay identical.
- With Haunting On but no input yet, the plugin behaves normally; there is no output from an empty memory.

### 4.5 Safety

- Ghost output passes through its own soft limiter and loudness control. At any setting, the ghost layer must never push total output more than +1.5 dB above the level it has with Haunting off (the Milestone 3 gate).
- Memory allocation happens only in `prepareToPlay`; changing Memory length only changes the read window, it never reallocates on the audio thread.
- Sample-rate changes rebuild the memory safely; relics are resampled.

---

## 5. VISUAL DESIGN

The plugin should look like a premium, ancient magical artifact while staying as clear and precise as professional audio software.

**Identity:** obsidian-black background; subtle procedurally generated stone or tarnished-metal texture; deep burgundy and muted bronze accents; antique-silver labels; thin gothic ornamental borders; arcane geometry. Restrained glow, no neon, no cartoon fantasy, no grey default JUCE look.

**Typography:** embed fonts via `BinaryData`, using only SIL Open Font License fonts (e.g. Cinzel for headings, Cormorant Garamond or EB Garamond for labels, a clean readable face for numbers). Include the licence files. Labels must be readable at 100% scale.

**Layout (design at 1200×800; scale proportionally 75%–150%, fixed aspect ratio):**

- **Top bar:** VEIL wordmark centred, subtitle "DARK FANTASY PROCESSOR", preset browser (prev/next, dropdown, save, load), A/B compare, global bypass, settings menu (UI scale, tooltips, about).
- **Centre: the Reliquary**, the visual home of the Haunting and the most distinctive part of the interface.
  - A large circular **memory ring**, drawn like an ancient dial or sundial. It shows the stored memory as a ring of faint, ink-like waveform marks, newest at the top, aging clockwise into the oldest memories; older material fades and discolours like old parchment. Marks are tinted by their pitch content (chroma) using a restrained bronze/silver/burgundy palette.
  - When a ghost is summoned, a pale **wisp** rises from the exact spot on the ring where its fragment came from and drifts slowly inward toward the centre, fading as the ghost fades. Several ghosts mean several wisps.
  - The centre holds a softly glowing **heart** that brightens with input level and darkens as Possession grows.
  - **Bound relics** appear as small sigil glyphs fixed on the ring. Clicking a point on the ring binds a relic there; clicking a relic unbinds it; hovering shows its age and length.
  - Seal shows the ring as locked (a closing ornamental band). Banish plays a short dissolve animation of the ring.
  - The Haunting controls sit in an arc below the Reliquary.
- **Left and right:** the six modules as ornate panels, three per side. Collapsed panels show title, on/off and 2–3 key knobs; clicking expands one panel per side to show all controls.
- **Bottom:** input/output meters, input gain, global wet/dry, output gain, stereo width/correlation indicator, CPU indicator.

**Knobs:** custom-drawn dials inspired by astrolabes and celestial instruments. Smooth drag, fine-adjust with shift, double-click to reset to default, hover highlight, value readout while dragging, tooltips, full host automation linkage, and the Possession secondary arc where relevant.

**Rendering and performance:**

- Software rendering with `juce::Graphics` (avoid the deprecated OpenGL path on macOS). Cache static layers (backgrounds, textures, ornaments) in images and only redraw what animates.
- Animation timer ≤ 30 fps, paused when the editor is not visible. Animation is purely cosmetic and never touches DSP.
- All assets procedurally drawn or original. No copyrighted images.

---

## 6. FACTORY PRESETS

At least 15 presets, each audibly distinct, each setting **every** parameter. Names:

1. Forgotten Kingdom · 2. Cathedral at Midnight · 3. The Hollow Choir · 4. Abyssal Drone · 5. Cursed Forest · 6. Fallen Angel · 7. Ashes of a King · 8. Moonlit Ruins · 9. The Black Citadel · 10. Ghosts of the Chapel · 11. Witch's Lament · 12. Eternal Winter · 13. The Final Ritual · 14. Ancient Spellbook · 15. Throne of Shadows

Include one **"Init — Clean Altar"** preset that is near-transparent, as a starting point.

Each preset also sets the Haunting to suit its character (for example: sparse, distant ancient ghosts for *Forgotten Kingdom*; frequent, harmony-locked choral ghosts for *The Hollow Choir*; heavy Possession for *The Final Ritual*; Haunting off for *Init*). Presets never include bound relics.

- Presets are exposed to the host as AU factory presets and appear in the VEIL browser.
- User presets save to `~/Library/Audio/Presets/Veil Audio/VEIL/` as XML.
- Every preset must pass the loudness test: output RMS on a reference sawtooth chord within roughly ±3 dB of input, no clipping.
- Add a test that measures pairwise spectral/temporal differences between presets and fails if any two are nearly identical.

---

## 7. TESTING

### 7.1 Processor tests (run on macOS and Linux CI)

Through the real `AudioProcessor::processBlock` path, not just module classes:

- Sustained sine and sawtooth chord produce meaningful non-zero output at default settings, at 44.1/48/88.2/96/192 kHz, at block sizes 1, 32, 64, 512, 4096 and varying block sizes.
- Bypass, wet/dry = 0, and all modules off → output matches input (after reported latency).
- Each module alone: measurably changes the output and stays finite.
- Mono→mono, stereo→stereo, mono→stereo layouts.
- Silence in → silence out (except intentional tails and Freeze), no denormals.
- Extreme settings for every parameter, randomised parameter fuzzing for several thousand blocks: no NaN/Inf, peak stays bounded.
- Every factory preset: audible, finite, within the loudness window.
- State round trip: save → new instance → load → identical parameters.
- Haunting:
  - Feed a phrase of distinct notes, then sustain a chord; assert ghosts appear and their content correlates with earlier input (not the current input, not silence).
  - Harmony Lock at 100%: chroma of the ghost layer matches the current input's chroma above a threshold.
  - Same seed + same input + same transport start → bit-identical output (and a different seed gives different output).
  - Haunting Off and Possession 0 restore the manual settings exactly; output with Haunting Off matches a build without it.
  - Seal stops new memories being recorded; Banish clears memory and fades ghosts without clicks; empty memory produces no ghosts.
  - Loudness bound (+1.5 dB) holds across random settings.
  - Bound relics survive a state save/load round trip and a sample-rate change.
  - Memory buffer never reallocates in `processBlock` when Memory length changes.
- No heap allocations in `processBlock` (allocation-counting test).
- Repeated create/prepare/release/destroy cycles, with and without an editor open.

### 7.2 Real Audio Unit host harness (macOS CI, the critical new test)

Write a small command-line program in C++ or Swift using **AudioToolbox** (not JUCE's own hosting) that behaves like a real host:

1. Finds the installed component `aufx/Vel1/Veil` with `AudioComponentFindNext`.
2. Instantiates it **twice: in-process, and out-of-process** (`AudioComponentInstantiate` with `kAudioComponentInstantiation_LoadOutOfProcess`), which mimics how sandboxed hosts like GarageBand can load it.
3. For each instance: sets stereo and mono stream formats, initialises, renders a 440 Hz sine through `AudioUnitRender`, and asserts RMS output > a meaningful threshold.
4. Tests host bypass (`kAudioUnitProperty_BypassEffect`) passes the input through.
5. Sets parameters via `AudioUnitSetParameter` and asserts the output changes.
6. Loads each factory preset via `kAudioUnitProperty_PresentPreset` and asserts audible output.
7. Saves `kAudioUnitProperty_ClassInfo`, creates a fresh instance, restores it, and checks parameters match.
8. Queries `kAudioUnitProperty_CocoaUI`, loads the view factory, creates the `NSView` (in-process instance), and asserts it is non-nil with a sensible size. For the out-of-process instance, use the appropriate request-view API if available, and document what is and is not testable.
9. Runs for both architectures where possible (`arch -arm64` and `arch -x86_64` under Rosetta on Apple Silicon runners).

Run this in CI after the install step. If it fails, the build fails.

### 7.3 Validators

- `auval -v aufx Vel1 Veil` (and `-strict` if available) on arm64 and x86_64.
- `pluginval` at strictness 10 on the VST3 and AU.
- Upload all logs as CI artifacts.

### 7.4 Honest limits

GarageBand and Logic cannot be driven in CI. Write `docs/MANUAL_TEST.md`: a checklist the user runs on their Mac (insert on instrument track, default sound, custom UI visible, bypass, wet/dry 0, each module alone, every preset, the Haunting (ghosts appear while playing, binding relics, Seal, Banish), automation, save and reopen project, Intel Mac if available). Mark the release "host-verified" only after the user reports back.

---

## 8. RELEASE, PACKAGING AND USER INSTRUCTIONS

- CI uploads `VEIL-macOS-Universal.zip` containing `VEIL.component`, `VEIL.vst3`, `install-mac.sh`, `uninstall-mac.sh`, and a short `INSTALL.txt`.
- On tagged commits (`v*`), create a GitHub Release with the same zip.
- **README** must include:
  - What VEIL is, with screenshots.
  - **Install:** download, unzip, run `install-mac.sh` (with the exact Terminal command), or the manual equivalent.
  - **GarageBand:** Settings → Audio/MIDI → make sure *Audio Units* are enabled; restart GarageBand; on a track, open Smart Controls → Plug-ins → Audio Units → Veil Audio → VEIL; if the window shows sliders instead of the VEIL interface, switch the plug-in window's view from *Controls* to *Editor*.
  - **Logic Pro:** Settings → Plug-in Manager → find VEIL → *Reset & Rescan Selection* if it shows as failed; insert from Audio FX → Audio Units → Veil Audio → VEIL.
  - Troubleshooting: plugin missing, plugin failed validation, "cannot be opened because the developer cannot be verified", silent output, generic UI.
  - A guide to every module, every control, and the Haunting, including how to get repeatable bounces with Seed and bound relics.
  - Preset list with one-line descriptions.
  - Known limitations and latency notes.
- `docs/DEVLOG.md` ends with a **Bug Report** section: each bug found in the old version, its root cause, the evidence, and the fix.

---

## 9. DEFINITION OF DONE

VEIL is done when **all** of these are true:

1. A universal (arm64 + x86_64), ad-hoc-signed `VEIL.component` is built by CI and published as an artifact.
2. All processor tests, the AudioToolbox host harness (in-process and out-of-process), `auval` and `pluginval` pass.
3. The user has confirmed in GarageBand on their Mac that sound passes, the custom interface appears and the controls are audible.
4. All six modules, the Haunting with its Reliquary display and bound relics, 15+ presets and the custom interface are implemented and audibly useful.
5. Inserting VEIL on a bright stock synth with default settings already sounds dark and atmospheric without level jumps.
6. The README, install scripts, manual test checklist and bug report are complete and accurate.

Start now with Milestone 0: read the repository, then report your audit and diagnosis before writing new features.
