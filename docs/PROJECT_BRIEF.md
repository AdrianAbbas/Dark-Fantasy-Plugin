# CODEX PROJECT BRIEF: VEIL — Dark Fantasy Audio Effects Plugin

You are an expert C++ audio DSP engineer, JUCE developer, and professional audio plugin UI/UX designer.

Your task is to design, implement, build, and test a fully functional audio effects plugin called **VEIL**, intended primarily for Logic Pro on macOS.

VEIL transforms ordinary synths, pads, pianos, guitars, and other audio sources into atmospheric, cinematic, haunting dark fantasy sounds.

The artistic direction is inspired by the atmosphere of Dark Souls, Elden Ring, Skyrim, Bloodborne, ancient medieval ruins, gothic cathedrals, forgotten kingdoms, ritual magic, and dark ambient soundtracks.

This must be a REAL WORKING AUDIO PLUGIN, not a web application, interface mockup, or visual prototype.

## 1. TECHNICAL REQUIREMENTS

Build the plugin using:

- C++20, or C++17 if compatibility requires it
- JUCE framework
- CMake
- Xcode/macOS toolchain
- Audio Unit (AUv2) format for Logic Pro
- VST3 format as an additional output if practical
- Standalone application for testing if practical
- Native macOS support, prioritizing Apple Silicon
- Intel macOS compatibility if feasible

The plugin should function as an audio effect inserted after existing software instruments, NOT as a separate synthesizer.

It must process incoming stereo or mono audio in real time.

Requirements:

- Fully functional DSP, with every control affecting the sound
- Sample-rate-independent processing
- Smooth parameter changes without clicks
- Logic Pro automation support
- Host tempo synchronization where applicable
- Preset saving and loading
- Proper plugin state serialization
- Adjustable wet/dry mix
- Input and output gain controls
- Global bypass
- Low CPU consumption where practical
- No real-time audio-thread allocations or locks
- Silence-safe behavior and no NaN or infinite audio output
- Stable operation at 44.1, 48, 88.2, and 96 kHz where feasible

Use JUCE AudioProcessorValueTreeState or an equivalent robust parameter-management architecture.

## 2. SOUND DESIGN MODULES

The plugin must contain six processing modules. Each module should have its own enable/disable switch, adjustable parameters, and a clearly recognizable sound.

### MODULE I — THE ABYSS (Dark Filtering)

Create a flexible tonal-shaping module for turning bright, modern synthesizers into darker, aged, mysterious instruments.

Controls:

- Cutoff: adjustable low-pass frequency
- Resonance: resonance around the cutoff
- High-Pass: remove unwanted sub-bass
- Slope: 12 dB/oct or 24 dB/oct
- Darkness: macro controlling high-frequency attenuation and subtle spectral coloration
- Movement: subtle modulated filtering
- Movement Rate: free or tempo-synchronized modulation

Include smooth filter automation and prevent unstable resonance.

Sound goals: muffled medieval synthesizers, dark atmospheric pads, worn orchestral textures, distant instruments.

### MODULE II — THE CRYPT (Cathedral Reverb)

Create a dark, spacious, cinematic reverb engine.

The reverb should evoke ancient stone cathedrals, underground chambers, abandoned castles, crypts, and immense fantasy environments.

Controls:

- Space: room size
- Decay: reverb tail duration
- Pre-Delay
- Damping: high-frequency decay
- Low Cut: prevent muddy reverb
- Shimmer: subtle upper-octave atmospheric component
- Width: stereo spaciousness
- Mix

Provide selectable acoustic characters:

1. Forgotten Chapel
2. Ancient Cathedral
3. Underground Crypt
4. Endless Void

These must result in actual DSP differences, not merely renamed presets.

A feedback delay network or similarly appropriate algorithm is acceptable. Keep feedback stable and bounded.

### MODULE III — THE WRAITH (Spectral Haunting)

Design a spectral effect that creates ghostly, unstable, ethereal textures from incoming sound.

Controls:

- Spectral Blur: smears and softens spectral detail
- Detune: subtle pitch instability
- Drift: slow modulation
- Ghost: amount of delayed, pitch-altered spectral material
- Freeze: captures and sustains a spectral texture
- Mix

The Freeze feature must hold a genuine processed audio texture, not simply mute the input or repeat a tiny unprocessed buffer.

Use a suitably designed STFT, overlap-add, granular, or equivalent approach.

Handle windowing, phase behavior, and latency carefully.

Sound goals: ghost choirs, haunted synthesizers, spectral drones, cursed ambience.

### MODULE IV — ANCIENT BLOOD (Saturation and Degradation)

Create a tasteful nonlinear processing module designed to make modern synths feel ancient, imperfect, and organic.

Controls:

- Drive
- Texture: clean warmth to coarse distortion
- Age: progressively reduced fidelity and softened high-frequency detail
- Dust: subtle optional noise texture
- Instability: randomized analog-style pitch/level movement
- Mix

Include anti-aliasing or oversampling where justified.

