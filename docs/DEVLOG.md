# VEIL development log

Decisions, bugs found, root causes and fixes, newest milestone last. The brief
is in [PROJECT_BRIEF.md](PROJECT_BRIEF.md).

---

## Milestone 0: audit and diagnosis (2026-10-24)

### Working conditions

What this session could and could not do. Read this before trusting anything below.

| | |
|---|---|
| Machine | Linux cloud container (Ubuntu 24.04, x86_64). No macOS, no Xcode, no GarageBand or Logic. |
| GitHub | The repository is public, so it can be cloned and its Actions history read. **Pushes from this session are refused** (the Claude GitHub App is not installed on the repository), so CI cannot be triggered from here. |
| CI logs | Job logs redirect to an Azure blob host that this environment's network policy blocks. Evidence was taken instead from the diagnostics the previous session committed on branch `ci/au-diagnostics`: `bundle-info.txt`, signature logs, the full `auval` log, and two zipped AU bundles exactly as CI published them. |
| Verified here | Linux build of unmodified `main` (05cc9e5) with the pinned JUCE 8.0.6: **474 assertions passed, 0 failed**. Mach-O and code-signature inspection of the published AU bundles (`llvm-objdump`, a small signature-blob reader). Reading the JUCE 8.0.6 AU wrapper source. |
| Not verified | Anything that needs macOS at runtime. The new host harness (`tools/au-harness`) has been syntax-checked against stub declarations of the Apple APIs only. It has **never been compiled against the real SDK or run**. |

### Audit of the existing code

**Architecture as found.** CMake fetches JUCE 8.0.6 by commit and builds AUv2 (Apple only), VST3 and Standalone. Sixty-one parameters live in one table (`Parameters.h`) behind an `AudioProcessorValueTreeState`. `processBlock` runs a per-sample loop: it smooths all 61 parameters every sample (including the discrete ones), runs the Ritual Engine to produce a modified copy of all 61 values, then runs six modules on a pair of floats. The order is Abyss → Blood → Echoes → Crypt → Wraith → Choir, followed by global mix, a soft clip and the bypass crossfade.

**Solid, keep:**
- Parameter IDs are stable, versioned (`ParameterID{id, 1}`), and there is one source of truth for ranges and defaults.
- State save/restore rejects malformed data, clamps values and fills missing parameters with defaults.
- All delay memory is allocated in `prepare`. The processing code has no locks, file I/O or logging. The existing allocation probe covers C++ `new`.
- NaN/Inf sanitising on input and output; feedback paths are bounded.
- The TPT state-variable filter in the Abyss is correct and stable.
- A real regression suite through `processBlock`: rates, block sizes, bypass, presets, state, extreme settings.

**Thin or wrong for the brief:**
- **Hosting/packaging.** Not sandbox-safe; arm64 only; deployment target 11.0; no `lipo` check for two slices. CI signs the bundle only inside the validation script. CI uploads installable artifacts even when validation fails. No install script, no quarantine removal, no AU cache reset. No mono→stereo layout. No test ever hosts the AU the way a DAW does, or opens the AU editor on macOS.
- **Signal order** differs from the brief (Echoes and Crypt run *before* Wraith and Choir, so pitch and spectral layers are not reverberated).
- **Wraith** is not spectral. It is a 1.25 s time-domain granular capture, and Freeze loops that buffer, which the brief explicitly forbids. Needs an STFT rewrite.
- **Crypt.** The 8-line Householder FDN is fine. It lacks input diffusion and delay modulation. "Characters" differ only by a delay scale and a damping factor. Shimmer is feed-forward, not in the feedback loop.
- **Ancient Blood.** The "2× oversampling" is linear interpolation plus a one-pole filter, not a real oversampler, and reports no latency.
- **Choir.** Real granular pitch shifting, which is a reasonable base. It has 2 static resonators instead of a vowel-morphing bank, and is missing the "fourth above" and "octave + fifth" intervals.
- **Echoes.** Sync divisions are incomplete (no 1/16, dotted 1/8, 1 bar).
- **Ritual Engine** must be removed (replaced by the Haunting).
- **Defaults.** Output gain defaults to −3 dB, so wet/dry 0 is not transparent and inserting VEIL drops the level.
- **CPU.** Per-sample smoothing of 61 parameters and per-sample `pow`/`exp`/`tan` calls measure about 8% of one core at 96 kHz on this container. That is wasteful, though not a fault.
- **Editor.** Hard-coded pixel layout. The whole 1280×860 window repaints at 30 fps, including 1800 random 1-px rectangles regenerated every frame, with no cached background. No fixed aspect ratio, no embedded fonts, no Reliquary.
- **Presets.** No "Init — Clean Altar". User presets go to `~/Documents` as `.veilpreset`. `getCurrentProgram()` reports preset 0 even when the state is custom.

