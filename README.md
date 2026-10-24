# VEIL

A native JUCE/C++20 stereo or mono **audio effect** for dark ambient, dungeon synth and cinematic sound design. Six working DSP modules surround an automatable triangular Ritual Engine. All artwork is procedural vector drawing; there are no game assets or external sound files.

## Build

CMake pins JUCE **8.0.6** to commit `51a8a6d7aeae7326956d747737ccf1575e61e209`. Supply `-DJUCE_PATH=/path/to/JUCE` for an existing copy of that version; otherwise CMake fetches it. An internet connection is needed for the initial dependency fetch. CMake 3.22+, a C++20 compiler and Git are required.

### macOS / Logic Pro

Install Xcode and its command-line tools, CMake and Ninja. On Apple Silicon, from this checkout:

```sh
cmake -S . -B build-mac -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
cmake --build build-mac --parallel 4
ctest --test-dir build-mac --output-on-failure
```

For an Intel Mac use `x86_64`; for a universal binary use `"arm64;x86_64"`. Alternatively use `-G Xcode`, then `cmake --build build-mac --config Release --parallel 4` and `ctest --test-dir build-mac -C Release --output-on-failure`.

Outputs are under `build-mac/VEIL_artefacts/Release/`:

- `AU/VEIL.component` — AUv2 effect for Logic Pro
- `VST3/VEIL.vst3`
- `Standalone/VEIL.app`

Install the AU into your user plugin folder:

```sh
mkdir -p "$HOME/Library/Audio/Plug-Ins/Components"
cp -R build-mac/VEIL_artefacts/Release/AU/VEIL.component "$HOME/Library/Audio/Plug-Ins/Components/"
auval -v aufx Vel1 Veil
```

Quit Logic before installing. Launch Logic, open **Logic Pro > Settings > Plug-in Manager**, find **Veil Audio / VEIL**, and use **Reset & Rescan Selection** if needed. Insert VEIL in an **Audio FX** slot after the instrument on a software instrument channel strip, or on an audio track. It is not an instrument and requires incoming audio. Verify automation, mono/stereo instances, bypass and session reopening in Logic. Developer-local builds are not notarized distribution releases; commercial delivery needs your own Apple signing identity, hardened-runtime packaging where applicable, notarization and installer testing.

**This project was developed and built on Linux. No macOS AU artifact, Apple Silicon execution, auval result, code signing or Logic Pro validation is claimed.** The CMake AU target and commands above are provided for that required Mac verification.

### Linux development

On a normal Debian/Ubuntu development machine install CMake, Ninja, a C++20 compiler, pkg-config, ALSA, FreeType, Fontconfig, X11 (including Xcomposite, Xcursor, Xext, Xinerama, Xrandr, Xrender) and OpenGL development packages. WebKit, CURL and browser support are disabled.

This cloud machine uses a tested rootless Debian 13 amd64 bootstrap:

```sh
bash scripts/bootstrap-linux.sh
source /workspace/toolchain/activate.sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DJUCE_PATH=/workspace/deps/JUCE
cmake --build build --parallel 3
ctest --test-dir build --output-on-failure
```

Use one build process per build directory; `--parallel 3` supplies compiler parallelism.

If `/workspace/deps/JUCE` is absent, omit `-DJUCE_PATH` to fetch the pinned dependency. The bootstrap uses signed Debian package metadata and extracts verified packages under `/workspace/toolchain`, without system installation. Set `VEIL_TOOLCHAIN` before running it to select another writable directory. It is specific to Debian 13 amd64. Linux outputs include `build/VEIL_artefacts/Release/VST3/VEIL.vst3` and `build/VEIL_artefacts/Release/Standalone/VEIL`. The cloud build uses GCC 14/glibc 2.41 and needs a compatible Linux host; it is not a broadly portable older-distribution package.

Run the standalone in a desktop session, select an audio device in **Options**, and enable an input. Hardware devices are not available in every cloud environment. In a DAW, insert the VST3 after an instrument or on an audio track. The standalone does not generate synth audio itself.

## First sound

Start with **Forgotten Kingdom**, feed a sustained synth or piano, and set global Wet / Dry around 60–70%. Drag the orb towards **SHADOW** for dark warmth, **SPIRIT** for harmonics and spectral space, or **RUIN** for distressed texture. Turn **Ritual Motion** on for slow evolution. Select any of the six module panels to expose its full controls. Disable individual modules to isolate their character. Disable Ritual to hear unmodified manual parameter values.

Custom antique rotary dials support dragging, editable numerical values and double-click reset. Every sound control is an automatable JUCE parameter. The `◈` marker identifies controls influenced by Ritual; the stored dial position remains the manual base value. The moving orb displays the effective, smoothed position, including motion. Module enable, global bypass, gains and zero module wet mixes remain under manual control.

Use **Save** / **Load** to store complete `.veilpreset` parameter presets. A/B recall compares two captured parameter snapshots; store each slot before recalling it. Comparison slots are temporary and are not stored in host/preset files. Host sessions store all parameters through versioned JUCE state data. Factory programs initialize every parameter; the 15 factory names and individual settings live in `Source/PresetManager.cpp`.

## Processing and controls

The signal order is **Abyss → Ancient Blood → Echoes → Crypt → Wraith → Choir → global blend/output**. Each module's enable control fades its contribution; the effect continues updating its internal history. Global bypass fades to the untouched input, leaving histories running for predictable re-entry. The global dry signal receives input/output gain; full bypass skips those gains. Host tempo defaults to 120 BPM when unavailable.