Avoid excessive noise or distortion at default values.

Sound goals: dusty ancient instruments, degraded magical artifacts, worn tape-like textures, gritty fantasy drones.

### MODULE V — THE ECHOES (Mystical Delay)

Build a musical delay engine capable of producing haunting, spatially distant repetitions.

Controls:

- Time: milliseconds or host tempo synchronization
- Feedback
- Damping
- Diffusion
- Reverse: reverse-delay texture mode
- Stereo Spread
- Mix

Reverse mode should use a buffered reverse-delay implementation with sensible behavior and clear latency or timing expectations.

Feedback must be stable and never produce uncontrolled volume growth.

### MODULE VI — THE CHOIR (Phantom Harmonics)

Create a spectral or pitch-based layer that turns an ordinary synth into something resembling a distant supernatural ensemble.

Controls:

- Voice Count: 1–4 additional voices
- Interval: octave below, fifth, octave above, and other useful intervals
- Detune
- Formant Character
- Movement
- Blend

The purpose is not perfect realistic vocal synthesis. Instead, it should create an eerie choir-like harmonic character that blends with sustained input notes.

Use a genuine harmonizing or spectral approach. Do not mislabel a simple chorus as a pitch-shifting choir.

Design practical CPU and latency tradeoffs.

## 3. UNIQUE FEATURE — THE RITUAL ENGINE

This is the signature feature of VEIL and should be the most visually distinctive part of the interface.

Create a large interactive mystical sigil in the center of the plugin.

The sigil functions as a two-dimensional sound-morphing interface.

Three points around the sigil represent three archetypes:

**SHADOW**
- Dark
- Warm
- Subdued
- Subterranean

**SPIRIT**
- Ethereal
- Wide
- Shimmering
- Ghostly

**RUIN**
- Degraded
- Unstable
- Distorted
- Ancient

The user drags a glowing orb inside a triangular ritual circle.

The orb's position continuously morphs the sound between these three archetypes.

Technical requirements:

- Convert orb position to three normalized barycentric weights
- Map these weights to carefully selected DSP parameters across multiple modules
- Smooth all interpolated parameters
- Avoid unexpected output-level spikes while morphing
- Allow the Ritual Engine to be bypassed
- Preserve sensible manual control over module parameters
- Expose automatable X and Y coordinates to Logic Pro
- Clearly indicate when a parameter is controlled by the Ritual Engine

The three archetypes should have distinctly different auditory characteristics.

Example:

Near SHADOW, the sound becomes darker with subdued filtering and deep reverb.

Near SPIRIT, spectral effects, ethereal harmonics, width, and shimmer become more prominent.

Near RUIN, saturation, degradation, instability, and distressed textures become stronger.

Also include a **RITUAL MOTION** feature.

When enabled, the glowing orb can travel slowly through the triangle automatically.

Controls:

- Motion Rate
- Motion Depth
- Motion Pattern: Circle, Pendulum, Wander
- Host Sync
- Motion Enable

Wander should use smooth, bounded randomized movement, rather than abrupt parameter jumps.

This feature should be genuinely useful for generating constantly evolving ambient soundscapes.

## 4. VISUAL DESIGN

The plugin should feel like a polished, premium fantasy artifact rather than conventional audio software.

Visual identity:

- Obsidian-black background
- Subtle stone, parchment, or tarnished-metal textures
- Deep burgundy and muted bronze accents
- Antique silver labels
- Thin ornamental gothic borders
- Arcane geometric shapes and magical sigils
- Soft animated glowing effects
- Sophisticated typography inspired by medieval manuscripts, while remaining readable

Avoid:

- Generic JUCE default controls
- Basic rectangular grey panels
- Excessive neon
- Cartoon fantasy aesthetics
- Cluttered layouts
- Unreadable decorative fonts
- Stock-looking knobs

### Layout

TOP:

VEIL logo centered.

Subheading: "DARK FANTASY PROCESSOR"

Preset browser, settings, bypass, and A/B comparison controls.

CENTER:

Large interactive Ritual Engine sigil with three archetype labels.

The sigil should glow subtly in response to the incoming audio signal.

LEFT AND RIGHT:

Six processing modules arranged as beautifully designed panels around the ritual interface.

Each module should have collapsible or selectable detailed controls if necessary to avoid crowding.

BOTTOM:

- Input and output meters
- Global wet/dry
- Input/output gain
- Stereo width indication
- CPU usage indicator if feasible
- Visual signal activity

### Control Design

Create custom rotary controls inspired by antique dials, celestial instruments, and medieval navigation tools.

Knobs should include:

- Smooth mouse dragging
- Double-click reset
- Numerical parameter readout
- Clear parameter labels
- Hover highlighting
- Host automation linkage

### Animation

Implement tasteful, GPU-efficient or CPU-efficient animations:

- Slowly rotating sigil markings
- Subtle pulsating glow
- Audio-reactive light changes
- Smooth knob movements
- Glowing Ritual Engine orb
- Soft transitions when changing archetypes