### Diagnosis: silent, generic sliders, controls inert in GarageBand (Apple Silicon)

**Reading the symptoms.** Symptom 3 follows from symptom 1: changes cannot be heard in silence. The instrument's dry sound is also gone, so the effect slot outputs nothing at all. Symptoms 1 and 2 together mean GarageBand has a parameter list for VEIL but neither a working render nor a working view. That fits two situations:
- the code that renders and draws is not running where GarageBand expects it (failed load, crashed hosting process, or rejected configuration);
- the bundle GarageBand loads is not the one CI validated.

It does not fit a DSP bug: auval's render tests pass, the offline tests pass, and a DSP bug would not remove the editor.

**Evidence gathered.**

| # | Evidence | Source |
|---|---|---|
| E1 | Built `Info.plist` has **no `sandboxSafe` key**. It carries JUCE's default `resourceUsage` (`network.client`, `temporary-exception.files.all.read-write`). In JUCE, `sandboxSafe` and `resourceUsage` are mutually exclusive (`juce_PlistOptions.cpp`). | `ci/au-diagnostics:diagnostics/reused-au-validation/bundle-info.txt`; Info.plist in the published zip |
| E2 | Binary is **arm64 only**, `minos 11.0`, SDK 14.5. | `lipo`, `llvm-objdump --private-headers` on the published binary |
| E3 | The green-run artifact (`main` 05cc9e5, run 40060059145) is ad-hoc signed with sealed Info.plist and CodeResources; the hashes match the files. The **earlier artifact (run 4be77de) has only a linker signature**: no `_CodeSignature`, no sealed resources, identifier `VEIL`. CI uploaded both, and the README told users artifacts are kept even after failure. The very first run failed `auval` with "Cannot get Component's Name strings / didn't find the component" on exactly such an unsealed bundle. | signature blobs of both zips; `docs/VALIDATION.md` |
| E4 | `auval` passes on the green artifact, **in-process, arm64**. It lists the Cocoa view class `JUCE_AUCocoaViewClass_…` but never instantiates the view. Channel capabilities are `[1,1] [2,2]` only (no mono→stereo). | `diagnostics/reused-au-validation/auval.txt` |
| E5 | On Apple Silicon, GarageBand and Logic host AUv2 plug-ins **out-of-process** in `AUHostingServiceXPC_arrow`, so `auval`'s in-process pass does not exercise GarageBand's path. *(Single developer report, not Apple documentation.)* | [orizonaudio/Sineless#172](https://github.com/orizonaudio/Sineless/issues/172) |
| E6 | JUCE 8.0.6's AUv2 view factory gets the C++ processor through a private property that returns raw in-process pointers (`juceFilterObjectPropertyID`). The view can only be built in the process that runs the plug-in. JUCE later reworked this path ("AU Client: Fix editor ownership bug" abd02516, "Simplify juceFilterObjectProperty usage" ba29da3e) and channel-info reporting (35d3fab9). All three are in 8.0.15/8.0.16. | JUCE source and history |
| E7 | Processor offline: sustained tones produce output at every tested rate and block size; bypass and dry paths are transparent. | Linux build of 05cc9e5 here, 474/474 |
| E8 | No install path ever removed `com.apple.quarantine`, reset `AudioComponentRegistrar` or checked for duplicate copies on the user's Mac. The README only described Logic. | repo |

**Ranked candidates.** Rank reflects how much of the symptom set a cause explains, weighted by evidence. None is confirmed yet.

1. **The out-of-process instance fails at runtime.** VEIL is not sandbox-safe (E1), so GarageBand hosts it out-of-process (E5), a path no test has ever exercised (E4). If the hosting service fails to build the view or crashes on first render or view creation, GarageBand keeps the parameter list it already fetched, falls back to generic sliders, and the slot goes silent. That explains all three symptoms.
   - *Test:* harness suites `v2 out-of-process` and `AUAudioUnit out-of-process` render, bypass, change parameters and request the remote view; CI collects `AUHostingService` crash reports.
   - *Fix if confirmed:* depends on the failure. Also set `AU_SANDBOX_SAFE TRUE` so GarageBand can load VEIL in-process, while still making out-of-process work (the brief requires both).
2. **The installed bundle is not loadable as installed:** an unsealed linker-signed artifact (E3), a quarantined download, a stale registration, or duplicate copies (E8). Explains silence directly. Explains the generic view only if GarageBand keeps cached parameter info for a component it cannot run, which I could not confirm.
   - *Test:* CI experiments B (quarantine flag) and C (linker-signed bundle); on the user's Mac, `diagnose-mac.sh` reports copies, xattrs, signatures, registry entries, and which process has VEIL mapped.
   - *Fix:* CI always seals the signature before packaging and never publishes failed builds; `install-mac.sh` strips quarantine, re-signs, removes duplicates, resets the registrar and runs `auval`.
3. **Bus-layout negotiation.** If GarageBand asks for a configuration VEIL rejects, initialisation fails and the slot is silent. VEIL offers only `[1,1] [2,2]` (E4). A stereo instrument track should get 2→2, so this is less likely, but a mono-output instrument would need 1→1 or 1→2.
   - *Test:* harness mono→mono and mono→stereo; question to the user.
   - *Fix:* support 1→2 (required by the brief anyway).
4. **Architecture.** arm64-only (E2). Native GarageBand on Apple Silicon is fine. GarageBand set to "Open using Rosetta" can only reach VEIL through the arm64 bridging service (back to candidate 1). An Intel Mac cannot load it at all.
   - *Test:* harness under `arch -x86_64`; question to the user.
   - *Fix:* universal binary with a CI `lipo` gate.
5. **Editor construction failure in the AU wrapper.** The editor constructor looks safe on review and pluginval opened the VST3 editor on Linux. The AU editor path on macOS has never been executed (E4, E6).
   - *Test:* harness in-process Cocoa view test and remote view request.
6. **GarageBand's plug-in window set to *Controls* instead of *Editor*.** Explains symptom 2 only.
   - *Test:* question to the user.
7. **DSP outputs silence for GarageBand's call pattern.** Unlikely (E4, E7) and would not explain the view.
   - *Test:* harness renders at several rates and block sizes, comparing output to input sample by sample.

Info.plist otherwise checked out: type/subtype/manufacturer `aufx`/`Vel1`/`Veil`, `factoryFunction VEILAUFactory` (exported, confirmed with `nm`), version 0x10000, name "Veil Audio: VEIL". **Decision:** keep the AU identity; there is no reason to change it.

### Experiments prepared (not yet run)

- `tools/au-harness/VeilAUHarness.mm` and `scripts/build-au-harness.sh`: an AudioToolbox host built as a universal binary. Three suites:
  - `v2 out-of-process`;
  - `AUAudioUnit out-of-process` (the API modern hosts use);
  - `v2 in-process`.

  Each suite renders a 440 Hz sine at 48/44.1/96 kHz with block sizes 64/512/4096 and runs these checks:
  - the plug-in actually pulls its input;
  - both output channels are written;
  - host bypass and Wet/Dry 0 pass the input through (sample-aligned residual < −40 dB);
  - `AudioUnitSetParameter`/`AUParameter` changes are audible;
  - every factory preset is audible;
  - ClassInfo/fullState survives a round trip into a fresh instance;
  - the editor is created through the Cocoa view factory (in-process) or a remote view request (out-of-process) and survives 1.5–2 s in a window.

  The harness also reports whether the plug-in image was mapped into its own process and which `AUHostingService` processes exist.
- `.github/workflows/au-harness-baseline.yml`: runs the harness on the **exact bundles CI published before**. Experiment A is the green artifact as-is, B the same with a browser-style quarantine flag, C the linker-signed artifact, and D the green artifact with `sandboxSafe` patched into the plist. A runs under arm64 and x86_64 (Rosetta). The job also collects `AUHostingService` crash reports and logs. It reports and does not enforce.
- `build.yml` builds and runs the harness after `auval`, report-only. It becomes `--strict` (failing the build) in Milestone 1.
- `scripts/diagnose-mac.sh`: read-only report from the user's Mac. It covers installed copies, quarantine, signatures, registry entries, which process has VEIL loaded, crash reports and system log lines, plus five questions only the user can answer.

**Gate status (first pass).** The top candidates were documented with evidence. The bug had not yet been reproduced or ruled out by a test, because that needed either push access (to run the harness on macOS CI) or the user's diagnostics report.

### Root cause: confirmed from the user's Mac

The user ran `scripts/diagnose-mac.sh` on their Mac (Apple M3, macOS 27.0.1, GarageBand 10.4.14, Gatekeeper assessments enabled, no Rosetta). The installed `VEIL.component` is **byte-identical to the green-run artifact** (binary sha256 `772d7d09…c179b7`, the build CI validated with `auval`). So the build itself was fine. macOS refused to run the downloaded copy.

The chain, from the report's system log:

1. The installed bundle and every file in it carry `com.apple.quarantine: 0081;…;Chrome;…` from the browser download. `spctl --assess` says `rejected`.
2. GarageBand asks for VEIL; CoreAudio hosts it **out-of-process** in `AUHostingServiceXPC_arrow`.
3. AMFI: `VEIL is adhoc signed` → amfid `-423 The file is adhoc signed or signed by an unknown certificate chain`.
4. syspolicyd: `GK evaluateScanResult: 1 … Prompt shown (6, 0)`, then `Adding Gatekeeper denial breadcrumb (direct)`. The "cannot be verified" prompt appeared and was not approved.
5. Kernel: `ASP: Library load (…AUHostingServiceXPC_arrow -> …/VEIL.component/Contents/MacOS/VEIL) rejected: library load disallowed by system policy`. dlopen fails: `code signature … not valid for use in process: library load disallowed by system policy`.
6. GarageBand: `APComponent.mm:354 error -1 returned from OpenAsync for aufx/Vel1/Veil`. The insert has no running instance, which explains the silence, the missing editor and inert controls. The repeated attempts at 00:08 and 01:14 fail identically.
7. On the same Mac, `auval -a` now prints `Cannot open component: -1` and `auval -v` fails with `OpenAComponent: result: -1` (`auvaltool` load rejected by the same policy). CI's `auval` passed only because the CI runner's copy was never quarantined.

**Root cause:** a quarantined, ad-hoc-signed (not notarised) bundle is rejected by Gatekeeper/AppleSystemPolicy at load time. Nothing in the old release removed quarantine, and nothing tested a downloaded copy. Candidate 2 above is confirmed; candidate 1's out-of-process path is confirmed as GarageBand's actual route. Whether VEIL works correctly out-of-process *once loadable* is still to be shown by the harness.

**Fix.**
- `install-mac.sh` removes quarantine, re-signs ad-hoc, verifies, resets the AU registrar, then runs `auval`.
- CI reproduces the user's state: it quarantines the packaged bundle, installs it with that script, then runs `auval` and the strict harness.
- Proper distribution to other people needs Developer ID signing and notarisation (optional CI path).

**Unrelated findings in the same report:**
- *GarageBand crashed at launch four times* (00:05:56–00:06:20): dyld `Symbol not found: _BNNSGraphContextGetArgPosition` in GarageBand's own `MAMachineLearning.framework` against the system Accelerate framework. This is a GarageBand/macOS version mismatch, not VEIL; GarageBand later launched normally.
- *The Standalone `VEIL.app` from the artifact was blocked the same way*: AMFI `no CMS blob`, `Security policy would not allow process`. It had only a linker signature, because CI re-signed the AU alone.
- *`lipo` failed in the diagnostics script* because it is an Xcode shim that requires accepting the Xcode licence. The script now uses `file`.

### Plan for Milestone 1 (after the gate)

- Upgrade JUCE 8.0.6 → 8.0.16, the latest 8.x, which includes the AU-client fixes in E6. Keep it only if the harness passes on it.
- `AU_SANDBOX_SAFE TRUE`. Universal `arm64;x86_64`, deployment target 10.15, CI `lipo` gate.
- Sign with `codesign --force --deep --sign -` before packaging, verify, and publish installable artifacts only when the build, tests, `auval` and the strict harness all pass.
- `install-mac.sh` / `uninstall-mac.sh` as specified in the brief.
- Mono→stereo layout.
- Transparent defaults for bypass and wet/dry 0 (output gain 0 dB).
- Sandbox-safe preset storage, with no file I/O in editor construction.
- Whatever the harness shows is broken out-of-process.

---

## Milestone 1: transparent, loadable, visible (2026-10-24)

### Changes

- **Installation (fixes B4/B7).** `scripts/install-mac.sh` does the following, in order:
  - copies the AU and VST3 to the user folders and warns about system-wide duplicates;
  - removes `com.apple.quarantine` and checks that none remains;
  - re-signs ad hoc (keeping a Developer ID signature if there is one) and verifies;
  - runs `killall -9 AudioComponentRegistrar` and waits for `aufx Vel1 Veil` to be listed;
  - runs `auval -v` (and, under Rosetta, x86_64), prints PASS/FAIL, then the GarageBand and Logic steps.

  `--out-of-process` strips `sandboxSafe` from the installed copy as a fallback. `uninstall-mac.sh` and `packaging/INSTALL.txt` are added. `validate-au-macos.sh` is removed (superseded).
- **Build.**
  - JUCE 8.0.6 → **8.0.16** (`ea774447`, latest 8.x): picks up the AU-client editor-ownership and channel-info fixes (E6).
  - `AU_SANDBOX_SAFE TRUE`.
  - macOS defaults to universal `arm64;x86_64` with deployment target **10.15** (arm64 slices are 11.0 by definition).
- **Processor.**
  - Layouts: mono→mono, stereo→stereo and **mono→stereo**. Input and output channel counts are now handled separately; the old code derived the right-output pointer from the *input* count, which would have left the right output silent with mono→stereo.
  - Output gain defaults to **0 dB**, so Wet/Dry 0 and all-modules-off are exactly transparent at default gains (now tested).
- **Default level.** A new test measured the default chain on a bright saw chord at **−5.1 dB** (about −8 dB with the old −3 dB output default). The wet layers were linearly crossfaded with the dry signal, which loses energy for uncorrelated layers. Crypt, Echoes, Wraith and Choir now use an equal-power blend (exact at mix 0 and 1): **−2.5 dB**.

  Per-module probe (each alone): Crypt −1.1, Abyss −0.6 (its resonance compensation also lowers the passband), Choir −0.4, Echoes −0.3; the Ritual Engine adds −0.4. These belong to the Milestone 2 rewrites, which must reach the brief's ±2 dB. The Milestone 1 regression guard is ±3 dB.
- **CI (`build.yml`).** In order:
  1. universal build;
  2. processor tests on arm64 and under Rosetta (x86_64 slice);
  3. `ci-check-bundles.sh` gate: both slices in AU/VST3/Standalone, AU identity, `sandboxSafe` true;
  4. optional Developer ID path;
  5. `package-mac.sh`: `codesign --force --deep --sign -` every bundle, verify strict, zip `VEIL-macOS-Universal.zip` with ditto;
  6. **`ci-user-install-test.sh`**: unzip, quarantine every file like a Chrome download, record what macOS does with the quarantined copy, install with `install-mac.sh --ci`, then `auval` and the harness `--strict` on arm64 and x86_64 (every step time-limited).

  The installable artifact is uploaded only when all of that passes. Logs go out as annotations, readable through the REST API, because this environment cannot download artifacts. A `release` job publishes the same zip for `v*` tags.
- **Harness.** If the v2 API does not offer `kAudioUnitProperty_RequestViewController` for an out-of-process instance, that is now reported rather than failed (Apple does not promise it). The `AUAudioUnit` `requestViewController` path, the one modern hosts use, stays required.

### Verified so far

- Linux: JUCE 8.0.16 build with VST3 and Standalone; **485 passed, 0 failed**.
- macOS CI: pending (run for `4d78227`).
- GarageBand on the user's Mac: pending (Milestone 1 gate).

---

## Bug report: old version (05cc9e5)

| ID | Bug | Status | Evidence | Fix |
|---|---|---|---|---|
| B1 | AU not marked sandbox-safe, so GarageBand hosts it out-of-process | confirmed (plist) | E1 | Milestone 1 |
| B2 | arm64-only binary; Intel Macs and Rosetta hosts cannot load it in-process | confirmed | E2 | Milestone 1 |
| B3 | CI published installable bundles from failed runs, including one with an unsealed linker signature | confirmed | E3 | Milestone 1 |
| B4 | No install path: quarantine never removed, AU registrar never reset, duplicates never detected. **This is the GarageBand failure** (see B7) | confirmed on the user's Mac | E8; user report | Milestone 1 |
| B5 | No mono→stereo layout | confirmed (`auval`) | E4 | Milestone 1 |
| B6 | AU editor and out-of-process hosting never exercised by any test | confirmed | E4 | harness added; enforced in Milestone 1 |
| B7 | GarageBand silence and generic UI: Gatekeeper rejects the quarantined ad-hoc-signed bundle, so `AUHostingServiceXPC_arrow` cannot load it and GarageBand's `OpenAsync` returns −1 | **confirmed** on the user's Mac | "Root cause" above | Milestone 1: install script + quarantined-install CI test |
| B8 | CI signed only the AU; the Standalone `VEIL.app` in the artifact kept a linker-only signature and was blocked on the user's Mac | confirmed on the user's Mac | user report | Milestone 1: sign every bundle; Standalone no longer shipped |