| Module | Controls and behavior |
| --- | --- |
| **The Abyss** | Bounded resonant low-pass (12/24 dB, crossfaded slope), high-pass, Darkness tilt/cutoff coloration, sinusoidal Movement and rate. Sync sets the movement cycle to four host beats. |
| **The Crypt** | Eight-line Householder feedback delay network with Space, T60 Decay, smoothed Pre-Delay, Damping, input Low Cut, actual feed-forward octave Shimmer, Width and Mix. Four acoustic characters change delay geometry and damping, independently of presets. |
| **The Wraith** | Two-head windowed granular resampling with filtered Spectral Blur, pitch Detune, slow Drift, a delayed fifth-shifted Ghost layer, and Mix. Freeze retains a **1.25-second processed texture** while overlapping playback heads continue traversing it. Feed audio before freezing. |
| **Ancient Blood** | Drive and nonlinear Texture, 2× interpolated saturation/reconstruction filtering, Age bit/hold degradation and darkening, gated Dust, wow/level Instability, Mix. Dust is intentionally absent on digital silence. |
| **The Echoes** | Free 20–2000 ms time or eighth/quarter/dotted-quarter/half-note sync, bounded Feedback, Damping, allpass Diffusion, buffered overlapping Reverse grains, Stereo Spread and Mix. Time changes bend pitch smoothly. Reverse Time is grain duration; history spans up to twice that duration. |
| **The Choir** | One to four additional, genuinely pitch-shifted voices: octave below, fifth below, fifth above, octave above or unison. Detune and Movement animate them; Formant moves broad vocal-band resonators. Blend sets their contribution. This is a creative phantom ensemble, not realistic vocal synthesis. |

### Ritual Engine

The triangle's vertices are Shadow `(0,1)`, Spirit `(0.5,0)`, Ruin `(1,1)` in normalized XY coordinates. Points outside the triangle are projected back into it; the normalized barycentric weights sum to one. The engine modifies 23 continuous controls across the modules with bounded offsets/scales, then clamps to their physical ranges. Manual settings remain stored, and a manually zeroed wet mix stays zero. Input/output gain, bypass, enables, freeze and timing choices are not changed by the archetypes.

Motion supports Circle, Pendulum and smoothly interpolated deterministic Wander. Depth sets excursion; Rate is in Hz, or sync uses a 16-beat cycle at host BPM. Motion follows tempo but is not transport-position-locked. Manual XY and motion are smoothed; the animation is message-thread-only and never drives DSP.

## Engineering limits

- The instantaneous dry path has **zero reported host latency**. Granular Wraith/Choir/shimmer and reverse delay introduce intentional audible wet-path delay (roughly tens to hundreds of milliseconds, or up to twice the selected reverse delay duration); these are sound-design layers, not phase-transparent pitch correction. Global dry/wet mixing can produce combing as part of the effect.
- Granular windows and interpolation trade CPU cost against pitch sidebands/transient smearing. The Wraith is an equivalent granular texture effect, not an FFT spectral-bin freeze. Frozen capture audio is not serialized; parameter state is. A frozen empty buffer produces silence until unfrozen and fed audio.
- Blood's 2× interpolation/reconstruction reduces aliasing but is not a mastering-grade high-order oversampling filter. Strong settings intentionally degrade fidelity.
- All audio history storage is allocated during prepare; processing is allocation-free in regression coverage, with no explicit locks. Histories are reset on re-prepare/sample-rate change. Supported test rates are 44.1, 48, 88.2 and 96 kHz.
- Feedback gains are bounded, denormals suppressed, and nonfinite inputs sanitized. A transparent-below-0.98 soft safety knee approaches a ±2 output ceiling for processed audio. This is not a brickwall true-peak limiter; reduce output gain for intersample/headroom requirements. Full bypass preserves finite input levels.
- CPU readout estimates this instance's processing cost versus block duration; it is not the host's total CPU load. Meters show peak amplitude. There is no audio hardware monitoring claim for the cloud session.
- A first version still needs musician listening tests, target-Mac host validation and performance profiling before production distribution.

## Validation and source layout

`VeilTests` exercises parameters, all factory presets, host state and user preset round trips, mono/stereo routing, bypass, multiple rates, silent/tone/impulse inputs, extreme feedback settings, nonfinite safety, allocation behavior, module sound changes, real harmonizer pitch, frozen-tail retention and ritual boundaries/motion. Test results and the cloud build details are recorded in [docs/VALIDATION.md](docs/VALIDATION.md). Optional UI rendering uses `VeilTests --render-ui path.png` under a desktop or Xvfb; it does not replace listening or DAW testing.

- `Source/PluginProcessor.*` — JUCE host integration, automation, routing, state and meters
- `Source/Parameters.h` — stable parameter IDs, ranges and defaults
- `Source/DSP/BasicModules.*`, `SpatialModules.*` — six DSP engines
- `Source/RitualEngine.*` — barycentric morph and motion
- `Source/PresetManager.*` — complete factory bank
- `Source/PluginEditor.*`, `Source/UI/` — original native vector interface
- `Tests/` — offline regression executable
- `scripts/bootstrap-linux.sh` — repeatable rootless cloud setup
- `docs/PROJECT_BRIEF.md` — supplied design requirements

JUCE is a third-party dependency with its own AGPL/commercial licensing terms. Choose an appropriate JUCE license before distributing VEIL; this repository does not grant a commercial JUCE license. No third-party image, font or audio assets are bundled.