Animations should be cosmetic and must not affect DSP timing.

Keep visual effects restrained and professional.

The final interface should look good enough for a commercial audio plugin.

## 5. FACTORY PRESETS

Create at least 15 presets with meaningful DSP settings.

1. Forgotten Kingdom — atmospheric, ancient, spacious
2. Cathedral at Midnight — enormous dark reverb
3. The Hollow Choir — spectral harmonic ensemble
4. Abyssal Drone — deep evolving ambient sound
5. Cursed Forest — unstable organic textures
6. Fallen Angel — dark ethereal shimmer
7. Ashes of a King — degraded melancholic ambience
8. Moonlit Ruins — delicate, mysterious textures
9. The Black Citadel — massive low-frequency darkness
10. Ghosts of the Chapel — distant spectral echoes
11. Witch's Lament — haunting melodic character
12. Eternal Winter — cold spectral atmosphere
13. The Final Ritual — heavy evolving transformation
14. Ancient Spellbook — dusty lo-fi magic
15. Throne of Shadows — cinematic dark fantasy

Make each preset audibly different.

Presets should initialize all relevant parameters and load reliably.

## 6. DSP QUALITY AND PERFORMANCE

Prioritize sound quality and reliability over implementing every possible feature poorly.

Requirements:

- No clipping caused by careless internal gain staging
- Denormal protection
- Smooth bypass transitions
- Stable feedback networks
- Correct mono/stereo handling
- Proper buffer initialization
- Thread-safe parameter communication
- Predictable plugin latency reporting
- No heap allocation inside processBlock
- Sample-accurate or sufficiently smoothed modulation where appropriate
- Correct reinitialization when sample rate changes
- Robust plugin state restoration

Use established DSP design principles and JUCE facilities where appropriate.

For unusually complex modules, prefer a stable and audible first implementation over a broken advanced algorithm.

## 7. IMPLEMENTATION PLAN

Work in stages, but actually implement the project.

Stage 1:
Create the JUCE/CMake project, AU effect target, parameter tree, basic editor, audio routing, and working bypass.

Stage 2:
Implement Abyss, Ancient Blood, and Echoes, with audible DSP behavior.

Stage 3:
Implement Crypt reverb, Wraith spectral processing, and Choir harmonic processing.

Stage 4:
Implement the Ritual Engine with automated morphing.

Stage 5:
Replace the basic interface with the complete custom fantasy-themed GUI.

Stage 6:
Implement presets, automation, metering, state persistence, and polish.

Stage 7:
Build, run available tests, validate the plugin, and document installation.

Do not stop after writing a plan or project skeleton.

Create actual source files and functional implementations.

## 8. TESTING

Implement appropriate automated tests where feasible, including:

- Parameter range checks
- State save/load consistency
- Processing silent input
- Processing sustained tones and impulses
- Repeated sample-rate changes
- Stable feedback at extreme settings
- No NaN/Inf output
- Stereo and mono operation
- Ritual morph boundaries and interpolation
- Preset loading
- Repeated plugin initialization/destruction

If the environment supports macOS builds, compile the plugin and fix build errors.

If it does not support a macOS toolchain, do not claim to have built or validated it. Instead, provide complete source code and exact macOS build instructions.

Use Apple's AU validation utility where available and explain how to run the Logic Pro plugin validation process.

## 9. PROJECT DELIVERABLES

Create an organized project with:

- CMakeLists.txt
- PluginProcessor.h/.cpp
- PluginEditor.h/.cpp
- Dedicated DSP module files
- RitualEngine.h/.cpp
- Custom visual component files
- Preset management files
- Required resources/assets
- Tests where practical
- README.md

Provide:

1. All source code
2. A complete buildable JUCE project
3. An Audio Unit .component build artifact when the build environment supports it
4. An optional VST3 artifact
5. Installation instructions for Logic Pro
6. A guide to controls and processing modules
7. A factory preset library
8. Notes about implemented features, limitations, and known issues

Do not use copyrighted images, artwork, sound recordings, or trademarked game assets. Design all visual assets originally, using vector drawing or procedurally generated graphics where practical.

## 10. FINAL EXPECTATIONS

The goal is to create a plugin that musicians would genuinely enjoy using for dark fantasy sound design, dark ambient, dungeon synth, cinematic composition, experimental music, and atmospheric production.

The project should meet three standards:

**Sound:** Turning an ordinary, bright stock synthesizer into a haunting and evolving sound should be possible with minimal adjustment.

**Visuals:** The interface should resemble an ancient magical artifact, but retain the clarity, precision, and usability of a professional audio plugin.

**Identity:** The Ritual Engine must make VEIL feel different from typical reverb, delay, and multi-effects plugins.

Deliver a fully working first version rather than a visually impressive application with nonfunctional controls.

Start by inspecting the development environment, then build the project and implement the plugin. Make reasonable engineering decisions independently and document them. Do not stop at a conceptual design.
