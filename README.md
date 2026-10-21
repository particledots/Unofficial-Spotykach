# Unofficial Spotykach

**Unofficial.** This is a personal, non-commercial port of the Spotykach dual-deck looper
firmware to an Audio Unit / VST3 plug-in for Logic Pro. It is **not** a Synthux Academy product
and is **not affiliated with, endorsed by, or supported by Synthux Academy**.

Spotykach is a hardware instrument by **Synthux Academy**. Original firmware:
<https://github.com/Synthux-Academy/Spotykach> (MIT licence, Copyright (c) 2026 Synthux Academy).

## Credits

Spotykach was designed by (per the upstream `CREDITS.md`):

- Roey Tsemah: concept, visual and hardware design
- Vlad Lytvynenko: concept, firmware design
- Nick Donaldson: electronics design, hardware drivers, bootloader
- Rosa Schuurmans: FX and LEDs

Code from Infrasonic Audio LLC and DaisySP (Electrosmith) is also used in the upstream firmware.
Built with [JUCE](https://juce.com).

## Status

Milestone 7: both decks of the Spotykach looper with Reel, Slice and Drift modes, Flux (delay) and Grit
(drive, reduce, filter) effects, two modulators that can be patched to a destination, a trigger
sequencer per deck, MIDI notes and CCs, CV-style automation inputs, fit tempo to loop, A/B crossfade,
the three routing modes, tempo (internal, host sync or tap), launch quantise and a resizable window.
Saving loops with the project is not in yet.

## MIDI

Needs Logic to deliver MIDI to the plug-in: in Logic Pro, MIDI-controlled effects are loaded in the
**Instrument slot of a Software Instrument track** (they are listed with the AU MIDI-controlled effects),
with the audio source chosen in the plug-in's **Side Chain** menu. (Not yet verified with this plug-in.)

Channels: Deck A listens on channel 1 and Deck B on channel 2 by default (changeable on the MIDI page).

- **Note on** (deck's channel): triggers the loop, pitched by (note - 60) semitones, +-5 octaves. The pitch applies to that
  triggered pass, as on the hardware.
- **CC 3** crossfade (any channel). **CC 14** record from input, **15** record from the other deck, **85** play,
  **86** reverse (values above 0).
- **CC 20** position, **21** slice offset, **22** size, **23** envelope, **24** pitch, **25** mix, **26** feedback,
  **27** envelope length, **28** window, **89** modulator rate, **90** modulator glow (Mod A for Deck A, Mod B for Deck B).
- **CC 102** Grit on, **103** intensity, **104** mix, **109** character; **105** Flux on, **106** time, **107** feedback, **108** mix.
- MIDI Start/Continue/Stop play and stop the decks that have **Follow transport** on; the host's own play/stop does the same.

The list is the firmware's (`third_party/spotykach/src/core/config.h`, `ui/core.ui.cpp`).

## Licence

The plug-in as a whole is licensed under the **GNU AGPLv3** (see `LICENSE`), because it is built
with JUCE. Upstream MIT code keeps its own copyright notices; see `THIRD-PARTY-LICENSES.txt`.

## Docs

`docs/SPEC.md` (specification and findings) and `docs/decisions.md` (decisions and open questions).
