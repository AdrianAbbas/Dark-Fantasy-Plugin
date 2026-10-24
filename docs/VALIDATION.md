# VEIL first-version validation

## Scope

Native Linux x86_64 build using GCC 14.2, C++20, CMake 4.4.4, Ninja 1.13.2 and pinned JUCE 8.0.6. Formats: VST3 and Standalone. GUI checks run under Xvfb. The current cloud instance has no usable macOS/Xcode toolchain or physical audio-device test setup.

## Automated checks

**Result: 474 assertions passed, 0 failed; CTest 1/1 suite passed.** Native editor rendering also succeeded.

The console regression suite tests all 61 declared parameters, their ranges and automation endpoints; all 15 complete factory programs; binary host state and file preset round trips; malformed state rejection; A/B recall; mono/stereo unity routing; dry mix and calibrated output gain; smoothly transitioning global and host bypass; nonfinite input protection; silence, sustained tones and impulses; repeated preparation and sample-rate changes at 44.1/48/88.2/96 kHz; extreme feedback/drive/resonance settings; repeated processor/editor lifecycle and editor resizing; and Ritual barycentric coordinates, motion, boundaries and archetype differences.

Direct DSP tests check low-pass attenuation, saturation harmonics, distinct FDN chamber responses, real buffered reverse response, sustained processed freeze after input stops, and pitch-shifted octave/fifth energy. The freeze test retains a 1.25-second processed texture; it is not an FFT-bin test.

The allocation probe intercepts C++ new/new[] and aligned variants on the processing thread after warmup. It does not instrument arbitrary C malloc, external host code or other threads. Inspection finds no explicit locks or history-buffer allocations in the sample-processing implementations.

CPU measurement is illustrative, not a release benchmark: the 96 kHz stress case measured approximately 8–10% of real time on this instance. Performance depends on host, buffer size, processor and settings.

## Actual VST3 hosting

**Result: GUI-inclusive pluginval passed (exit 0).** The Standalone executable also stayed alive for a five-second Xvfb smoke test; physical audio playback was not exercised. Both binaries resolved their runtime libraries without the rootless sysroot. Incremental rebuild repeatability was confirmed with a subsequent no-op build.

Tracktion **pluginval v1.0.4**, strictness 5, with 44.1/48/88.2/96 kHz and 64/256/1024 sample buffers. Includes scanning, cold/warm load, all 15 programs, editor open/close, editor automation, audio processing, parameter automation with 32-sample subblocks, state persistence and available bus-layout tests. See `pluginval.txt` for the final complete run and exact outcomes.

An early GUI-inclusive run found an editor initialization-order crash. The editor was laying out controls before they were constructed. That was fixed; editor creation/resizing/destruction are now part of the default regression suite. Subsequent GUI-inclusive host validation passed.

pluginval's `auval` section on Linux does **not** execute Apple's validator. Its external Steinberg SDK validator is skipped because that separate tool is not installed. Neither should be reported as an AU or Steinberg certification.

## Remaining target-platform validation

**Not run:** macOS compilation, AUv2 artifact generation, Apple Silicon/Intel runtime checks, Apple's `auval`, Logic Pro scan/automation/session reopening, code signing, notarization, physical audio hardware, and musician listening evaluation. A macOS build/auval CI workflow is supplied but has not been run by this cloud task. README provides exact build and installation instructions.

The delivered Linux VST3/Standalone artifacts are Linux binaries and cannot be installed into Logic Pro. They depend on a compatible modern Linux runtime (cloud build: glibc 2.41), rather than being a universal distribution package.

The Wraith/Choir/reverse wet paths deliberately add delay and texture. The direct path is immediate and host latency is reported as 0 samples. Pitch/granular artifacts, approximate 2× saturation anti-aliasing, non-serialized frozen audio and tempo-following rather than transport-locked motion are documented first-version tradeoffs.
