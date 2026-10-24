# VEIL

VEIL is an Audio Unit effect for Logic Pro and GarageBand on macOS (Apple Silicon and Intel, macOS 10.15 or later). It turns synths, pads, pianos and guitars into dark, cinematic textures: cathedral reverbs, haunted delays, phantom choirs, worn saturation. VST3 is built too; a Standalone app exists for development only.

All artwork is drawn procedurally. There are no third-party images, fonts, samples or game assets.

> **Status: Milestone 1.** This build is about loading and working correctly in hosts: universal binary, sandbox-safe, installer that clears the download quarantine, and automated host tests. The six sound modules are the first-generation versions. The signature feature, *The Haunting*, and the final interface arrive in later milestones (see [docs/PROJECT_BRIEF.md](docs/PROJECT_BRIEF.md) and [docs/DEVLOG.md](docs/DEVLOG.md)).

## Install

1. Download **VEIL-macOS-Universal** from the latest green [Native build](https://github.com/AdrianAbbas/Dark-Fantasy-Plugin/actions/workflows/build.yml) run, or `VEIL-macOS-Universal.zip` from a [release](https://github.com/AdrianAbbas/Dark-Fantasy-Plugin/releases).
2. Double-click the download to unzip it. If you got the Actions artifact, it contains `VEIL-macOS-Universal.zip`; double-click that too. You now have a folder **VEIL-macOS-Universal**.
3. Quit GarageBand and Logic.
4. Open **Terminal** (Applications → Utilities). Type `bash ` (with a space), drag `install-mac.sh` from the folder into the window, press Return. For example:
   ```sh
   bash ~/Downloads/VEIL-macOS-Universal/install-mac.sh
   ```
5. Wait for **"VEIL is installed and validated."**

**Why a script?** This build is signed ad hoc, not with a paid Apple Developer ID. When a browser downloads it, macOS marks every file with `com.apple.quarantine`. Gatekeeper then refuses to load the plug-in code. GarageBand shows a silent slot with plain sliders, and `auval` reports `Cannot open component: -1`. That was the bug in the first release. The script fixes it, in order:
- copies the plug-ins to `~/Library/Audio/Plug-Ins/Components` and `…/VST3`;
- removes the quarantine flag;
- re-signs ad hoc and verifies;
- restarts `AudioComponentRegistrar`;
- runs Apple's `auval`.

### Manual install (equivalent)

```sh
cp -R VEIL.component ~/Library/Audio/Plug-Ins/Components/
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/VEIL.component
codesign --force --deep --sign - ~/Library/Audio/Plug-Ins/Components/VEIL.component
killall -9 AudioComponentRegistrar
auval -v aufx Vel1 Veil        # should end with "AU VALIDATION SUCCEEDED"
```

Uninstall with `bash uninstall-mac.sh` (`--purge` also deletes saved presets).

## GarageBand

1. **GarageBand → Settings → Audio/MIDI**: make sure **Audio Units** is enabled. Restart GarageBand.
2. Select a software-instrument track, open **Smart Controls** (press B), click **Plug-ins**, then click an empty slot. Choose **Audio Units → Veil Audio → VEIL**.
3. If the plug-in window shows sliders instead of the VEIL interface, switch the view menu at the top of the plug-in window from **Controls** to **Editor**.

## Logic Pro

1. **Logic Pro → Settings → Plug-in Manager**. Find **Veil Audio → VEIL**. If it is marked as failed, select it and click **Reset & Rescan Selection**.
2. Insert it from an **Audio FX** slot: **Audio Units → Veil Audio → VEIL**. VEIL is an effect: put it after an instrument, or on an audio track.

## Troubleshooting

| Symptom | What to do |
|---|---|
| VEIL is missing from the plug-in list | Run `install-mac.sh` again and check that it prints `macOS lists aufx Vel1 Veil`. Remove any old copy in `/Library/Audio/Plug-Ins/Components` (the script warns about it). Restart the host. |
| "VEIL cannot be opened because the developer cannot be verified" | The quarantine flag is still there. Run `install-mac.sh`; do not click *Move to Trash*. |
| Plug-in failed validation in Logic | Run `auval -v aufx Vel1 Veil` in Terminal. If it ends with `AU VALIDATION SUCCEEDED`, use **Reset & Rescan Selection** in the Plug-in Manager. |
| Silent output, plain sliders, controls do nothing | macOS refused to load the code (quarantine). Run `install-mac.sh`, then restart the host. |
| Plain sliders but sound works | Switch the plug-in window's view from **Controls** to **Editor**. |
| Something else | Run `bash scripts/diagnose-mac.sh` (read-only) with the host open and VEIL inserted, and attach `~/Desktop/VEIL-diagnostics.txt` to an issue. |

If GarageBand still cannot load VEIL in-process after a clean install, `bash install-mac.sh --out-of-process` removes the sandbox-safe flag from the installed copy. GarageBand then hosts VEIL in a separate process instead.

## Distributing to other people

Ad-hoc signing only works on the Mac that runs the install script. To hand VEIL to others without the script, it needs a paid Apple Developer ID, signing with the hardened runtime, notarisation and stapling. CI has an optional path for this (`scripts/ci-developer-id.sh`). It runs when these repository secrets are set and is skipped otherwise:
- `MACOS_CERTIFICATE`, `MACOS_CERTIFICATE_PASSWORD`, `MACOS_SIGNING_IDENTITY`;
- `NOTARY_APPLE_ID`, `NOTARY_TEAM_ID`, `NOTARY_PASSWORD`.

That path has not been exercised yet.

## Sound modules (first generation)

Signal order today: Abyss → Ancient Blood → Echoes → Crypt → Wraith → Choir → global Wet/Dry → output. Milestone 2 changes it to the order in the brief (Abyss → Blood → Choir → Wraith → Echoes → Crypt).

| Module | What it does now |
|---|---|
| **The Abyss** | Resonant low-pass (12/24 dB), high-pass, Darkness, slow cutoff Movement. |
| **The Crypt** | Eight-line feedback-delay-network reverb: Space, Decay, Pre-Delay, Damping, Low Cut, Shimmer, Width, Mix, four chamber characters. |
| **The Wraith** | Granular blur, detune and drift with a delayed fifth "ghost"; Freeze holds a short processed texture. (An STFT spectral version replaces this in Milestone 2.) |
| **Ancient Blood** | Saturation with Texture, Age (bandwidth and bit softening), Dust, Instability (wow), Mix. |
| **The Echoes** | Delay with free or synced time, Feedback, Damping, Diffusion, Reverse, Stereo Spread, Mix. |
| **The Choir** | Up to four pitch-shifted voices (octave/fifth intervals) with detune, movement and vowel colour. |

Every control is automatable and saved with the project. Host bypass and VEIL's own **Bypass** pass the dry signal. **Wet/Dry 0 %**, or all modules off, passes the input unchanged. Reported latency is 0 samples; the wet layers carry their own intentional delays.

## Building

Requirements: CMake 3.22+, a C++20 compiler, Git. CMake fetches JUCE 8.0.16 (pinned commit) unless you pass `-DJUCE_PATH=/path/to/JUCE`.

```sh
# macOS (universal arm64 + x86_64, macOS 10.15+ by default)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
bash scripts/package-mac.sh build/VEIL_artefacts/Release dist   # signs and zips
bash scripts/build-au-harness.sh build/VeilAUHarness             # AudioToolbox host test
```

On Linux the same commands build the VST3 and Standalone and run the processor tests (install the JUCE Linux dependencies listed in `.github/workflows/build.yml`).

### Tests

- `Tests/VeilTests.cpp`: processor tests through `processBlock`. They cover:
  - parameters, presets and state;
  - mono/stereo/mono→stereo routing, bypass and dry-path transparency;
  - several sample rates, silence, extreme settings;
  - no heap allocation in `processBlock`;
  - module behaviour;
  - editor lifecycle.
- `tools/au-harness/VeilAUHarness.mm`: a small AudioToolbox host, not JUCE. It loads `aufx/Vel1/Veil` out-of-process (AUv2 and AUAudioUnit APIs, as GarageBand and Logic do on Apple Silicon) and in-process. It renders audio and checks host bypass, parameters, every factory preset, state restore and the editor view. CI runs it `--strict` on arm64 and x86_64, after installing a deliberately quarantined download with `install-mac.sh`.
- [docs/MANUAL_TEST.md](docs/MANUAL_TEST.md): the checklist for GarageBand and Logic, which CI cannot drive.

## Licences

JUCE is used under its own licence terms (AGPLv3 or a commercial JUCE licence); choose the appropriate one before distributing VEIL.
