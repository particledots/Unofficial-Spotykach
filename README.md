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

Milestone 4: both decks of the Spotykach looper with Reel, Slice and Drift modes (record, play, reverse,
trig, record from the other deck, position, size, pitch with optional snap, envelope, mix, feedback),
A/B crossfade, the three routing modes, tempo (internal or host sync), launch quantise and a resizable
window. Effects, modulators, the sequencer and MIDI control are not in yet.

## Licence

The plug-in as a whole is licensed under the **GNU AGPLv3** (see `LICENSE`), because it is built
with JUCE. Upstream MIT code keeps its own copyright notices; see `THIRD-PARTY-LICENSES.txt`.

## Docs

`docs/SPEC.md` (specification and findings) and `docs/decisions.md` (decisions and open questions).
