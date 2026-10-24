# Manual host test checklist

GarageBand and Logic cannot be driven from CI. Run this on a real Mac and report the results (✓, ✗ and anything odd). A release is marked **host-verified** only after this checklist passes.

Record:
- Mac model and chip;
- macOS version;
- GarageBand / Logic version;
- the build: CI run number, or the sha256 of `VEIL.component/Contents/MacOS/VEIL`.

## A. Install

1. Download the **VEIL-macOS-Universal** artifact with your normal browser, so it is quarantined like a real user's download.
2. Unzip it and run `bash …/install-mac.sh`. Expected: every step `OK`, then `auval: PASS` and **"VEIL is installed and validated."**
3. If it fails, run `bash scripts/diagnose-mac.sh` and attach the report.

## B. GarageBand: Milestone 1 gate

1. New project → Software Instrument (any bright synth). Play a sustained chord: the instrument is audible.
2. Smart Controls → Plug-ins → Audio Units → Veil Audio → VEIL.
   - [ ] Sound still passes and is clearly processed: darker, with reverb and echoes.
   - [ ] The **VEIL interface** appears (not sliders). If it shows sliders, switch Controls → Editor and note which one GarageBand opened with.
   - [ ] Moving **Wet / Dry** changes the sound. At 0 % it sounds like the plain instrument.
   - [ ] VEIL's **BYPASS** button returns the plain instrument.
   - [ ] The plug-in's power button in Smart Controls (host bypass) returns the plain instrument.
   - [ ] Switching factory presets in the VEIL menu changes the sound. GarageBand's own preset menu lists the 15 presets.
3. Save the project, quit GarageBand, reopen the project.
   - [ ] VEIL is still there with the same settings.

## C. Logic Pro

1. Plug-in Manager shows VEIL as successfully validated.
2. Software-instrument track → Audio FX → Audio Units → Veil Audio → VEIL. Repeat B.2 and B.3.
3. Mono audio track: the insert menu offers VEIL as Mono and as Mono → Stereo. Both pass sound; Mono → Stereo is audible on both sides.
4. Automate **Cutoff** and **Wet / Dry**. Playback follows the automation.

## D. Every module and preset (Milestone 2 onward)

- [ ] Each module alone (others off) is audible and its controls change the sound.
- [ ] Every factory preset sounds different and is roughly as loud as the dry instrument.

## E. The Haunting (Milestone 3 onward)

- [ ] Play a phrase for 30 s. Ghosts appear and are recognisably drawn from earlier playing.
- [ ] Binding relics, Seal, Banish, and save/reopen with bound relics.

## F. Intel Mac (if available)

- [ ] Steps A–C on an Intel Mac.
